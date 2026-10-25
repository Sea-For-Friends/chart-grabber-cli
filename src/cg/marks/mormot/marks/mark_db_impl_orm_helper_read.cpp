// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/MarksDB/mORMot/Marks/u_MarkDbImplORMHelper.pas :1012-1174 (ReadMarkSQL), :1349-1644 (the four
// _FillPrepare* functions) and :1646-1751 (GetMarkRecArray); docs/specs/marks.md §2.9.8 R-1, R-2; statements
// S-30 ... S-36, S-3A ... S-3F. The line numbers in the comments are those of that unit.
#include "cg/geo/wkb.h"
#include "cg/marks/mormot/mark_system_orm_statement.h"
#include "cg/marks/mormot/mark_system_orm_tools.h"
#include "cg/marks/mormot/marks/mark_db_impl_orm_helper.h"

#include <QByteArray>

#include <algorithm>
#include <vector>

namespace cg::marks {

namespace {

using storage::sqlite::SqliteStmt;

// The geo type column as the legacy enum cast TOrmGeoType(integer) (a byte-sized enum).
OrmGeoType geoTypeOf(const SqliteStmt& row, int column) {
    return static_cast<OrmGeoType>(static_cast<quint8>(row.columnInt64(column)));
}

// One row of S-30 / S-31 (a TOrmMark with the fields that were asked for).
struct MarkMainRow {
    bool found = false;
    qint64 id = 0;
    qint64 categoryId = 0;
    qint64 imageId = 0;
    qint64 appearanceId = 0;
    QString name;
    QString desc;
    OrmGeoType geoType = OrmGeoType::Undef;
    qint32 geoCount = 0;
};

} // namespace

// :1012-1174. Assert((AMarkID > 0) or (AMarkName <> '')) (:1033) is off in the shipped build: with neither
// the routine answers false (the last `else Exit` of :1083-1085).
bool MarkDbImplORMHelper::readMarkSql(OrmMarkRec& markRec, qint64 markId, qint64 categoryId,
                                      const QString& markName) {
    markRec = OrmMarkRec{};

    QByteArray where;
    std::vector<OrmArg> whereArgs;
    if (markId > 0) {
        // `index` points into the id index; its ids are copied at once, and the only call that changes the
        // index (deleteMarkSql, below) is followed by the return: the pointer is not read after it.
        if (const OrmMarkIdIndexRec* index = cache_.markIdIndex->find(markId)) {
            // fill id's from cache
            markRec.markId = markId;
            markRec.categoryId = index->categoryId;
            markRec.picId = index->imageId;
            markRec.appearanceId = index->appearanceId;
            if (const OrmMarkRow* cached = cache_.markCache->find(index->id)) {
                // fill main params from cache
                markRec.name = cached->name;
                markRec.desc = cached->desc;
                markRec.geoType = cached->geoType;
                markRec.geoCount = cached->geoCount;
            } else {
                // get main params from db: S-30
                MarkMainRow row;
                runStatement(provider_,
                             "SELECT RowID,mName,mDesc,mGeoType,mGeoCount FROM Mark WHERE RowID=? LIMIT 1",
                             {markId}, [&](const SqliteStmt& r) {
                                 row.found = true;
                                 row.name = r.columnText(1);
                                 row.desc = r.columnText(2);
                                 row.geoType = geoTypeOf(r, 3);
                                 row.geoCount = static_cast<qint32>(r.columnInt64(4));
                             });
                if (row.found) {
                    markRec.name = row.name;
                    markRec.desc = row.desc;
                    markRec.geoType = row.geoType;
                    markRec.geoCount = row.geoCount;
                    // add to cache
                    cache_.markCache->addOrUpdate(markRec);
                } else {
                    // purge the dangling index entry (:1064); the result is ignored (false when read-only)
                    deleteMarkSql(markId, true);
                    return false;
                }
            }
        } else {
            where = "RowID=?";
            whereArgs = {markId};
        }
    } else if (!markName.isEmpty()) {
        if (categoryId > 0) {
            where = "mName=? AND mCategory=?";
            whereArgs = {markName, categoryId};
        } else {
            where = "mName=?";
            whereArgs = {markName};
        }
    } else {
        return false;
    }
    if (!where.isEmpty()) {
        // get all from db: S-31 / S-32
        MarkMainRow row;
        runStatement(provider_,
                     "SELECT RowID,mCategory,mImage,mAppearance,mName,mDesc,mGeoType,mGeoCount FROM Mark "
                     "WHERE " +
                         where + " LIMIT 1",
                     whereArgs, [&](const SqliteStmt& r) {
                         row.found = true;
                         row.id = r.columnInt64(0);
                         row.categoryId = r.columnInt64(1);
                         row.imageId = r.columnInt64(2);
                         row.appearanceId = r.columnInt64(3);
                         row.name = r.columnText(4);
                         row.desc = r.columnText(5);
                         row.geoType = geoTypeOf(r, 6);
                         row.geoCount = static_cast<qint32>(r.columnInt64(7));
                     });
        if (!row.found) {
            return false;
        }
        // fill id's from db
        markRec.markId = row.id;
        markRec.categoryId = row.categoryId;
        checkId(markRec.categoryId);
        markRec.picId = row.imageId; // = 0 is OK
        markRec.appearanceId = row.appearanceId;
        checkId(markRec.appearanceId);
        // fill main params from db
        markRec.name = row.name;
        markRec.desc = row.desc;
        markRec.geoType = row.geoType;
        markRec.geoCount = row.geoCount;
        // add to cache
        cache_.markCache->addOrUpdate(markRec);
        cache_.markIdIndex->addOrUpdate(markRec);
        cache_.markIdByCategoryIndex->add(markRec.categoryId, markRec.markId);
    }

    const qint64 id = markRec.markId;

    // read geometry blob
    if (geo::GeometryLonLatPtr cachedGeometry; cache_.markGeometryCache->find(id, cachedGeometry)) {
        // found in cache
        markRec.geometry = cachedGeometry;
    } else {
        // read from db
        geo::PointsMeta meta; // empty = legacy nil (geometryMetaFromBlob; parseWkb drops an empty meta)
        // S-33
        const qint64 metaId =
            selectRowId(provider_, "SELECT RowID FROM MarkMeta WHERE mMark=? LIMIT 1", {id});
        if (metaId > 0) {
            // S-34
            runStatement(provider_, "SELECT mMeta FROM MarkMeta WHERE RowID=?", {metaId},
                         [&](const SqliteStmt& r) { meta = geometryMetaFromBlob(r.columnBlob(0)); });
        }

        // S-35
        bool blobFound = false;
        QByteArray pointsBlob;
        runStatement(provider_, "SELECT mGeoWKB FROM Mark WHERE RowID=?", {id}, [&](const SqliteStmt& r) {
            blobFound = true;
            pointsBlob = r.columnBlob(0);
        });
        checkRetrieveResult(blobFound);
        markRec.geometry = geometryFromBlob(pointsBlob, meta.isEmpty() ? nullptr : &meta);

        // add to cache (the size is the WKB bytes only, as in legacy)
        cache_.markGeometryCache->addOrUpdate(id, static_cast<qint32>(pointsBlob.size()), markRec.geometry);
    }

    // read view
    if (const OrmMarkViewRow* viewItem = cache_.markViewCache->find(id)) {
        // found in cache
        markRec.viewId = viewItem->viewId;
        markRec.visible = viewItem->visible;
    } else {
        if (!cache_.markViewCache->isPrepared()) {
            // read from db: S-36
            qint64 viewId = 0;
            bool viewVisible = false;
            runStatement(
                provider_,
                "SELECT RowID,mvCategory,mvVisible FROM MarkView WHERE mvMark=? AND mvUser=? LIMIT 1",
                {id, userId_}, [&](const SqliteStmt& r) {
                    viewId = r.columnInt64(0);
                    viewVisible = r.columnInt64(2) != 0;
                });
            if (viewId > 0) {
                markRec.viewId = viewId;
                markRec.visible = viewVisible;
            } else {
                markRec.visible = true;
            }
        }
        // add to cache
        cache_.markViewCache->addOrUpdate(markRec);
    }

    // read pic name
    readMarkImage(markRec);

    // read appearance
    readMarkAppearance(markRec);

    return true;
}

// :1349-1452. Returns the number of marks of the scope (0 when the query failed: legacy `Assigned(VList)`).
int MarkDbImplORMHelper::fillPrepareMarkIdIndex(qint64 categoryId) {
    const bool byCategory = categoryId > 0;

    bool listed = false;
    std::vector<OrmMarkIdIndexRec> rows;
    if (byCategory) {
        const std::vector<qint64>* known = nullptr;
        int knownCount = 0;
        if (cache_.markIdByCategoryIndex->find(categoryId, known, knownCount)) {
            return knownCount;
        }
        // S-3A
        listed = runStatement(provider_, "SELECT RowID,mImage,mAppearance FROM Mark WHERE mCategory=?",
                              {categoryId}, [&](const SqliteStmt& r) {
                                  OrmMarkIdIndexRec item;
                                  item.id = r.columnInt64(0);
                                  item.imageId = r.columnInt64(1);
                                  item.appearanceId = r.columnInt64(2);
                                  item.categoryId = categoryId;
                                  rows.push_back(item);
                              });
    } else {
        if (cache_.markIdIndex->isPrepared()) {
            return cache_.markIdIndex->count();
        }
        // S-3B
        listed =
            runStatement(provider_, "SELECT RowID,mImage,mAppearance,mCategory FROM Mark ORDER BY mCategory",
                         {}, [&](const SqliteStmt& r) {
                             OrmMarkIdIndexRec item;
                             item.id = r.columnInt64(0);
                             item.imageId = r.columnInt64(1);
                             item.appearanceId = r.columnInt64(2);
                             item.categoryId = r.columnInt64(3);
                             rows.push_back(item);
                         });
    }
    if (!listed) {
        return 0;
    }

    const int count = static_cast<int>(rows.size());
    if (!byCategory) {
        // Ensure that result is sorted by Category: TOrmTable.SortFields(3, True, nil, oftID) compares the
        // category and, for equal categories, the RowID (mormot.orm.base.pas TUtf8QuickSort.CompI)
        std::sort(rows.begin(), rows.end(), [](const OrmMarkIdIndexRec& a, const OrmMarkIdIndexRec& b) {
            return a.categoryId != b.categoryId ? a.categoryId < b.categoryId : a.id < b.id;
        });
    }
    std::vector<qint64> ids;
    ids.reserve(rows.size());
    for (const OrmMarkIdIndexRec& item : rows) {
        ids.push_back(item.id);
    }
    if (!byCategory) {
        // The groups of equal category go to the by-category index when the category changes. Legacy never
        // flushes the LAST group after the loop (new legacy defect found by T-0105, row numbered by the Tech
        // Lead): the highest category of the table is not registered here. The `else` of :1437
        // (Assert(False, 'List not ordered by Category!')) is off in the shipped build and cannot be reached
        // after the sort.
        int k = 0;
        qint64 currentCategory = 0;
        for (int i = 0; i < count; ++i) {
            const qint64 category = rows[static_cast<std::size_t>(i)].categoryId;
            if (currentCategory == 0) {
                currentCategory = category;
            }
            if (currentCategory == category) {
                ++k;
            } else if (currentCategory < category) {
                if (k > 0) {
                    cache_.markIdByCategoryIndex->addPrepared(currentCategory, ids, i - k, k);
                    k = 0;
                }
                currentCategory = category;
                ++k;
            }
        }
    }
    if (byCategory) {
        cache_.markIdIndex->addArray(rows);
        cache_.markIdByCategoryIndex->addPrepared(categoryId, ids, 0, count);
    } else {
        cache_.markIdIndex->addPrepared(rows);
    }
    return count;
}

// :1454-1497
void MarkDbImplORMHelper::fillPrepareMarkIdCache(qint64 categoryId) {
    QByteArray sql;
    std::vector<OrmArg> args;
    if (categoryId > 0) {
        if (cache_.markCache->isCategoryPrepared(categoryId)) {
            return;
        }
        // S-3C
        sql = "SELECT RowID,mName,mDesc,mGeoType,mGeoCount FROM Mark WHERE mCategory=?";
        args = {categoryId};
    } else {
        if (cache_.markCache->isPrepared()) {
            return;
        }
        sql = "SELECT RowID,mName,mDesc,mGeoType,mGeoCount FROM Mark";
    }
    std::vector<OrmMarkRow> rows;
    const bool listed = runStatement(provider_, sql, args, [&](const SqliteStmt& r) {
        OrmMarkRow item;
        item.id = r.columnInt64(0);
        item.name = r.columnText(1);
        item.desc = r.columnText(2);
        item.geoType = geoTypeOf(r, 3);
        item.geoCount = static_cast<qint32>(r.columnInt64(4));
        rows.push_back(std::move(item));
    });
    if (listed) {
        cache_.markCache->addPrepared(categoryId, rows);
    }
}

// :1499-1591
void MarkDbImplORMHelper::fillPrepareMarkGeometryCache(qint64 categoryId) {
    struct MarkBlob {
        qint64 id = 0;
        QByteArray blob;
    };

    QByteArray marksSql;
    QByteArray metaSql;
    std::vector<OrmArg> args;
    if (categoryId > 0) {
        if (cache_.markGeometryCache->isCategoryPrepared(categoryId)) {
            return;
        }
        // S-3D, S-3E
        marksSql = "SELECT RowID,mGeoWKB FROM Mark WHERE mCategory=?";
        metaSql = "SELECT A.mMark,A.mMeta FROM MarkMeta A WHERE A.mMark IN (SELECT B.RowID FROM Mark B WHERE "
                  "B.mCategory=?)";
        args = {categoryId};
    } else {
        if (cache_.markGeometryCache->isPrepared()) {
            return;
        }
        marksSql = "SELECT RowID,mGeoWKB FROM Mark";
        metaSql = "SELECT mMark,mMeta FROM MarkMeta";
    }

    std::vector<MarkBlob> marks;
    const bool marksListed = runStatement(provider_, marksSql, args, [&](const SqliteStmt& r) {
        marks.push_back(MarkBlob{r.columnInt64(0), r.columnBlob(1)});
    });
    DynArray<MarkBlob> metas; // sorted by the mark id (VMetaDynArr.Sort)
    runStatement(provider_, metaSql, args, [&](const SqliteStmt& r) {
        metas.items().push_back(MarkBlob{r.columnInt64(0), r.columnBlob(1)});
    });
    metas.sort();

    if (!marksListed) {
        return;
    }
    std::vector<OrmMarkGeometryRec> rows;
    rows.reserve(marks.size());
    for (const MarkBlob& mark : marks) {
        geo::PointsMeta meta;
        qint32 metaBlobSize = 0;
        int index = 0;
        if (metas.count() > 0 && metas.fastLocateSorted(mark.id, index)) {
            const QByteArray& metaBlob = metas.items()[static_cast<std::size_t>(index)].blob;
            meta = geometryMetaFromBlob(metaBlob);
            if (!meta.isEmpty()) { // legacy: VMeta <> nil
                metaBlobSize = static_cast<qint32>(metaBlob.size());
            }
        }
        OrmMarkGeometryRec item;
        item.id = mark.id;
        item.geometry = geometryFromBlob(mark.blob, meta.isEmpty() ? nullptr : &meta);
        item.size = static_cast<qint32>(mark.blob.size()) + metaBlobSize;
        rows.push_back(std::move(item));
    }
    cache_.markGeometryCache->addPrepared(categoryId, rows);
}

// :1593-1644
void MarkDbImplORMHelper::fillPrepareMarkViewCache(qint64 categoryId) {
    QByteArray sql;
    std::vector<OrmArg> args;
    if (categoryId > 0) {
        if (cache_.markViewCache->isCategoryPrepared(categoryId)) {
            return;
        }
        // S-3F
        sql = "SELECT RowID,mvMark,mvVisible FROM MarkView WHERE mvCategory=? AND mvUser=?";
        args = {categoryId, userId_};
    } else {
        if (cache_.markViewCache->isPrepared()) {
            return;
        }
        sql = "SELECT RowID,mvMark,mvVisible,mvCategory FROM MarkView WHERE mvUser=?";
        args = {userId_};
    }
    std::vector<OrmMarkViewRow> rows;
    const bool listed = runStatement(provider_, sql, args, [&](const SqliteStmt& r) {
        OrmMarkViewRow item;
        item.viewId = r.columnInt64(0);
        item.id = r.columnInt64(1);
        item.visible = !r.isNull(2) && r.columnInt64(2) != 0; // GetB: NULL and "0" are false
        item.categoryId = categoryId > 0 ? categoryId : r.columnInt64(3);
        rows.push_back(item);
    });
    if (listed) {
        cache_.markViewCache->addPrepared(categoryId, rows);
    }
}

// :1646-1751
int MarkDbImplORMHelper::getMarkRecArray(qint64 categoryId, bool includeHidden, bool includeGeometry,
                                         bool includeAppearance, std::vector<OrmMarkRec>& out) {
    out.clear();

    // :1654-1702. The pointers it reads (index row, mark row, view row) are used before anything is added to
    // a cache: the only caches it changes are the image and appearance caches (readMarkImage /
    // ...Appearance), never the id index, the mark cache or the by-category index.
    const auto fillMarkRec = [&](OrmMarkRec& markRec, const OrmMarkIdIndexRec& indexRec) {
        markRec.markId = indexRec.id;
        markRec.categoryId = indexRec.categoryId;
        markRec.picId = indexRec.imageId;
        markRec.appearanceId = indexRec.appearanceId;

        const OrmMarkRow* cached = cache_.markCache->find(indexRec.id);
        if (cached == nullptr) {
            return false; // Assert(False); Exit (:1668): the mark is left out
        }
        markRec.name = cached->name;
        markRec.desc = cached->desc;
        markRec.geoType = cached->geoType;
        markRec.geoCount = cached->geoCount;

        if (const OrmMarkViewRow* viewItem = cache_.markViewCache->find(indexRec.id)) {
            markRec.viewId = viewItem->viewId;
            markRec.visible = viewItem->visible;
            if (!includeHidden && !markRec.visible) {
                return false;
            }
        }

        if (includeGeometry) {
            geo::GeometryLonLatPtr geometry;
            if (!cache_.markGeometryCache->find(indexRec.id, geometry)) {
                return false; // Assert(False); Exit (:1691): the mark is left out (L-158, fixed: D-88)
            }
            markRec.geometry = geometry;
        }

        if (includeAppearance) {
            readMarkImage(markRec);
            readMarkAppearance(markRec);
        }
        return true;
    };

    const int count = fillPrepareMarkIdIndex(categoryId);
    if (count > 0) {
        fillPrepareMarkIdCache(categoryId);
        fillPrepareMarkViewCache(categoryId);
        if (includeGeometry) {
            fillPrepareMarkGeometryCache(categoryId);
        }

        out.reserve(static_cast<std::size_t>(count));

        if (categoryId > 0) {
            const std::vector<qint64>* ids = nullptr;
            int idCount = 0;
            if (cache_.markIdByCategoryIndex->find(categoryId, ids, idCount)) {
                // `ids` is a pointer into the live by-category index (legacy: a reference-counted array).
                // Nothing in this loop adds to, removes from or resets that index (see fillMarkRec), so it
                // stays valid and unchanged until the loop ends.
                for (int i = 0; i < idCount; ++i) {
                    if (const OrmMarkIdIndexRec* indexRec =
                            cache_.markIdIndex->find((*ids)[static_cast<std::size_t>(i)])) {
                        OrmMarkRec markRec;
                        if (fillMarkRec(markRec, *indexRec)) {
                            out.push_back(std::move(markRec));
                        }
                    }
                }
            }
        } else {
            // the id index is not changed by the loop either
            const std::vector<OrmMarkIdIndexRec>& indexRows = cache_.markIdIndex->rows();
            for (int i = 0; i < cache_.markIdIndex->count(); ++i) {
                OrmMarkRec markRec;
                if (fillMarkRec(markRec, indexRows[static_cast<std::size_t>(i)])) {
                    out.push_back(std::move(markRec));
                }
            }
        }
    }

    return static_cast<int>(out.size());
}

} // namespace cg::marks
