// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorageImporter/u_TileStorageSQLiteFileImporter.pas:234-527 (GetTablesInfo,
// IsContainsAll/Any, TryGetOruxMapsXmlFileName, TryDetectCacheTypeCode, ReadMetadata,
// TryDetectContentType) and the projection / content-type chains of the same unit's file-info
// builder (:763-805). docs/specs/storage.md §2.17.4, §2.17.5.
//
// Not ported here: TryDetectGoToPoint and the import job (:528-), and ReadMetadata's OruxMaps
// branch (map_name / center / layer_N_* from the .otrk2.xml -- read by the file-info builder (map name),
// TryDetectGoToPoint and the import dialog; the FEAT-STO-030 importer ticket carries that branch,
// with Trim, RoundEx and parseOruxXml exposed).

#include "cg/core/content_type.h"
#include "cg/core/result.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/sqlite/sqlite_file_connection.h"

#include <QByteArray>
#include <QHash>
#include <QString>

#include <optional>

namespace cg::storage::sqlite {

// FEAT-STO-022. Which of the five formats `path` is, or nullopt. Opens the file read-only.
[[nodiscard]] core::Result<std::optional<SqliteFileFormat>> detectSqliteFileFormat(const QString& path);

// Lower-cased key -> value. MBTiles: the `metadata` rows; RMaps/OsmAnd/Locus: the first `info` row
// by column; OruxMaps: empty. A `sasgis` JSON value adds `sasgis_epsg` / `sasgis_format`.
using SqliteFileMetadata = QHash<QString, QString>;
[[nodiscard]] core::Result<SqliteFileMetadata> readSqliteFileMetadata(SqliteDb& db, SqliteFileFormat format);

// FEAT-STO-022. info.sasgis.epsg -> `crs` ("EPSG:<n>" or "<n>") -> `ellipsoid` = 1 => 3395 -> 3857.
[[nodiscard]] core::Result<int> sqliteFileProjectionEpsg(const SqliteFileMetadata& metadata);

struct SqliteFileContentType {
    QByteArray type; // "" = unsupported (a `format` extension the registry does not know)
    QString ext;     // registry default extension of `type`, or ".<format>" when unsupported
    bool isBitmap = false;
};

// FEAT-STO-022. info.sasgis.format -> `format` (extension or MIME) -> first tile's magic bytes ->
// "image/jpg".
[[nodiscard]] core::Result<SqliteFileContentType>
sqliteFileContentType(SqliteDb& db, SqliteFileFormat format, const SqliteFileMetadata& metadata,
                      const core::ContentTypeRegistry& registry);

} // namespace cg::storage::sqlite
