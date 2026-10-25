// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLite/u_TileStorageSQLiteHolder.pas (:102-305), the `GetFileNameEvent`
// half of Src/TileStorage/SQLite/u_TileStorageSQLiteErrorLogger.pas that T-0022A left out (§2.16.4
// -- SqliteErrorLog itself, the fixed-filename half, landed with T-0022A), and the log-kind
// constants of Src/TileStorage/SQLite/c_TileStorageSQLite.pas.
//
// docs/specs/storage.md §2.16.1 "Schema and pragmas, with the three override files" and §2.16.4
// "Error logging". One instance per SqliteRegionTileStorage (i.e. per map/cache root, NOT per
// region file): it reads the three override files once, and every SqliteRegionHandler opened
// under that root shares it (ports ITileStorageSQLiteHolder, held by every
// TTileStorageSQLiteHandler via its constructor argument).
//
// Simplifications vs legacy (all verified, not guesses):
// - No IContentTypeManager / IContentTypeInfoBasic: this module's TileInfo::contentType is a plain
//   canonical-MIME QByteArray (tile_info.h), not a registry-validated pointer. GetContentTypeToDB
//   reduces to a same-text-as-main check (contentTypeToDb below): the port has no per-tile
//   IContentTypeInfoBasic to compare identity against. GetContentTypeInfo DOES have a registry to
//   go through -- core::ContentTypeRegistry (T-0022B2 review finding 4) -- so contentTypeFromDb
//   below looks the DB value up there, exactly as legacy's FContentTypeManager.GetInfo does.
// - `LogError`'s ARaiseError parameter is dropped: grep across every `.LogError(` call in
//   Src/TileStorage/SQLite/*.pas shows no call site ever passes True (u_TileStorageSQLiteHandler.
//   pas:394,396,422,465,810,847,1036,1070,1197,1224 all use the False default), so the
//   ETileStorageSQLiteError-raising branch is dead code in the ported scope.

#include "cg/storage/map_version.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>

#include <cstdint>
#include <memory>

namespace cg::storage::sqlite {

class SqliteErrorLog;

// FEAT-STO-018.
class SqliteRegionSchema final {
public:
    // Ports the six LogError kinds actually used (c_TileStorageSQLite.pas: c_Log_Init/Delete/
    // Select/Replace/GetVer/GetMap; c_Log_SetVer = 'w' is declared but never passed to LogError
    // anywhere in the SQLite storage domain, so it is not ported). Legacy's `else -> 'custom'`
    // branch is unreachable for the same reason and is not represented.
    enum class LogKind : std::uint8_t { Init, Delete, Select, Replace, GetVer, GetMap };

    // `storagePath` MUST already end with '/' (this module's convention, e.g.
    // file_system_tile_storage.cpp's storagePath_). `mainContentType` is the storage's own fixed
    // canonical MIME type. `logging` ports ALogging -- when true, LogError also appends to
    // `<storagePath>sqlite.log` (§2.16.4).
    SqliteRegionSchema(const QString& storagePath, QByteArray mainContentType,
                       MapVersionFactoryPtr mapVersionFactory, bool logging);
    ~SqliteRegionSchema();

    SqliteRegionSchema(const SqliteRegionSchema&) = delete;
    SqliteRegionSchema& operator=(const SqliteRegionSchema&) = delete;

    // The three override files' statements, one entry per line, split and read once at
    // construction (InternalLoadParams, u_TileStorageSQLiteHolder.pas:186-226). Each is run
    // verbatim, in order, by SqliteRegionHandler's constructor (§2.16.1's table): sessionSql() on
    // every connection right after open, newTableSql() only when `t` is missing, establishedSql()
    // on every connection after the structure check. newTableSql() is never empty (a built-in
    // default exists); establishedSql() legitimately is (no `e.sql` and no built-in default).
    [[nodiscard]] const QList<QByteArray>& sessionSql() const noexcept { return sessionSql_; }
    [[nodiscard]] const QList<QByteArray>& newTableSql() const noexcept { return newTableSql_; }
    [[nodiscard]] const QList<QByteArray>& establishedSql() const noexcept { return establishedSql_; }

    // Ports GetContentTypeToDB (u_TileStorageSQLiteHolder.pas:170-179): an empty return means
    // "bind SQL NULL for column `c`" -- no content type, or `contentType` case-insensitively
    // equals the storage's main content type (SameTextA). Otherwise returns `contentType` itself,
    // to be bound as the `c` value.
    [[nodiscard]] QByteArray contentTypeToDb(QByteArrayView contentType) const;

    // Ports GetContentTypeInfo (:181-189): an empty `contentTypeFromDb` (column `c` was NULL)
    // means the storage's main content type. Otherwise the DB value is looked up in
    // core::ContentTypeRegistry::standard() (T-0022B2 review finding 4): the canonical MIME for a
    // registered type, or an empty/null QByteArray for an unregistered one -- legacy's
    // `FContentTypeManager.GetInfo(c)` returns nil for an unknown type, not `c` itself.
    [[nodiscard]] QByteArray contentTypeFromDb(QByteArrayView contentTypeFromDb) const;

    // Ports GetVersionInfo (:191-194): FMapVersionFactory.CreateByStoreString.
    [[nodiscard]] MapVersionInfoPtr versionInfo(const QString& versionStr) const;

    // Ports LogError (:264-296) minus ARaiseError (see header comment). Always logs via
    // qCWarning(core::logStorage); when `logging` was true at construction, also appends one line
    // to `<storagePath>sqlite.log` in the exact §2.16.4 format
    // ("yyyy-mm-dd hh:nn:ss.zzzz\t(kind)\t<message>\r\n") through SqliteErrorLog. Deviation D-26
    // (already landed): that file is UTF-8, not legacy's UTF-16LE.
    void logError(LogKind kind, const QString& message) const;

private:
    QByteArray mainContentType_;
    MapVersionFactoryPtr mapVersionFactory_;
    bool logging_ = false;

    QList<QByteArray> sessionSql_;
    QList<QByteArray> newTableSql_;
    QList<QByteArray> establishedSql_;

    std::unique_ptr<SqliteErrorLog> errorLog_; // null unless `logging` was true
};

} // namespace cg::storage::sqlite
