// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// T-0022B3: SqliteRegionHandler's read side. Ports:
// - GetTileInfo / CallbackSelectTileInfo (u_TileStorageSQLiteHandler.pas:949-1039, :526-671)
// - GetListOfTileVersions / GetListOfVersions / CallbackSelectVersions / TSelectTileVersions
//   (u_TileStorageSQLiteHandler.pas:773-816, :817-853, :715-772, :230-251)
// docs/specs/storage.md §2.15.1 (LOGICALSORTING ordering), §2.16.3 "The statements".
// FEAT-STO-018, FEAT-STO-032.

#include "cg/storage/sqlite/sqlite_region_handler.h"
#include "cg/storage/sqlite/sqlite_region_sql.h"

#include <QtGlobal>

#include <vector>

namespace cg::storage::sqlite {

namespace {

// The version string a `v` column value represents, ports the shared half of
// CallbackSelectTileInfo (:557-606) and CallbackSelectVersions (:738-759): both switch on
// SQLite's per-row dynamic type (NOT the column's declared type -- see sqlite_db.h's isInteger())
// and reduce it to the same three cases. NULL and an INTEGER value of 0 both collapse to the
// empty string (cDefaultVersionAsStrValue); a non-zero INTEGER becomes its decimal text; anything
// else (TEXT/FLOAT/BLOB) is read as-is (ColumnAsString).
QString versionStringFromColumn(const SqliteStmt& stmt, int index) {
    if (stmt.isNull(index)) {
        return QString();
    }
    if (stmt.isInteger(index)) {
        const qint64 value = stmt.columnInt64(index);
        return value == 0 ? QString() : QString::number(value);
    }
    return stmt.columnText(index);
}

// Ports SysUtils's 2-arg SameText (u_TileStorageSQLiteHandler.pas:597), used to decide whether the
// row's TEXT/FLOAT/BLOB version text is the same version the caller already holds a pointer to.
// That overload folds only ASCII 'A'-'Z' -- unlike Qt::CaseInsensitive (QString::compare), which
// folds full Unicode -- so a non-ASCII letter differing only in case must NOT compare equal here
// (review finding 2, T-0022B3 rework).
bool asciiSameText(const QString& a, const QString& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (int i = 0; i < a.size(); ++i) {
        ushort ca = a[i].unicode();
        ushort cb = b[i].unicode();
        if (ca >= u'a' && ca <= u'z') {
            ca -= (u'a' - u'A');
        }
        if (cb >= u'a' && cb <= u'z') {
            cb -= (u'a' - u'A');
        }
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

// Ports CallbackSelectVersions (:715-772) plus TSelectTileVersions.GetMapVersionInfoStatic
// (:243-250) for both `listTileVersions` and `listVersions`: steps `stmt` to completion (or until
// `stop` fires), turning each row's `v` value into a version via `schema.versionInfo()` --
// dropping the default/empty version unless `storeEmptyVersion` is set (GetListOfTileVersions
// passes False, GetListOfVersions passes True, :798/:835). Ports the all-or-nothing result: a
// cancellation (`stop`) OR a query failure discards every row already collected and yields an
// empty list (nullptr), exactly like GetMapVersionInfoStatic's unconditional `if not Cancelled`
// and GetListOfTileVersions/GetListOfVersions's try/except (which never reaches the
// `Result := ...GetMapVersionInfoStatic` line on an exception) -- see the header's corrected
// comment on `listTileVersions`. A query failure additionally logs via `schema` (c_Log_GetVer),
// matching legacy's LogError call; a cancellation does not log (legacy's cancellation path is not
// an exception).
core::Result<MapVersionList> collectVersions(SqliteRegionSchema& schema, SqliteStmt& stmt,
                                             bool storeEmptyVersion, const std::stop_token& stop) {
    std::vector<MapVersionInfoPtr> versions;
    for (;;) {
        if (stop.stop_requested()) {
            return MapVersionList{};
        }
        auto step = stmt.step();
        if (!step) {
            schema.logError(SqliteRegionSchema::LogKind::GetVer, step.error().what());
            return MapVersionList{};
        }
        if (*step == SqliteStmt::StepResult::Done) {
            break;
        }
        const QString versionStr = versionStringFromColumn(stmt, 0);
        if (storeEmptyVersion || !versionStr.isEmpty()) {
            versions.push_back(schema.versionInfo(versionStr));
        }
    }
    if (versions.empty()) {
        return MapVersionList{};
    }
    return std::make_shared<const std::vector<MapVersionInfoPtr>>(std::move(versions));
}

} // namespace

core::Result<TileInfo> SqliteRegionHandler::tileInfo(geo::PointI xy, const MapVersionInfoPtr& version,
                                                     bool useOtherVersions, LoadMode mode) {
    // Callers must not query an unopened handler (mirrors `db()`'s "nullptr when !isOpened()" --
    // legacy never reaches GetTileInfo through a handler whose Create failed either, since the
    // owning helper/map checks Opened first; T-0022B5's scope).
    Q_ASSERT(isOpened());

    const bool withBody = (mode == LoadMode::WithData); // TileInfoModeToSQLiteMode's gtiiBody,
                                                        // folded in -- see the .cpp file comment.
    const bool hasVersion = columnInfo_.hasVersion;
    const VersionColMode verMode = columnInfo_.versionMode;

    // ParseSQLiteDBVersion (u_TileStorageSQLiteFunc.pas:95-129), the parts CallbackSelectTileInfo
    // needs directly (the WHERE-clause text itself is built by versionFieldIsEqual/
    // versionFieldIsEqualOrMax below, which redo the same parse internally).
    QByteArray requestedRaw;
    bool requestedIsInt = true;
    qint64 requestedAsInt = 0;
    bool requestedIsSet = false;
    if (hasVersion && useVersionFieldInDb_ && version && !version->storeString().isEmpty()) {
        requestedRaw = version->storeString().toUtf8();
        requestedIsSet = true;
        if (!localTryStrToInt64(requestedRaw, &requestedAsInt)) {
            requestedIsInt = false;
        }
    }

    QByteArray sql = "SELECT s,d";
    QByteArray where = "WHERE x=? AND y=?";
    QByteArray order;
    bool bindSubselectXy = false;

    if (hasVersion) {
        sql += ",v";
        if (!useOtherVersions) {
            // use ONLY the given (or default) version
            where += " AND " + versionFieldIsEqual(verMode, requestedRaw);
        } else if (requestedIsSet) {
            // use the given version, or the newest
            where += " AND " + versionFieldIsEqualOrMax(verMode, requestedRaw);
            order = " ORDER BY " + getOrderByVersionAsc(verMode);
            bindSubselectXy = true;
        } else {
            // no version requested -- just the newest
            order = " ORDER BY " + getOrderByVersionDesc(verMode);
        }
    }
    if (columnInfo_.hasContentType) {
        sql += ",c";
    }
    if (withBody) {
        sql += ",b";
    }
    sql += " FROM t " + where + order + " LIMIT 1";

    // DEVIATION D-160 (T-0276, L-75): a prepare/bind/step failure here is what legacy's GetTileInfo catches
    // around OpenSQL (u_TileStorageSQLiteHandler.pas:1027-1037: LogError(c_Log_Select), Result stays False),
    // which GetTileInfoInternal (u_TileStorageSQLite.pas:280-297) turns into a cached NotExists, so a locked
    // or damaged file looked empty. Here the failure is logged the same way and then returned as an error
    // (never cached, see SqliteRegionTileStorage::tileInfoInternal); "no row" stays NotExists.
    const auto selectFailed = [this](const QString& message) -> core::Result<TileInfo> {
        schema_->logError(SqliteRegionSchema::LogKind::Select, message);
        return core::fail(core::ErrorCode::IoError, message);
    };
    auto stmt = db_->prepare(sql);
    if (!stmt) {
        return selectFailed(stmt.error().what());
    }
    if (auto b = stmt->bindInt(1, xy.x()); !b) {
        return selectFailed(b.error().what());
    }
    if (auto b = stmt->bindInt(2, xy.y()); !b) {
        return selectFailed(b.error().what());
    }
    if (bindSubselectXy) {
        if (auto b = stmt->bindInt(3, xy.x()); !b) {
            return selectFailed(b.error().what());
        }
        if (auto b = stmt->bindInt(4, xy.y()); !b) {
            return selectFailed(b.error().what());
        }
    }

    auto step = stmt->step();
    if (!step) {
        return selectFailed(step.error().what());
    }
    if (*step == SqliteStmt::StepResult::Done) {
        TileInfo notExists;
        notExists.type = TileInfoType::NotExists;
        return notExists;
    }

    const int origSize = stmt->columnInt(0);
    const TileDate loadDate = fromLegacyUnixSeconds(stmt->columnInt64(1));

    int col = 2;
    MapVersionInfoPtr resultVersion;
    if (hasVersion) {
        // CallbackSelectTileInfo :557-606: resolve the tile's actual version, reusing the
        // originally-requested `version` pointer when the row's value matches it (the "keep
        // source version" / "given version was the same" branches) instead of always interning a
        // fresh one -- see the .cpp comment above versionStringFromColumn for why the three
        // dynamic-type cases share one "the string this row represents" helper even though their
        // "keep vs replace" test differs.
        const QString colStr = versionStringFromColumn(*stmt, col);
        bool keepOriginal;
        if (stmt->isNull(col)) {
            keepOriginal = requestedIsInt && requestedAsInt == 0;
        } else if (stmt->isInteger(col)) {
            keepOriginal = requestedIsInt && requestedAsInt == stmt->columnInt64(col);
        } else {
            keepOriginal = version && asciiSameText(version->storeString(), colStr);
        }
        resultVersion = keepOriginal ? version : schema_->versionInfo(colStr);
        ++col;
    }

    if (origSize <= 0) {
        // :608-616 -- TNE, decided from `s` alone; content type and body are never read.
        TileInfo tne;
        tne.type = TileInfoType::TneExists;
        tne.loadDate = loadDate;
        tne.version = resultVersion;
        return tne;
    }

    QByteArray contentTypeDbValue;
    if (columnInfo_.hasContentType) {
        contentTypeDbValue = stmt->columnUtf8(col);
        ++col;
    }
    const QByteArray contentType = schema_->contentTypeFromDb(contentTypeDbValue);

    if (withBody) {
        QByteArray data = stmt->columnBlob(col);
        if (data.isEmpty()) {
            // :636-643 -- "TNE ?!": `s` claimed a body but the blob is actually empty. Reproduced
            // as legacy has it: still a TNE, content type discarded (TTileInfoBasicTNE carries no
            // content type either).
            TileInfo tne;
            tne.type = TileInfoType::TneExists;
            tne.loadDate = loadDate;
            tne.version = resultVersion;
            return tne;
        }
        TileInfo exists;
        exists.type = TileInfoType::Exists;
        exists.loadDate = loadDate;
        exists.size = static_cast<quint32>(data.size()); // :210-213 GetSize -> the tile data's size
        exists.data = std::move(data);
        exists.version = resultVersion;
        exists.contentType = contentType;
        return exists;
    }

    TileInfo exists;
    exists.type = TileInfoType::Exists;
    exists.loadDate = loadDate;
    exists.size = static_cast<quint32>(origSize); // :176-179 GetSize -> the `s` column
    exists.version = resultVersion;
    exists.contentType = contentType;
    return exists;
}

core::Result<MapVersionList> SqliteRegionHandler::listTileVersions(geo::PointI xy,
                                                                   const std::stop_token& stop) {
    Q_ASSERT(isOpened());
    if (!useVersionFieldInDb_ || !columnInfo_.hasVersion) {
        return MapVersionList{};
    }

    const QByteArray sql =
        "SELECT v FROM t WHERE x=? AND y=? ORDER BY " + getOrderByVersionDesc(columnInfo_.versionMode);
    auto stmt = db_->prepare(sql);
    if (!stmt) {
        schema_->logError(SqliteRegionSchema::LogKind::GetVer, stmt.error().what());
        return MapVersionList{};
    }
    if (auto b = stmt->bindInt(1, xy.x()); !b) {
        schema_->logError(SqliteRegionSchema::LogKind::GetVer, b.error().what());
        return MapVersionList{};
    }
    if (auto b = stmt->bindInt(2, xy.y()); !b) {
        schema_->logError(SqliteRegionSchema::LogKind::GetVer, b.error().what());
        return MapVersionList{};
    }

    return collectVersions(*schema_, *stmt, /*storeEmptyVersion=*/false, stop);
}

core::Result<MapVersionList> SqliteRegionHandler::listVersions() {
    Q_ASSERT(isOpened());
    if (!useVersionFieldInDb_ || !columnInfo_.hasVersion) {
        return MapVersionList{};
    }

    auto stmt = db_->prepare(QByteArrayView("SELECT DISTINCT v FROM t"));
    if (!stmt) {
        schema_->logError(SqliteRegionSchema::LogKind::GetVer, stmt.error().what());
        return MapVersionList{};
    }

    // GetListOfVersions (:817-853) takes an AOper and polls it exactly like GetListOfTileVersions
    // -- but the FINAL handler header (T-0022B2) declares this method with no std::stop_token, so
    // there is no cancellation source to poll here. That costs nothing observable: legacy's only
    // caller (u_TileStorageSQLiteFetcher.pas:433) always passes AOper = nil, and
    // TNotifierOperationRec.IsOperationCancelled is `Assigned(@Self) and ...`
    // (t_NotifierOperationRec.pas:50-54) -- legacy itself never cancels this scan either (review
    // finding 3 / ruling R4, T-0022B3 rework). A default-constructed std::stop_token is never
    // stoppable (stop_possible() == false), so collectVersions() simply runs to completion.
    return collectVersions(*schema_, *stmt, /*storeEmptyVersion=*/true, std::stop_token{});
}

} // namespace cg::storage::sqlite
