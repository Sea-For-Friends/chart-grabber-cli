// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/MarksDB/mORMot/Marks/u_MarkDbImplORMHelper.pas :1753-1819 (_GetMarkIDArrayByRectSQL), :1968-2070
// (GetMarkRecArrayByRect), :2072-2106 (_GetMarkRecArrayByTextSQLite3), :2159-2269 (GetMarkRecArrayByText) and
// :2271-2279 (SetReadOnly); docs/specs/marks.md 2.9.9; statements S-41, S-43. The line numbers in the
// comments are those of that unit.
#include "cg/marks/mormot/mark_system_orm_statement.h"
#include "cg/marks/mormot/mark_system_orm_tools.h"
#include "cg/marks/mormot/marks/mark_db_impl_orm_helper.h"

#include <QByteArray>

#include <algorithm>
#include <vector>

namespace cg::marks {

namespace {

using storage::sqlite::SqliteStmt;

constexpr std::size_t kMaxCategoryArrayLen = 128; // cMaxArrayLen (:1976)

} // namespace

// :1753-1819. The statement is the legacy text; the four rectangle values are spliced as numeric literals
// (mORMot's :(15 significant digits): inline parameter, spec 2.9.2); the category and the three limits are
// bound.
int MarkDbImplORMHelper::getMarkIdArrayByRectSql(const std::vector<qint64>& categoryIds,
                                                 const geo::DoubleRect& rect, quint32 lonSize,
                                                 quint32 latSize, bool receiveCategoryId,
                                                 std::vector<MarkWithCategoryId>& out) {
    out.clear();

    QByteArray selectedRows = receiveCategoryId ? "Mark.RowID,Mark.mCategory" : "Mark.RowID";

    std::vector<OrmArg> args;
    QByteArray categoryWhere;
    if (categoryIds.size() == 1) {
        if (categoryIds[0] <= 0) {
            categoryWhere = ""; // Assert(False) (:1782) is off in the shipped build
        } else {
            categoryWhere = "AND Mark.mCategory=? ";
            args.emplace_back(categoryIds[0]);
        }
    } else if (categoryIds.size() > 1) {
        QByteArray csv;
        for (std::size_t i = 0; i < categoryIds.size(); ++i) {
            if (i > 0) {
                csv += ',';
            }
            csv += QByteArray::number(categoryIds[i]);
        }
        categoryWhere = "AND Mark.mCategory IN (" + csv + ") ";
    }

    // mLeft<=right AND mRight>=left AND mBottom<=top AND mTop>=bottom: edges that only touch intersect
    const QByteArray sql =
        "SELECT " + selectedRows + " FROM Mark,MarkRTree WHERE Mark.RowID=MarkRTree.RowID " + categoryWhere +
        "AND mLeft<=" + doubleLiteral(rect.right) + " AND mRight>=" + doubleLiteral(rect.left) +
        " AND mBottom<=" + doubleLiteral(rect.top) + " AND mTop>=" + doubleLiteral(rect.bottom) +
        " AND (Mark.mGeoType=? OR Mark.mGeoLonSize>=? OR Mark.mGeoLatSize>=?);";
    args.emplace_back(qint64{static_cast<quint8>(OrmGeoType::Point)});
    args.emplace_back(qint64{lonSize});
    args.emplace_back(qint64{latSize});

    runStatement(provider_, sql, args, [&](const SqliteStmt& row) {
        MarkWithCategoryId item;
        item.markId = row.columnInt64(0);
        if (receiveCategoryId) {
            item.categoryId = row.columnInt64(1);
        }
        out.push_back(item);
    });
    return static_cast<int>(out.size());
}

// :1968-2070
int MarkDbImplORMHelper::getMarkRecArrayByRect(std::vector<qint64>& categoryIds, const geo::DoubleRect& rect,
                                               bool includeHidden, const geo::DoublePoint& lonLatSize,
                                               std::vector<OrmMarkRec>& out) {
    out.clear();

    quint32 lonSize = 0;
    quint32 latSize = 0;
    lonLatSizeToInternalSize(lonLatSize, lonSize, latSize);

    // If there are too many categories, we fetch Mark IDs from all categories for the given Rect from the DB,
    // and then filter the results manually
    const bool filterByCategory = categoryIds.size() >= kMaxCategoryArrayLen;

    std::vector<qint64> queryCategoryIds;
    if (filterByCategory) {
        // legacy sorts the caller's array in place (when it is not flagged sorted)
        std::sort(categoryIds.begin(), categoryIds.end());
    } else {
        queryCategoryIds = categoryIds; // Slice(…, cMaxArrayLen): the whole list, it is shorter
    }

    // search mark id's (ctSQLite3 branch; the other client types are not ported)
    std::vector<MarkWithCategoryId> idArray;
    const int count =
        getMarkIdArrayByRectSql(queryCategoryIds, rect, lonSize, latSize, filterByCategory, idArray);

    // read marks data
    for (int i = 0; i < count; ++i) {
        const MarkWithCategoryId& item = idArray[static_cast<std::size_t>(i)];
        if (filterByCategory &&
            !std::binary_search(categoryIds.begin(), categoryIds.end(), item.categoryId)) {
            continue;
        }
        if (item.markId > 0) {
            OrmMarkRec markRec;
            if (readMarkSql(markRec, item.markId, 0, QString())) {
                if (!includeHidden && !markRec.visible) {
                    continue;
                }
                out.push_back(std::move(markRec));
            }
        }
    }
    return static_cast<int>(out.size());
}

// :2072-2106. `SELECT RowID FROM MarkFTS WHERE mName MATCH <text>{ LIMIT n}` (FtsMatch,
// orm/mormot.orm.rest.pas :1279-1284 -> OneFieldValues -> SqlFromSelect, db/mormot.db.core.pas:2716). Legacy
// concatenates the text between quotes; the port binds it (spec 2.9.9 D-fix, LC-10 / DV-7). A statement
// SQLite rejects gives no ids.
int MarkDbImplORMHelper::getMarkRecArrayByTextSqlite3(const QString& search, int maxCount,
                                                      bool searchInDescription, std::vector<qint64>& nameIds,
                                                      std::vector<qint64>& descIds) {
    int result = 0;
    nameIds.clear();
    descIds.clear();

    QByteArray limit;
    if (maxCount > 0) {
        limit = " LIMIT " + QByteArray::number(maxCount);
    }

    const auto match = [&](const char* column, std::vector<qint64>& ids) {
        runStatement(provider_, QByteArray("SELECT RowID FROM MarkFTS WHERE ") + column + " MATCH ?" + limit,
                     {search}, [&](const SqliteStmt& row) { ids.push_back(row.columnInt64(0)); });
        result += static_cast<int>(ids.size());
    };

    match("mName", nameIds);

    if (!searchInDescription || (maxCount > 0 && result >= maxCount)) {
        return result;
    }

    match("mDesc", descIds);
    return result;
}

// :2159-2269
int MarkDbImplORMHelper::getMarkRecArrayByText(const QString& text, int maxCount, bool includeHidden,
                                               bool searchInDescription, std::vector<OrmMarkRec>& out) {
    out.clear();

    // MergeArrUnique (:2167-2198): the union of two id arrays, sorted ascending
    const auto mergeArrUnique = [](std::vector<qint64> a, std::vector<qint64> b) {
        std::vector<qint64>& longer = a.size() >= b.size() ? a : b;
        const std::vector<qint64>& shorter = a.size() >= b.size() ? b : a;
        std::sort(longer.begin(), longer.end());
        std::vector<qint64> merged = longer;
        for (const qint64 id : shorter) {
            if (!std::binary_search(longer.begin(), longer.end(), id)) {
                merged.push_back(id);
            }
        }
        std::sort(merged.begin(), merged.end());
        return merged;
    };

    // D-97: AnsiLowerCase is QString::toLower
    const QString search = text.toLower();

    std::vector<qint64> nameIds;
    std::vector<qint64> descIds;
    const int found = getMarkRecArrayByTextSqlite3(search, maxCount, searchInDescription, nameIds, descIds);

    // read marks data
    if (found > 0) {
        std::vector<qint64> ids;
        if (!nameIds.empty() && !descIds.empty()) {
            ids = mergeArrUnique(nameIds, descIds);
        } else if (!nameIds.empty()) {
            ids = nameIds;
        } else if (!descIds.empty()) {
            ids = descIds;
        }
        for (const qint64 id : ids) {
            if (id > 0) {
                OrmMarkRec markRec;
                if (readMarkSql(markRec, id, 0, QString())) {
                    if (!includeHidden && !markRec.visible) {
                        continue;
                    }
                    out.push_back(std::move(markRec));
                }
            }
        }
    }
    return static_cast<int>(out.size());
}

// :2271-2279
void MarkDbImplORMHelper::setReadOnly(bool value) {
    if (isReadOnly_ != value) {
        if (isReadOnly_) {
            cache_.markViewCache->reset(); // remove possible fake id's
        }
        isReadOnly_ = value;
    }
}

} // namespace cg::marks
