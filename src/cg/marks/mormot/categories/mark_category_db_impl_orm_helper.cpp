// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/MarksDB/mORMot/Categories/u_MarkCategoryDbImplORMHelper.pas (docs/specs/marks.md §2.9.6, S-10
// ... S-1C).
#include "cg/marks/mormot/categories/mark_category_db_impl_orm_helper.h"

#include "cg/marks/mormot/mark_system_orm_log.h"
#include "cg/marks/mormot/mark_system_orm_statement.h"
#include "cg/marks/mormot/mark_system_orm_tools.h"

#include <QByteArray>

namespace cg::marks {

namespace {

using storage::sqlite::SqliteStmt;

constexpr const char* kUpdateCategoryName = "UPDATE Category SET cName=? WHERE RowID=?"; // S-15

} // namespace

// :98-110 (the destructor, :112-116, only frees the cache)
MarkCategoryDbImplORMHelper::MarkCategoryDbImplORMHelper(bool isReadOnly,
                                                         IMarkSystemImplORMClientProvider& provider)
    : userId_(provider.userId()), provider_(provider), isReadOnly_(isReadOnly) {}

// :118-141
bool MarkCategoryDbImplORMHelper::deleteCategorySql(qint64 categoryId) {
    if (isReadOnly_) {
        return false;
    }

    checkId(categoryId);

    // first delete view for all users (if exists): S-1C, the result is ignored (:131)
    runWrite(provider_, "DELETE FROM CategoryView WHERE cvCategory=?", {categoryId});

    // then delete category: S-16 (:134)
    checkDeleteResult(runWrite(provider_, "DELETE FROM Category WHERE RowID=?", {categoryId}));

    // delete from cache
    cache_.categoryCache.remove(categoryId);
    cache_.categoryViewCache.remove(categoryId);

    return true;
}

// :143-189
bool MarkCategoryDbImplORMHelper::insertCategorySql(OrmCategoryRec& rec) {
    if (isReadOnly_) {
        return false;
    }

    // insert category: S-14 (:161)
    const qint64 categoryId = insertRow(provider_, "INSERT INTO Category (cName) VALUES (?)", {rec.name});
    checkId(categoryId);
    rec.categoryId = categoryId;
    // add to cache
    cache_.categoryCache.addOrUpdate(rec);

    // insert view (only for a hidden category: LC-16, a visible one gets no row) S-19 (:179)
    if (!rec.visible) {
        const qint64 viewId = insertRow(
            provider_,
            "INSERT INTO CategoryView (cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) VALUES (?,?,?,?,?)",
            {userId_, rec.categoryId, qint64{rec.visible ? 1 : 0}, qint64{rec.minZoom}, qint64{rec.maxZoom}});
        checkId(viewId);
        rec.viewId = viewId;
    }
    // add to cache
    cache_.categoryViewCache.addOrUpdate(rec);

    return true;
}

// :191-345
bool MarkCategoryDbImplORMHelper::updateCategorySql(const OrmCategoryRec& oldRec, OrmCategoryRec& newRec) {
    bool result = false;

    checkId(oldRec.categoryId);

    newRec.categoryId = oldRec.categoryId;

    const bool updateView = newRec.visible != oldRec.visible || newRec.minZoom != oldRec.minZoom ||
                            newRec.maxZoom != oldRec.maxZoom;

    if (updateView) {
        // the TOrmCategoryView of legacy: only the members used below
        qint64 viewId = 0;
        bool viewVisible = false;
        quint8 viewMinZoom = 0;
        quint8 viewMaxZoom = 0;

        bool found = false;
        if (const OrmCategoryViewRow* item = cache_.categoryViewCache.find(newRec.categoryId)) {
            found = true;
            // init from cache (copied: the row pointer is only valid until the next cache change)
            viewId = item->viewId;
            viewVisible = item->visible;
            viewMinZoom = item->minZoom;
            viewMaxZoom = item->maxZoom;
        } else if (!cache_.categoryViewCache.isPrepared()) {
            // init from db: S-17 (:268)
            runStatement(provider_,
                         "SELECT RowID,cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom FROM CategoryView "
                         "WHERE cvCategory=? AND cvUser=? LIMIT 1",
                         {newRec.categoryId, userId_}, [&](const SqliteStmt& row) {
                             found = true;
                             viewId = row.columnInt64(0);
                             viewVisible = row.columnInt64(3) != 0;
                             viewMinZoom = static_cast<quint8>(row.columnInt(4));
                             viewMaxZoom = static_cast<quint8>(row.columnInt(5));
                         });
        }
        if (!isReadOnly_) {
            if (found && viewId > 0) {
                // update existing: S-1A, the changed columns in the fixed order (:287)
                CsvFieldsBuilder fields;
                std::vector<OrmArg> args;
                if (newRec.visible != oldRec.visible) {
                    viewVisible = newRec.visible;
                    fields.add(QStringLiteral("cvVisible"));
                    args.emplace_back(qint64{viewVisible ? 1 : 0});
                }
                if (newRec.minZoom != oldRec.minZoom) {
                    viewMinZoom = newRec.minZoom;
                    fields.add(QStringLiteral("cvMinZoom"));
                    args.emplace_back(qint64{viewMinZoom});
                }
                if (newRec.maxZoom != oldRec.maxZoom) {
                    viewMaxZoom = newRec.maxZoom;
                    fields.add(QStringLiteral("cvMaxZoom"));
                    args.emplace_back(qint64{viewMaxZoom});
                }
                QByteArray sql = "UPDATE CategoryView SET ";
                const QByteArrayList columns = fields.fields;
                for (qsizetype i = 0; i < columns.size(); ++i) {
                    sql += (i == 0 ? "" : ",") + columns[i] + "=?";
                }
                sql += " WHERE RowID=?";
                args.emplace_back(viewId);
                checkUpdateResult(runWrite(provider_, sql, args));
            } else {
                // add new: S-19 with all five values of the new record (:296)
                viewId =
                    insertRow(provider_,
                              "INSERT INTO CategoryView (cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) "
                              "VALUES (?,?,?,?,?)",
                              {userId_, newRec.categoryId, qint64{newRec.visible ? 1 : 0},
                               qint64{newRec.minZoom}, qint64{newRec.maxZoom}});
                checkId(viewId);
            }
        }
        // update cache
        newRec.viewId = viewId; // can be zero
        cache_.categoryViewCache.addOrUpdate(newRec);
        result = true;
    }

    if (isReadOnly_) {
        return result;
    }

    if (newRec.name != oldRec.name) {
        // update name: S-15 (:318)
        checkUpdateResult(runWrite(provider_, kUpdateCategoryName, {newRec.name, newRec.categoryId}));
        // update cache
        cache_.categoryCache.addOrUpdate(newRec);

        // update sub categories names
        const QString searchName = oldRec.name + QLatin1Char('\\');
        const QString updateName = newRec.name + QLatin1Char('\\');

        if (fillPrepareCategoryCache() > 0) {
            // live rows of the cache (:327): the rename below is stored in place, the data size of the cache
            // is not touched (:332)
            std::vector<OrmCategoryRow>& categories = cache_.categoryCache.rows();
            for (int i = 0; i < cache_.categoryCache.count(); ++i) {
                OrmCategoryRow& category = categories[static_cast<size_t>(i)];
                const QString oldName = category.name;
                if (oldName.startsWith(searchName, Qt::CaseInsensitive)) {
                    // StringReplace without rfReplaceAll: the first case-insensitive occurrence (DV-5)
                    QString replaced = oldName;
                    replaced.replace(replaced.indexOf(searchName, 0, Qt::CaseInsensitive), searchName.size(),
                                     updateName);
                    category.name = replaced;
                    // update db: S-15 for the row (:336)
                    checkUpdateResult(runWrite(provider_, kUpdateCategoryName, {category.name, category.id}));
                }
            }
        }
        result = true;
    }
    return result;
}

// :347-432
bool MarkCategoryDbImplORMHelper::readCategorySql(OrmCategoryRec& rec, qint64 id, const QString& name) {
    rec = OrmCategoryRec{};

    // try get category from cache
    bool result = false;
    if (id > 0) {
        if (const OrmCategoryRow* item = cache_.categoryCache.find(id)) {
            result = true;
            rec.categoryId = item->id;
            rec.name = item->name;
        }
    } else if (!name.isEmpty()) {
        if (const OrmCategoryRow* item = cache_.categoryCache.find(name)) {
            result = true;
            rec.categoryId = item->id;
            rec.name = item->name;
        }
    }
    if (!result && !cache_.categoryCache.isPrepared()) {
        // not found in cache -> get from db
        qint64 rowId = 0;
        QString rowName;
        const auto onRow = [&](const SqliteStmt& row) {
            result = true;
            rowId = row.columnInt64(0);
            rowName = row.columnText(1);
        };
        if (id > 0) {
            // S-10 (:381)
            runStatement(provider_, "SELECT RowID,cName FROM Category WHERE RowID=?", {id}, onRow);
        } else if (!name.isEmpty()) {
            // S-11 (:384): cName is declared SYSTEMNOCASE, so this equality is NOT the AnsiSameText of the
            // cache look-up above (L-187)
            runStatement(provider_, "SELECT RowID,cName FROM Category WHERE cName=? LIMIT 1", {name}, onRow);
        }
        if (result) {
            checkId(rowId);
            rec.categoryId = rowId;
            rec.name = rowName;
            // add to cache
            cache_.categoryCache.addOrUpdate(rec);
        }
    }

    if (!result) {
        return false;
    }

    // try get view from cache
    if (const OrmCategoryViewRow* item = cache_.categoryViewCache.find(rec.categoryId)) {
        // view found in cache
        rec.viewId = item->viewId;
        rec.visible = item->visible;
        rec.minZoom = item->minZoom;
        rec.maxZoom = item->maxZoom;
        return result;
    }
    if (!cache_.categoryViewCache.isPrepared()) {
        // view not in cache -> get it from db: S-17 (:415)
        qint64 viewId = 0;
        bool visible = false;
        int minZoom = 0;
        int maxZoom = 0;
        bool found = false;
        runStatement(provider_,
                     "SELECT RowID,cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom FROM CategoryView "
                     "WHERE cvCategory=? AND cvUser=? LIMIT 1",
                     {rec.categoryId, userId_}, [&](const SqliteStmt& row) {
                         found = true;
                         viewId = row.columnInt64(0);
                         visible = row.columnInt64(3) != 0;
                         minZoom = row.columnInt(4);
                         maxZoom = row.columnInt(5);
                     });
        if (found) {
            checkId(viewId);
            rec.viewId = viewId;
            rec.visible = visible;
            rec.minZoom = static_cast<quint8>(minZoom);
            rec.maxZoom = static_cast<quint8>(maxZoom);
        }
    }
    // add view to cache (the defaults when nothing was found)
    cache_.categoryViewCache.addOrUpdate(rec);
    return result;
}

// :434-457
int MarkCategoryDbImplORMHelper::countCategorySql(const QString& name) {
    // S-12: bypasses the cache, cName compares through SYSTEMNOCASE (:446)
    int count = 0;
    const bool done = runStatement(provider_, "SELECT RowID FROM Category WHERE cName=?", {name},
                                   [&](const SqliteStmt&) { ++count; });
    return done ? count : 0;
}

// :459-486
int MarkCategoryDbImplORMHelper::fillPrepareCategoryCache() {
    if (!cache_.categoryCache.isPrepared()) {
        // S-13 (:467)
        std::vector<OrmCategoryRow> rows;
        const bool done =
            runStatement(provider_, "SELECT RowID,cName FROM Category", {}, [&](const SqliteStmt& row) {
                rows.push_back(OrmCategoryRow{row.columnInt64(0), row.columnText(1)});
            });
        // ExecuteList returned nil on failure: the cache stays unprepared
        if (done) {
            cache_.categoryCache.addPrepared(rows);
        }
    }
    return cache_.categoryCache.count();
}

// :488-521
int MarkCategoryDbImplORMHelper::fillPrepareCategoryViewCache() {
    if (!cache_.categoryViewCache.isPrepared()) {
        // S-18 (:496)
        std::vector<OrmCategoryViewRow> rows;
        const bool done = runStatement(
            provider_,
            "SELECT RowID,cvCategory,cvVisible,cvMinZoom,cvMaxZoom FROM CategoryView WHERE cvUser=?",
            {userId_}, [&](const SqliteStmt& row) {
                OrmCategoryViewRow view;
                view.viewId = row.columnInt64(0);
                view.id = row.columnInt64(1);
                view.visible = row.columnInt64(2) != 0; // GetB
                view.minZoom = static_cast<quint8>(row.columnInt(3));
                view.maxZoom = static_cast<quint8>(row.columnInt(4));
                rows.push_back(view);
            });
        if (done) {
            cache_.categoryViewCache.addPrepared(rows);
        }
    }
    return cache_.categoryViewCache.count();
}

// :523-591
void MarkCategoryDbImplORMHelper::setAllCategoriesVisibleSql(bool visible) {
    if (!isReadOnly_) {
        // update db: S-1B (:542); zero rows is a success
        checkUpdateResult(runWrite(provider_, "UPDATE CategoryView SET cvVisible=? WHERE cvUser=?",
                                   {qint64{visible ? 1 : 0}, userId_}));
    }
    const int viewCount = fillPrepareCategoryViewCache();
    OrmCategoryViewCache& viewCache = cache_.categoryViewCache;

    // update cache
    std::vector<OrmCategoryViewRow>& views = viewCache.rows();
    for (int i = 0; i < viewCount; ++i) {
        views[static_cast<size_t>(i)].visible = visible;
    }

    if (!visible) {
        const int categoryCount = fillPrepareCategoryCache();
        if (categoryCount > 0) {
            // the category cache is not changed below, so its rows stay valid (:557)
            const std::vector<OrmCategoryRow>& categories = cache_.categoryCache.rows();
            OrmCategoryRec rec; // cEmptyOrmCategoryRec
            for (int i = 0; i < categoryCount; ++i) {
                const qint64 categoryId = categories[static_cast<size_t>(i)].id;
                // the pointer is only read before the cache is changed
                const OrmCategoryViewRow* item = viewCount > 0 ? viewCache.find(categoryId) : nullptr;
                const bool isFound = item != nullptr;
                if (!isFound || item->viewId == 0) {
                    rec.categoryId = categoryId;
                    rec.visible = visible;

                    // add to db: S-19 (:577), with the zoom defaults of the empty record
                    if (!isReadOnly_) {
                        const qint64 viewId = insertRow(
                            provider_,
                            "INSERT INTO CategoryView (cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) "
                            "VALUES (?,?,?,?,?)",
                            {userId_, rec.categoryId, qint64{rec.visible ? 1 : 0}, qint64{rec.minZoom},
                             qint64{rec.maxZoom}});
                        checkId(viewId);
                        rec.viewId = viewId;
                    } else {
                        rec.viewId = 0; // fake id
                    }
                    // add to cache
                    viewCache.addOrUpdate(rec);
                }
            }
        }
    }
}

// :593-632
int MarkCategoryDbImplORMHelper::getCategoriesRecArray(std::vector<OrmCategoryRec>& out) {
    const int count = fillPrepareCategoryCache();
    int viewCount = 0;
    if (count > 0) {
        viewCount = fillPrepareCategoryViewCache();
    }

    out.assign(static_cast<size_t>(count), OrmCategoryRec{});
    if (count > 0) {
        const std::vector<OrmCategoryRow>& rows = cache_.categoryCache.rows(); // :614
        OrmCategoryViewCache& viewCache = cache_.categoryViewCache;
        for (int i = 0; i < cache_.categoryCache.count(); ++i) {
            OrmCategoryRec& rec = out[static_cast<size_t>(i)];
            rec.categoryId = rows[static_cast<size_t>(i)].id;
            rec.name = rows[static_cast<size_t>(i)].name;
            if (viewCount > 0) {
                if (const OrmCategoryViewRow* item = viewCache.find(rec.categoryId)) {
                    rec.viewId = item->viewId;
                    rec.visible = item->visible;
                    rec.minZoom = item->minZoom;
                    rec.maxZoom = item->maxZoom;
                }
            }
        }
    }
    return count;
}

// :634-642
void MarkCategoryDbImplORMHelper::setReadOnly(bool value) {
    if (isReadOnly_ != value) {
        if (isReadOnly_) {
            cache_.categoryViewCache.reset(); // remove possible fake id's
        }
        isReadOnly_ = value;
    }
}

} // namespace cg::marks
