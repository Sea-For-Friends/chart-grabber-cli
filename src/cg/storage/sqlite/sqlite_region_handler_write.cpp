// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports the write half of Src/TileStorage/SQLite/u_TileStorageSQLiteHandler.pas
// (TTileStorageSQLiteHandlerComplex): SaveTile (:1075-1229, TNE at :1196-1204), DeleteTile
// (:401-425) / GetSQL_DeleteTile (:854-881), GetTileRectInfo (:444-469) / GetSQL_TileRectInfo
// (:882-943) / CallbackSelectTileRectInfo (:672-714).
//
// docs/specs/storage.md §2.16.3 "The statements". See sqlite_region_handler.h for the class-level
// design notes (T-0022B2 ruling); this file only defines the three T-0022B4 member functions.
//
// Mechanism vs. legacy, resolved once here rather than per-statement below: legacy inlines every
// value (x, y, d, s, the CRC, the version) as SQL text and binds only the tile body (the one value
// that cannot safely become text). This port keeps that for every plain scalar (x, y, d, s, the
// version literal, the content type) -- there is no behavioural difference, since the STORED byte
// value is identical either way -- with two deliberate exceptions:
//   - `h` (CRC-32) IS bound, as a signed 32-bit int (L-35): binding the bit-reinterpreted value is
//     simpler and less error-prone than formatting legacy's `IntToStrA(Integer(Cardinal))` text,
//     and produces the exact same stored INTEGER.
//   - `b` (the tile body) is always bound; it is arbitrary binary data that cannot be a SQL literal.
// `c` (content type) is ALWAYS literal, never bound, in both the "NULL" and the mismatched-type
// case (L-47) -- docs/specs/storage.md §2.16.3 suggests binding it in the common case, but
// SqliteStmt has no bindNull(), and legacy's GetContentTypeToDB produces the literal text `NULL`
// (four characters, not a bound SQL NULL) for that case too, so literal-always is both simpler and
// a more faithful port than switching mechanism per case.

#include "cg/core/hash.h"
#include "cg/storage/sqlite/sqlite_region_handler.h"

namespace cg::storage::sqlite {

namespace {

// Ports ParseSQLiteDBVersion's gate (u_TileStorageSQLiteFunc.pas:99): `AUseVersionFieldInDB and
// Assigned(ARequestedVersionInfo)`. An assigned-but-empty StoreString is deliberately let through
// unfiltered -- requestedVersionToDb/versionFieldIsEqual already treat an empty QByteArrayView as
// "no version requested" (the same default-version fallback ParseSQLiteDBVersion applies).
QByteArray versionForColumn(bool useVersionFieldInDb, const MapVersionInfoPtr& version) {
    return (useVersionFieldInDb && version) ? version->storeString().toUtf8() : QByteArray();
}

} // namespace

// `loadDate` is by value because sqlite_region_handler.h's saveTile signature is FINAL (T-0022B2
// ruling); this .cpp definition must match it exactly.
core::Result<bool>
SqliteRegionHandler::saveTile(geo::PointI xy, const MapVersionInfoPtr& version,
                              TileDate loadDate, // NOLINT(performance-unnecessary-value-param)
                              QByteArrayView contentType, const QByteArray& data, SaveFlags flags) {
    if (!db_) {
        schema_->logError(SqliteRegionSchema::LogKind::Replace, QStringLiteral("not opened"));
        return false;
    }

    // VIsTne := (Size = 0) or (Body = nil) (:1117) -- an empty QByteArray covers both.
    const bool isTne = data.isEmpty();
    const quint32 crc =
        columnInfo_.hasCrc ? core::crc32(0, data.constData(), static_cast<std::size_t>(data.size())) : 0;

    QByteArray cols = "x,y,d";
    QByteArray vals = QByteArray::number(xy.x()) + "," + QByteArray::number(xy.y()) + "," +
                      QByteArray::number(toLegacyUnixSeconds(loadDate));

    if (columnInfo_.hasCrc) {
        cols += ",h";
        vals += ",?"; // bound below, signed int32 (L-35)
    }

    // SkipIfSameAsPrev's WHERE fragment (GetCheckOtherVersionsBy*SQLText); TNE never gets one --
    // legacy's `if not VIsTne and (stfSkipIfSameAsPrev in ...)` guard.
    QByteArray whereClause;
    if (columnInfo_.hasVersion) {
        cols += ",v";
        vals += "," + requestedVersionToDb(columnInfo_.versionMode,
                                           versionForColumn(useVersionFieldInDb_, version));

        if (!isTne && flags.testFlag(SaveFlag::SkipIfSameAsPrev)) {
            whereClause = " WHERE " + (columnInfo_.hasCrc ? getCheckOtherVersionsByCrc32SqlText()
                                                          : getCheckOtherVersionsBySizeSqlText());
        }
    }

    if (!isTne && columnInfo_.hasContentType) {
        cols += ",c";
        const QByteArray dbContentType = schema_->contentTypeToDb(contentType);
        vals += "," + (dbContentType.isEmpty() ? QByteArray("NULL") : dbContentType);
    }

    cols += ",s,b";
    vals += isTne ? QByteArray(",0,NULL") : ("," + QByteArray::number(data.size()) + ",?"); // b bound below

    const QByteArray verb =
        flags.testFlag(SaveFlag::KeepExisting) ? QByteArray("IGNORE") : QByteArray("REPLACE");
    QByteArray sql = "INSERT OR " + verb + " INTO t (" + cols + ") ";
    sql += whereClause.isEmpty() ? ("VALUES (" + vals + ")") : ("SELECT " + vals + whereClause);

    auto stmt = db_->prepare(sql);
    if (!stmt) {
        schema_->logError(SqliteRegionSchema::LogKind::Replace, stmt.error().what());
        return false;
    }

    const auto bindFailed = [this](const core::Result<void>& bound) {
        if (!bound) {
            schema_->logError(SqliteRegionSchema::LogKind::Replace, bound.error().what());
            return true;
        }
        return false;
    };

    int index = 1;
    if (columnInfo_.hasCrc && bindFailed(stmt->bindInt(index++, static_cast<qint32>(crc)))) {
        return false;
    }
    if (!isTne) {
        if (bindFailed(stmt->bindBlob(index++, data))) {
            return false;
        }
        if (!whereClause.isEmpty()) {
            // "?<>COALESCE((SELECT s|h FROM t WHERE x=? AND y=? AND s|h=?),-1)": compared-value,
            // x, y, compared-value again (sqlite_region_sql.h).
            const qint32 compared =
                columnInfo_.hasCrc ? static_cast<qint32>(crc) : static_cast<qint32>(data.size());
            const int comparedIndex = index++;
            const int xIndex = index++;
            const int yIndex = index++;
            const int comparedAgainIndex = index++;
            if (bindFailed(stmt->bindInt(comparedIndex, compared)) ||
                bindFailed(stmt->bindInt(xIndex, xy.x())) || bindFailed(stmt->bindInt(yIndex, xy.y())) ||
                bindFailed(stmt->bindInt(comparedAgainIndex, compared))) {
                return false;
            }
        }
    }

    auto stepResult = stmt->step();
    if (!stepResult) {
        schema_->logError(SqliteRegionSchema::LogKind::Replace, stepResult.error().what());
        return false;
    }
    return stmt->changes() > 0;
}

core::Result<bool> SqliteRegionHandler::deleteTile(geo::PointI xy, const MapVersionInfoPtr& version) {
    if (!db_) {
        schema_->logError(SqliteRegionSchema::LogKind::Delete, QStringLiteral("not opened"));
        return false;
    }

    QByteArray sql =
        "DELETE FROM t WHERE x=" + QByteArray::number(xy.x()) + " AND y=" + QByteArray::number(xy.y());
    if (columnInfo_.hasVersion) {
        sql += " AND " +
               versionFieldIsEqual(columnInfo_.versionMode, versionForColumn(useVersionFieldInDb_, version));
    }

    auto result = db_->exec(sql);
    if (!result) {
        schema_->logError(SqliteRegionSchema::LogKind::Delete, result.error().what());
        return false;
    }
    return *result > 0;
}

bool SqliteRegionHandler::tileRectInfo(geo::RectI destRect, const MapVersionInfoPtr& version,
                                       bool useOtherVersions, std::vector<TileRectInfoEntry>& items,
                                       const std::stop_token& stop) {
    if (!db_) {
        schema_->logError(SqliteRegionSchema::LogKind::GetMap, QStringLiteral("not opened"));
        return true; // !cancelled, matching legacy's swallow-and-return-true on any failure
    }

    QByteArray sql = "SELECT x,y,d,s FROM t WHERE x>=" + QByteArray::number(destRect.left) + " AND x<" +
                     QByteArray::number(destRect.right) + " AND y>=" + QByteArray::number(destRect.top) +
                     " AND y<" + QByteArray::number(destRect.bottom);

    if (columnInfo_.hasVersion) {
        if (!useOtherVersions) {
            sql += " AND " + versionFieldIsEqual(columnInfo_.versionMode,
                                                 versionForColumn(useVersionFieldInDb_, version));
        } else {
            // Legacy quirk, reproduced (not fixed -- see the T-0022B4 handoff, "Legacy defects
            // noticed"): this ORDER BY sorts the WHOLE result set by `v`, not per tile, and the
            // callback below overwrites `items[index]` unconditionally on every row. For a tile
            // with more than one version row inside `destRect`, the entry this function leaves
            // behind is therefore the row with the SMALLEST version among that tile's rows (it is
            // processed last in the DESC traversal), not the largest -- the opposite of what
            // "newest" and the DESC ordering suggest.
            sql += " ORDER BY " + getOrderByVersionDesc(columnInfo_.versionMode);
        }
    }

    auto stmt = db_->prepare(sql);
    if (!stmt) {
        schema_->logError(SqliteRegionSchema::LogKind::GetMap, stmt.error().what());
        return true;
    }

    bool cancelled = false;
    const int width = destRect.width();
    for (;;) {
        auto step = stmt->step();
        if (!step) {
            schema_->logError(SqliteRegionSchema::LogKind::GetMap, step.error().what());
            break; // swallowed, like legacy's OpenSQL exception handler; not a cancellation
        }
        if (*step == SqliteStmt::StepResult::Done) {
            break;
        }

        // Legacy checks cancellation only once a row has come back (CallbackSelectTileRectInfo,
        // u_TileStorageSQLiteHandler.pas:687-691), not before every step(); reproduced, so a
        // stop already requested with no rows (or no rows left) still returns "not cancelled".
        if (stop.stop_requested()) {
            cancelled = true;
            break;
        }

        const int x = stmt->columnInt(0);
        const int y = stmt->columnInt(1);
        // TTileRectInfoShort.TileInRectToIndex: row-major, already guaranteed in range by the
        // WHERE clause above matching `destRect` exactly (tile_rect_info.h).
        const auto index =
            static_cast<std::size_t>(static_cast<qint64>(x - destRect.left) +
                                     static_cast<qint64>(y - destRect.top) * static_cast<qint64>(width));

        TileRectInfoEntry& entry = items[index];
        entry.loadDate = fromLegacyUnixSeconds(stmt->columnInt64(2));
        entry.size = static_cast<quint32>(stmt->columnInt(3));
        entry.type = entry.size > 0 ? TileInfoType::Exists : TileInfoType::TneExists;
    }
    return !cancelled;
}

} // namespace cg::storage::sqlite
