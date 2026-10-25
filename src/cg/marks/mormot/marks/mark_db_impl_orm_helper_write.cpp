// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/MarksDB/mORMot/Marks/u_MarkDbImplORMHelper.pas :230-774 (with the decoders :267-299 added by
// T-0100) (docs/specs/marks.md §2.9.8 W-1, W-3, W-4; S-20 ... S-2D, S-40, S-42).
#include "cg/geo/wkb.h"
#include "cg/marks/mormot/mark_system_orm_statement.h"
#include "cg/marks/mormot/mark_system_orm_tools.h"
#include "cg/marks/mormot/marks/mark_db_impl_orm_helper.h"

#include <QByteArray>

#include <algorithm>
#include <functional>
#include <mutex>
#include <variant>

namespace cg::marks {

namespace {

using storage::sqlite::SqliteStmt;

constexpr int kMaxSqlParams = 500; // MAX_SQLPARAMS (db/mormot.db.core.pas:79)

} // namespace

// :230-259 (the destructor, :261-265, only frees the cache). The four geometry reader / writer interfaces
// of the legacy constructor are the free functions of cg/geo/wkb.h; the reader needs the factory.
MarkDbImplORMHelper::MarkDbImplORMHelper(bool isReadOnly, quint32 cacheSizeMb,
                                         const geo::GeometryLonLatFactory& geometryFactory,
                                         IMarkSystemImplORMClientProvider& provider)
    : userId_(provider.userId()), provider_(provider), geometryFactory_(geometryFactory),
      isReadOnly_(isReadOnly) {
    cache_.init(static_cast<qint64>(cacheSizeMb) * 1024 * 1024);
}

// :267-282
geo::GeometryLonLatPtr MarkDbImplORMHelper::geometryFromBlob(const QByteArray& blob,
                                                             const geo::PointsMeta* meta) const {
    auto parsed = geo::parseWkb(blob, geometryFactory_, meta);
    if (!parsed) {
        throw MarkSystemORMError("MarkSystemORM: geometry blob: " + parsed.error().what().toStdString());
    }
    return *parsed;
}

// :284-299. A blob that does not parse is "no meta", as legacy: the Parse of u_GeometryMetaFromStreamJson.pas
// leaves its result nil on invalid JSON (:97-100) and on a missing geometry array (:115-118) (assertions are
// off in the shipped build).
geo::PointsMeta MarkDbImplORMHelper::geometryMetaFromBlob(const QByteArray& blob) const {
    if (blob.isEmpty()) {
        return {};
    }
    auto parsed = geo::parseMetaJson(blob);
    if (!parsed) {
        return {};
    }
    return std::move(*parsed);
}

// :333-361
qint64 MarkDbImplORMHelper::addMarkImage(const QString& picName) {
    if (const OrmMarkImageRow* item = cache_.markImageCache->find(picName)) {
        // found in cache
        return item->id;
    }
    // S-20: the look-up is SYSTEMNOCASE (the column's collation)
    qint64 id = selectRowId(provider_, "SELECT RowID FROM MarkImage WHERE miName=? LIMIT 1", {picName});
    if (id == 0) {
        // add to db: S-21
        id = insertRow(provider_, "INSERT INTO MarkImage (miName) VALUES (?)", {picName});
        checkId(id);
    }
    // add to cache
    cache_.markImageCache->addOrIgnore(id, picName);
    return id;
}

// :363-389
void MarkDbImplORMHelper::readMarkImage(OrmMarkRec& rec) {
    if (rec.geoType != OrmGeoType::Point) {
        return;
    }
    if (rec.picId > 0) {
        if (const OrmMarkImageRow* item = cache_.markImageCache->find(rec.picId)) {
            // found in cache
            rec.picName = item->name;
        } else {
            // read from db: S-22 (an id of the row that is not there: CheckID throws)
            qint64 id = 0;
            QString name;
            runStatement(provider_, "SELECT RowID,miName FROM MarkImage WHERE RowID=?", {rec.picId},
                         [&](const SqliteStmt& row) {
                             id = row.columnInt64(0);
                             name = row.columnText(1);
                         });
            checkId(id);
            rec.picName = name;
            // add to cache
            cache_.markImageCache->addOrIgnore(rec);
        }
    } else {
        rec.picName.clear();
    }
}

// :391-426
qint64 MarkDbImplORMHelper::addMarkAppearance(quint32 color1, quint32 color2, qint32 scale1, qint32 scale2) {
    if (const OrmMarkAppearanceRow* item = cache_.markAppearanceCache->find(color1, color2, scale1, scale2)) {
        // found in cache
        return item->id;
    }
    // S-28
    qint64 id = selectRowId(provider_,
                            "SELECT RowID FROM MarkAppearance WHERE maColor1=? AND maColor2=? AND maScale1=? "
                            "AND maScale2=? LIMIT 1",
                            {qint64{color1}, qint64{color2}, qint64{scale1}, qint64{scale2}});
    if (id == 0) {
        // add to db: S-29
        id = insertRow(provider_,
                       "INSERT INTO MarkAppearance (maColor1,maColor2,maScale1,maScale2) VALUES (?,?,?,?)",
                       {qint64{color1}, qint64{color2}, qint64{scale1}, qint64{scale2}});
        checkId(id);
    }
    // add to cache
    cache_.markAppearanceCache->addOrIgnore(id, color1, color2, scale1, scale2);
    return id;
}

// :428-454
void MarkDbImplORMHelper::readMarkAppearance(OrmMarkRec& rec) {
    if (const OrmMarkAppearanceRow* item = cache_.markAppearanceCache->find(rec.appearanceId)) {
        // found in cache
        rec.color1 = item->color1;
        rec.color2 = item->color2;
        rec.scale1 = item->scale1;
        rec.scale2 = item->scale2;
    } else {
        // read from db: S-2A
        qint64 id = 0;
        OrmMarkAppearanceRow row;
        runStatement(provider_,
                     "SELECT RowID,maColor1,maColor2,maScale1,maScale2 FROM MarkAppearance WHERE RowID=?",
                     {rec.appearanceId}, [&](const SqliteStmt& r) {
                         id = r.columnInt64(0);
                         row.color1 = static_cast<quint32>(r.columnInt64(1));
                         row.color2 = static_cast<quint32>(r.columnInt64(2));
                         row.scale1 = r.columnInt(3);
                         row.scale2 = r.columnInt(4);
                     });
        checkId(id);
        rec.color1 = row.color1;
        rec.color2 = row.color2;
        rec.scale1 = row.scale1;
        rec.scale2 = row.scale2;
        // add to cache
        cache_.markAppearanceCache->addOrIgnore(rec);
    }
}

// :456-519
bool MarkDbImplORMHelper::deleteMarkSql(qint64 markId, bool useTransactions) {
    if (isReadOnly_) {
        return false;
    }
    if (markId <= 0) {
        return false; // the Assert(False) at :471 is off in the shipped build
    }

    // delete from cache
    cache_.markCache->remove(markId);
    cache_.markGeometryCache->remove(markId);
    cache_.markViewCache->remove(markId);
    if (OrmMarkIdIndexRec* index = cache_.markIdIndex->find(markId)) {
        // L-212: legacy keeps the pointer `VIndex` and reads
        // VIndex.CategoryId AFTER FMarkIdIndex.Delete (:480-481). TDynArray.Delete moves the following rows
        // down and zero-fills the last one, so the pointer then names the row that followed the deleted one
        // (category 0 when it was the last row). The by-category index is therefore cleaned for the category
        // of the NEXT mark, and the deleted id stays in its own category's array.
        auto& rows = cache_.markIdIndex->rows();
        const auto position = static_cast<std::size_t>(index - rows.data());
        const std::size_t next = position + 1;
        const qint64 categoryAfterDelete =
            next < static_cast<std::size_t>(cache_.markIdIndex->count()) ? rows[next].categoryId : 0;
        cache_.markIdIndex->remove(markId);
        cache_.markIdByCategoryIndex->remove(categoryAfterDelete, markId);
    }

    // delete from db
    inTransaction(provider_, useTransactions, isReadOnly_, [&] {
        // delete view for all users if exists: S-40 (1), the result is ignored
        runWrite(provider_, "DELETE FROM MarkView WHERE mvMark=?", {markId});

        // delete rect: S-40 (2)
        checkDeleteResult(runWrite(provider_, "DELETE FROM MarkRTree WHERE RowID=?", {markId}));

        // delete name and desc: S-40 (3)
        checkDeleteResult(runWrite(provider_, "DELETE FROM MarkFTS WHERE RowID=?", {markId}));

        // delete meta if exists: S-40 (4), the result is ignored
        runWrite(provider_, "DELETE FROM MarkMeta WHERE mMark=?", {markId});

        // delete mark: S-40 (5)
        checkDeleteResult(runWrite(provider_, "DELETE FROM Mark WHERE RowID=?", {markId}));

        // pic name and appearance are never deleted...
    });

    return true;
}

// :521-616 (the MongoDB branch :583-610 and the non-SQLite branches are not ported)
bool MarkDbImplORMHelper::deleteMarkSql(const std::vector<qint64>& markIds) {
    if (isReadOnly_) {
        return false;
    }
    if (markIds.empty()) {
        return true;
    }

    // delete from cache
    cache_.markCache->reset();
    cache_.markGeometryCache->reset();
    cache_.markViewCache->reset();
    cache_.markIdIndex->reset();
    cache_.markIdByCategoryIndex->reset();

    // delete from db: Start is outside the guard, the commit is the last statement of the guard (:553-615)
    inTransaction(provider_, true, isReadOnly_, [&] {
        const std::size_t length = markIds.size();
        std::size_t i = 0;
        while (i < length) {
            const std::size_t count = std::min<std::size_t>(kMaxSqlParams, length - i);
            QByteArray ids = "(";
            for (std::size_t k = 0; k < count; ++k) {
                if (k > 0) {
                    ids.append(',');
                }
                ids.append(QByteArray::number(markIds[i + k]));
            }
            ids.append(");");
            i += count;

            // S-42, evaluated left to right and stopping at the first failure
            const bool result = runWrite(provider_, "DELETE FROM MarkRTree WHERE RowID IN " + ids) &&
                                runWrite(provider_, "DELETE FROM MarkFTS WHERE RowID IN " + ids) &&
                                runWrite(provider_, "DELETE FROM MarkMeta WHERE mMark IN " + ids) &&
                                runWrite(provider_, "DELETE FROM MarkView WHERE mvMark IN " + ids) &&
                                runWrite(provider_, "DELETE FROM Mark WHERE RowID IN " + ids);
            checkDeleteResult(result);
        }
    });
    return true;
}

// :618-774 (FEAT ids: none, ticket header `Features: —`)
bool MarkDbImplORMHelper::insertMarkSql(OrmMarkRec& rec, bool useTransactions) {
    if (isReadOnly_) {
        return false;
    }

    rec.markId = 0;

    // :643 reads FGeometry.Bounds on a nil interface: an access violation. Legacy callers catch it: the
    // facade wraps UpdateMark / UpdateMarkList in `except CatchException` (u_MarkDbByImpl.pas:503-516,
    // :518-534), and u_MarkDbImplORM.pas:454 dereferences AMark.Geometry.Bounds before the helper is reached.
    // A reproduction (no D row): the port throws before any statement or cache write.
    if (!rec.geometry) {
        throw MarkSystemORMError("MarkSystemORM: Geometry is empty!");
    }
    const geo::LonLatRect bounds = rec.geometry->bounds();
    const geo::DoubleRect rect{bounds.left, bounds.top, bounds.right, bounds.bottom};
    const QByteArray geometryPointsBlob = geo::writeWkb(*rec.geometry);
    const QByteArray geometryMetaBlob = geo::writeMetaJson(*rec.geometry);
    calcGeometrySize(rect, rec.geoLonSize, rec.geoLatSize);

    inTransaction(provider_, useTransactions, isReadOnly_, [&] {
        if (!rec.picName.isEmpty()) {
            rec.picId = addMarkImage(rec.picName);
        } else {
            rec.picId = 0;
        }

        rec.appearanceId = addMarkAppearance(rec.color1, rec.color2, rec.scale1, rec.scale2);

        // add mark to db: S-2B
        const qint64 markId = insertRow(
            provider_,
            "INSERT INTO Mark (mCategory,mImage,mAppearance,mName,mDesc,mGeoType,mGeoCount,mGeoLonSize,"
            "mGeoLatSize) VALUES (?,?,?,?,?,?,?,?,?)",
            {rec.categoryId, rec.picId, rec.appearanceId, rec.name, rec.desc,
             qint64{static_cast<quint8>(rec.geoType)}, qint64{static_cast<quint32>(rec.geoCount)},
             qint64{rec.geoLonSize}, qint64{rec.geoLatSize}});
        checkId(markId);
        rec.markId = markId;
        // add geometry points blob to db: S-23 (writeWkb never returns an empty blob, so the NULL form of
        // the statement is not reached)
        checkUpdateResult(
            runWrite(provider_, "UPDATE Mark SET mGeoWKB=? WHERE RowID=?", {geometryPointsBlob, rec.markId}));
        // add to cache
        cache_.markCache->addOrUpdate(rec);
        cache_.markGeometryCache->addOrUpdate(
            rec.markId, static_cast<qint32>(geometryPointsBlob.size() + geometryMetaBlob.size()),
            rec.geometry);
        cache_.markIdIndex->addOrUpdate(rec);
        cache_.markIdByCategoryIndex->add(rec.categoryId, rec.markId);

        // add name and desc to db (fts index): S-24. Tech Lead ruling 2026-10-13 (user may overturn): legacy
        // does not fail here. For a forced-id Add, MainEngineAdd seeds the result from the sent id
        // (mormot.orm.sqlite3.pas:1431) and never resets it when InternalExecute fails (:1468-1469, the
        // exception is swallowed :1805-1809), so CheckID passes and legacy commits the mark WITHOUT its FTS
        // row. The port keeps the throw (and the rollback); the same holds for S-25 below.
        checkId(insertRow(provider_, "INSERT INTO MarkFTS (RowID,mName,mDesc) VALUES (?,?,?)",
                          {rec.markId, rec.name.toLower(), rec.desc.toLower()}));

        // add rect to db (rtree index): S-25
        checkId(insertRow(provider_,
                          "INSERT INTO MarkRTree (RowID,mLeft,mRight,mBottom,mTop) VALUES (?," +
                              doubleLiteral(rect.left) + ',' + doubleLiteral(rect.right) + ',' +
                              doubleLiteral(rect.bottom) + ',' + doubleLiteral(rect.top) + ')',
                          {rec.markId}));

        if (!rec.visible) {
            // add view to db: S-2C
            const qint64 viewId = insertRow(
                provider_, "INSERT INTO MarkView (mvUser,mvMark,mvCategory,mvVisible) VALUES (?,?,?,?)",
                {userId_, rec.markId, rec.categoryId, qint64{rec.visible ? 1 : 0}});
            checkId(viewId);
            rec.viewId = viewId;
        }
        // add view to cache
        cache_.markViewCache->addOrUpdate(rec);

        // add geometry meta blob to db
        if (!geometryMetaBlob.isEmpty()) {
            // S-2D
            const qint64 metaId =
                insertRow(provider_, "INSERT INTO MarkMeta (mMark) VALUES (?)", {rec.markId});
            checkId(metaId);
            // S-27
            checkUpdateResult(
                runWrite(provider_, "UPDATE MarkMeta SET mMeta=? WHERE RowID=?", {geometryMetaBlob, metaId}));
        }
    });

    return rec.markId > 0;
}

} // namespace cg::marks
