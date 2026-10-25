// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileFileNameSQLite.pas, u_TileFileNameBerkeleyDB.pas -- byte-identical apart from the
// class name (docs/specs/storage.md §2.10.2), so one codec class serves both the native-SQLite
// (`cache_sqlite`) and BerkeleyDB region-file backends. The FEAT id that owns this class is
// assigned with the backend that first links it (T-0011/T-0021D for SQLite, T-0012 for
// BerkeleyDB); this ticket (T-0021C) ports the naming formula only.

#include "cg/storage/tile_name_codec.h"

namespace cg::storage {

// tilePath: "z{Z+1}/{X>>10}/{Y>>10}/{X>>8}.{Y>>8}". Each region file holds a 256x256 tile block
// (the two middle directories shard by 1024 tiles); parse() reads back only the block's own
// top-left tile (X = m3 << 8, Y = m4 << 8) -- the directory shard numbers are write-only, exactly
// like ES's directory structure (T-0021B).
//
// The base file name already contains a '.' (the {X>>8}.{Y>>8} separator), so the extension must
// be CONCATENATED directly by the caller, never passed through addExt()/ChangeFileExt (it would
// find that '.' first and eat the {Y>>8} component). This codec deliberately does not override
// addExt() -- legacy's TTileFileNameSQLite/TTileFileNameBerkeleyDB don't either; the callers
// (u_TileStorageSQLiteHelper.pas:238-240, u_TileStorageBerkeleyDB.pas:634) simply never call it.
class RegionTileNameCodec final : public ITileNameCodec {
public:
    [[nodiscard]] QString tilePath(geo::PointI xy, quint8 zoom) const override;
    [[nodiscard]] bool parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const override;
};

} // namespace cg::storage
