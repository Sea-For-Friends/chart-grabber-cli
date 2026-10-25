// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLite/u_EnumTileInfoBySQLite.pas (:79-189), u_TileStorageSQLiteFetcher.pas
// (:118-440), i_TileStorageSQLiteFetcher.pas, and the file walk built in
// u_TileStorageSQLite.pas:505-558 (ScanTiles: mask '*.sqlitedb', folders by levels, depth 10).
//
// docs/specs/storage.md §3.3 scan amendment (2026-09-28): the enumerator keeps one SELECT open on
// the file's shared connection across next() calls (through a pinned SqliteRegionPool::Lease) and
// takes that file's mutex per step, not for the whole walk. FEAT-STO-035.

#include "cg/storage/sqlite/sqlite_region_pool.h"
#include "cg/storage/tile_info.h"
#include "cg/storage/tile_name_codec.h"

#include <QString>

#include <memory>

namespace cg::storage::sqlite {

// `storagePath` ends with '/'. `ignoreTne` ports AIgnoreTNE. There is no ignoreMultiVersionTiles
// parameter: legacy stores it and never reads it (u_EnumTileInfoBySQLite.pas:96, "(!) not used"),
// docs/parity/legacy-defects.md.
// FEAT-STO-035
[[nodiscard]] EnumTileInfoPtr makeSqliteRegionScan(std::shared_ptr<SqliteRegionPool> pool,
                                                   QString storagePath, ITileNameCodecPtr codec,
                                                   bool ignoreTne);

} // namespace cg::storage::sqlite
