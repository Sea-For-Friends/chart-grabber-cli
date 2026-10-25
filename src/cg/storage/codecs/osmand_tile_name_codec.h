// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileFileNameOsmAnd.pas -- the OsmAnd+ folder cache (CacheType id 44), default root
// folder `cache_osmand`. FEAT-STO-009.

#include "cg/storage/tile_name_codec.h"

namespace cg::storage {

// tilePath: "{Z}/{X}/{Y}" -- same shape as MOBAC. addExt() is overridden: a real tile gets
// ".<ext>.tile" (e.g. "12/345/678.jpg.tile"), a TNE marker keeps a plain ".tne". Ports
// TTileFileNameOsmAnd.
//
// storage.md §10 Q1 / deviation D-4: legacy's single-tile lookup (GetTileInfoByPath) derives the
// TNE path from the *already-extended* name (ChangeFileExt("…/678.jpg.tile", ".tne") ->
// "…/678.jpg.tne"), while every write path (SaveTile/DeleteTile/GetTileRectInfo) uses
// AddExt(baseName, ".tne") -> "…/678.tne" -- so legacy can never find the TNE it wrote itself via
// that one lookup path. Decided "fix" (Tech Lead ruling 2026-09-18): this codec always produces
// "…/678.tne" for a TNE marker; there is no separate "lookup" spelling to reproduce.
class OsmandTileNameCodec final : public ITileNameCodec {
public:
    [[nodiscard]] QString tilePath(geo::PointI xy, quint8 zoom) const override;
    [[nodiscard]] bool parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const override;
    [[nodiscard]] QString addExt(const QString& path, QStringView ext) const override;
};

} // namespace cg::storage
