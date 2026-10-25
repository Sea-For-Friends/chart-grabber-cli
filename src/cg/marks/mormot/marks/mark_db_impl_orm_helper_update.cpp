// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/MarksDB/mORMot/Marks/u_MarkDbImplORMHelper.pas :776-1010 (UpdateMarkSQL) and :1176-1347
// (UpdateMarkView, SetMarksInCategoryVisibleSQL); docs/specs/marks.md §2.9.8 W-2, W-5, W-6; statements
// S-23 ... S-27, S-2E, S-2F, S-36 ... S-39. The line numbers in the comments are those of that unit.
#include "cg/geo/wkb.h"
#include "cg/marks/mormot/mark_system_orm_statement.h"
#include "cg/marks/mormot/mark_system_orm_tools.h"
#include "cg/marks/mormot/marks/mark_db_impl_orm_helper.h"

#include <QByteArray>

#include <vector>

namespace cg::marks {

namespace {

using storage::sqlite::SqliteStmt;

// The SET list of an ORM Update(rec, 'a,b,...'): mORMot turns the field list into a set of field bits and
// writes the JSON of the record in TABLE order (TRestOrm.Update, orm.rest.pas:1936-1984, GetJsonValue),
// whatever the order in which TCSVFieldsBuilder collected the names. `add` is therefore called in table
// order.
class SetList {
public:
    void add(const char* column, OrmArg value) {
        if (!sql_.isEmpty()) {
            sql_.append(',');
        }
        sql_.append(column).append("=?");
        args_.push_back(std::move(value));
    }
    [[nodiscard]] bool isEmpty() const { return args_.empty(); }
    // "UPDATE <table> SET a=?,b=? WHERE RowID=?"; the row id is the last bound value.
    [[nodiscard]] QByteArray statement(const char* table) const {
        return QByteArray("UPDATE ") + table + " SET " + sql_ + " WHERE RowID=?";
    }
    [[nodiscard]] std::vector<OrmArg> args(qint64 rowId) const {
        std::vector<OrmArg> out = args_;
        out.emplace_back(rowId);
        return out;
    }

private:
    QByteArray sql_;
    std::vector<OrmArg> args_;
};

} // namespace

// :776-1010
bool MarkDbImplORMHelper::updateMarkSql(const OrmMarkRec& oldRec, OrmMarkRec& newRec) {
    bool result = false;

    checkId(oldRec.markId);

    newRec.markId = oldRec.markId;

    if (isReadOnly_) {
        if (oldRec.visible != newRec.visible) {
            // update view
            result = updateMarkView(newRec.markId, newRec.categoryId, newRec.visible, false);
        }
        return result;
    }

    // :815 calls IsSameGeometry on a nil interface when the new mark has no geometry: an access violation.
    // A reproduction (no D row): the facade wraps UpdateMark / UpdateMarkList in `except CatchException`
    // (u_MarkDbByImpl.pas:503-516, :518-534); the port throws at the same point, after `markId` was copied
    // and before any statement or cache write. Whether a caller can pass such a mark: not traced. A nil OLD
    // geometry is not an error: TGeometryLonLat*.IsSameGeometry(nil) answers False
    // (u_GeometryLonLat.pas:535-538).
    if (!newRec.geometry) {
        throw MarkSystemORMError("MarkSystemORM: Geometry is empty!");
    }

    const bool updatePic = oldRec.picName != newRec.picName;
    const bool updateGeo = !oldRec.geometry || !newRec.geometry->isSameGeometry(*oldRec.geometry);
    const bool updateName = oldRec.name != newRec.name;
    const bool updateDesc = oldRec.desc != newRec.desc;
    const bool updateCategory = oldRec.categoryId != newRec.categoryId;

    const bool updateAppearance = oldRec.color1 != newRec.color1 || oldRec.color2 != newRec.color2 ||
                                  oldRec.scale1 != newRec.scale1 || oldRec.scale2 != newRec.scale2;

    const bool updateIdIndex = updateAppearance || updatePic || updateCategory;

    geo::DoubleRect rect;
    QByteArray geometryPointsBlob;
    QByteArray geometryMetaBlob;
    if (updateGeo) {
        const geo::LonLatRect bounds = newRec.geometry->bounds();
        rect = geo::DoubleRect{bounds.left, bounds.top, bounds.right, bounds.bottom};
        geometryPointsBlob = geo::writeWkb(*newRec.geometry);
        geometryMetaBlob = geo::writeMetaJson(*newRec.geometry);
        calcGeometrySize(rect, newRec.geoLonSize, newRec.geoLatSize);
    }

    inTransaction(provider_, true, isReadOnly_, [&] {
        if (updateIdIndex) {
            if (!newRec.picName.isEmpty()) {
                newRec.picId = addMarkImage(newRec.picName);
            } else {
                newRec.picId = 0;
            }

            newRec.appearanceId =
                addMarkAppearance(newRec.color1, newRec.color2, newRec.scale1, newRec.scale2);
        }

        {
            SetList fields;
            // table order of TOrmMark: mCategory, mImage, mAppearance, mName, mDesc, mGeoType, mGeoCount,
            // mGeoLonSize, mGeoLatSize
            if (updateCategory) {
                fields.add("mCategory", newRec.categoryId);
            }
            if (updatePic) {
                fields.add("mImage", newRec.picId);
            }
            if (updateAppearance) {
                fields.add("mAppearance", newRec.appearanceId);
            }
            if (updateName) {
                fields.add("mName", newRec.name);
            }
            if (updateDesc) {
                fields.add("mDesc", newRec.desc);
            }
            if (updateGeo) {
                fields.add("mGeoType", qint64{static_cast<quint8>(newRec.geoType)});
                fields.add("mGeoCount", qint64{static_cast<quint32>(newRec.geoCount)});
                fields.add("mGeoLonSize", qint64{newRec.geoLonSize});
                fields.add("mGeoLatSize", qint64{newRec.geoLatSize});
            }

            if (!fields.isEmpty()) {
                // update mark: S-2E
                checkUpdateResult(runWrite(provider_, fields.statement("Mark"), fields.args(newRec.markId)));
                // update cache
                if (updateName || updateDesc || updateGeo) {
                    cache_.markCache->addOrUpdate(newRec);
                }
                if (updateIdIndex) {
                    cache_.markIdIndex->addOrUpdate(newRec);
                    if (updateCategory) {
                        cache_.markIdByCategoryIndex->remove(oldRec.categoryId, newRec.markId);
                        cache_.markIdByCategoryIndex->add(newRec.categoryId, newRec.markId);
                    }
                }
                result = true;
            }

            if (updateGeo) {
                // update geometry blob: S-23 (writeWkb never returns an empty blob: the NULL form is not
                // reached)
                checkUpdateResult(runWrite(provider_, "UPDATE Mark SET mGeoWKB=? WHERE RowID=?",
                                           {geometryPointsBlob, newRec.markId}));
                // update cache
                cache_.markGeometryCache->addOrUpdate(
                    newRec.markId, static_cast<qint32>(geometryPointsBlob.size() + geometryMetaBlob.size()),
                    newRec.geometry);
                result = true;
            }
        }

        {
            SetList fields;
            if (updateName) {
                fields.add("mName", newRec.name.toLower());
            }
            if (updateDesc) {
                fields.add("mDesc", newRec.desc.toLower());
            }
            if (!fields.isEmpty()) {
                // update name / desc (fts index): S-2F. Legacy detects a failure here (Update returns the
                // result of MainEngineUpdate, orm.sqlite3.pas:2064-2103), as the port does.
                checkUpdateResult(
                    runWrite(provider_, fields.statement("MarkFTS"), fields.args(newRec.markId)));
            }
        }

        if (updateGeo) {
            // update rect (rtree index): S-26, all four fields in table order
            checkUpdateResult(runWrite(provider_,
                                       "UPDATE MarkRTree SET mLeft=" + doubleLiteral(rect.left) +
                                           ",mRight=" + doubleLiteral(rect.right) +
                                           ",mBottom=" + doubleLiteral(rect.bottom) +
                                           ",mTop=" + doubleLiteral(rect.top) + " WHERE RowID=?",
                                       {newRec.markId}));
        }

        if (oldRec.visible != newRec.visible || updateCategory) {
            // update view
            if (updateMarkView(newRec.markId, newRec.categoryId, newRec.visible, false)) {
                result = true;
            }
        }

        // update geometry meta blob
        if (updateGeo) {
            // S-33
            const qint64 metaId =
                selectRowId(provider_, "SELECT RowID FROM MarkMeta WHERE mMark=? LIMIT 1", {newRec.markId});
            if (geometryMetaBlob.isEmpty()) {
                if (metaId > 0) {
                    checkDeleteResult(runWrite(provider_, "DELETE FROM MarkMeta WHERE RowID=?", {metaId}));
                    result = true;
                }
            } else {
                qint64 rowId = metaId;
                if (rowId == 0) {
                    // add new: S-2D
                    rowId = insertRow(provider_, "INSERT INTO MarkMeta (mMark) VALUES (?)", {newRec.markId});
                    checkId(rowId);
                }
                // S-27 (geometryMetaBlob is not empty here: the NULL form is not reached)
                checkUpdateResult(runWrite(provider_, "UPDATE MarkMeta SET mMeta=? WHERE RowID=?",
                                           {geometryMetaBlob, rowId}));
                result = true;
            }
        }
    });

    return result;
}

// :1176-1264. Assert(AMarkID > 0) (:1192) is off in the shipped build.
bool MarkDbImplORMHelper::updateMarkView(qint64 markId, qint64 categoryId, bool visible,
                                         bool useTransaction) {
    bool result = false;

    inTransaction(provider_, useTransaction, isReadOnly_, [&] {
        // the fields of the TOrmMarkView instance
        qint64 viewId = 0;
        qint64 viewCategory = 0;
        bool viewVisible = false;
        bool updateCache = true;
        bool find = false;

        if (const OrmMarkViewRow* item = cache_.markViewCache->find(markId)) {
            find = true;
            viewId = item->viewId;
            viewCategory = item->categoryId;
            viewVisible = item->visible;
            updateCache = (item->categoryId != categoryId) || (item->visible != visible);
        } else if (!(cache_.markViewCache->isPrepared() ||
                     cache_.markViewCache->isCategoryPrepared(categoryId))) {
            // S-36
            runStatement(
                provider_,
                "SELECT RowID,mvCategory,mvVisible FROM MarkView WHERE mvMark=? AND mvUser=? LIMIT 1",
                {markId, userId_}, [&](const SqliteStmt& row) {
                    find = true;
                    viewId = row.columnInt64(0);
                    viewCategory = row.columnInt64(1);
                    viewVisible = row.columnInt64(2) != 0;
                });
        }
        if (!isReadOnly_) {
            if (find && viewId > 0) {
                // table order of TOrmMarkView: mvUser, mvMark, mvCategory, mvVisible
                bool setVisible = false;
                bool setCategory = false;
                if (viewVisible != visible) {
                    setVisible = true;
                    viewVisible = visible;
                }
                checkId(viewCategory);
                if (categoryId > 0 && viewCategory != categoryId) {
                    setCategory = true;
                    viewCategory = categoryId;
                }
                SetList fields;
                if (setCategory) {
                    fields.add("mvCategory", viewCategory);
                }
                if (setVisible) {
                    fields.add("mvVisible", qint64{viewVisible ? 1 : 0});
                }
                if (!fields.isEmpty()) {
                    // update db: S-37
                    result = runWrite(provider_, fields.statement("MarkView"), fields.args(viewId));
                    checkUpdateResult(result);
                }
            } else if (!visible) {
                // add to db: S-2C
                viewId = insertRow(
                    provider_, "INSERT INTO MarkView (mvUser,mvMark,mvCategory,mvVisible) VALUES (?,?,?,?)",
                    {userId_, markId, categoryId, qint64{visible ? 1 : 0}});
                checkId(viewId);
                result = true;
            } else {
                // AVisible = True: marks are visible by default, so no entry is added to the db
            }
        } else {
            result = true;
        }
        if (updateCache) {
            // update cache; the view id can be zero
            cache_.markViewCache->addOrUpdate(markId, viewId, categoryId, visible);
        }
    });

    return result;
}

// :1266-1347
bool MarkDbImplORMHelper::setMarksInCategoryVisibleSql(qint64 categoryId, bool visible) {
    bool result = false;
    checkId(categoryId);

    if (!isReadOnly_) {
        // UPDATE: S-38
        cache_.markViewCache->reset();

        result = runWrite(provider_,
                          QByteArray("UPDATE MarkView SET mvVisible=") + (visible ? "1" : "0") +
                              " WHERE mvCategory=? AND mvUser=?",
                          {categoryId, userId_});
        checkUpdateResult(result);
    }

    if (!isReadOnly_ && !visible) {
        // INSERT: S-39
        result = runWrite(provider_,
                          "INSERT OR IGNORE INTO MarkView (mvUser,mvMark,mvCategory,mvVisible) SELECT " +
                              QByteArray::number(userId_) + ",RowID," + QByteArray::number(categoryId) + "," +
                              (visible ? "1" : "0") + " FROM Mark WHERE mCategory=?",
                          {categoryId});
        checkExecuteResult(result);
    } else if (isReadOnly_) {
        // :1321-1344. The view cache of a read-only database is changed in memory only. _TryFastUpdate
        // (:1271-1287) with FIsReadOnly: a cached row with the wanted state is left alone, a cached row with
        // another state gets the wanted one (its own view id), a missing row is added with the fake id 0.
        const auto tryFastUpdate = [&](qint64 markId) {
            bool fast = false;
            if (const OrmMarkViewRow* view = cache_.markViewCache->find(markId)) {
                if (view->visible == visible) {
                    fast = true; // nothing to change in db
                } else if (isReadOnly_) {
                    // the view id is read before the call that may move rows (legacy: an argument)
                    const qint64 viewId = view->viewId;
                    cache_.markViewCache->addOrUpdate(markId, viewId, categoryId, visible);
                    fast = true;
                }
            } else if (isReadOnly_) {
                cache_.markViewCache->addOrUpdate(markId, 0 /* use fake id */, categoryId, visible);
                fast = true;
            }
            return fast;
        };

        if (fillPrepareMarkIdIndex(categoryId) > 0) {
            fillPrepareMarkViewCache(categoryId);
            const std::vector<qint64>* ids = nullptr;
            int count = 0;
            if (cache_.markIdByCategoryIndex->find(categoryId, ids, count)) {
                // `ids` points into the live by-category index. The loop body touches the view cache only
                // (tryFastUpdate; updateMarkView with no transaction: S-36 and the view cache), never the
                // by-category index, so the pointer stays valid; no copy is needed. In this branch
                // (read-only) tryFastUpdate always answers true, so updateMarkView is kept for the shape
                // of the legacy loop and is not reached.
                inTransaction(provider_, true, isReadOnly_, [&] {
                    for (int i = 0; i < count; ++i) {
                        const qint64 markId = (*ids)[static_cast<std::size_t>(i)];
                        if (!tryFastUpdate(markId)) {
                            updateMarkView(markId, categoryId, visible, false);
                        }
                    }
                    result = true;
                });
            }
            // else Assert(False) (:1343): off in the shipped build; not reachable right after the fill
        }
    }
    return result;
}

} // namespace cg::marks
