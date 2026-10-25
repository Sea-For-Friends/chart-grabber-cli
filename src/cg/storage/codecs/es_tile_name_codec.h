// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileFileNameES.pas -- the EarthSlicer (ES) 3-tier folder cache (CacheType id 3),
// default root folder `cache_es`. FEAT-STO-004.

#include "cg/storage/tile_name_codec.h"

namespace cg::storage {

// tilePath is 3-tiered by zoom (see the .cpp for the exact FullInt/directory formulas ported
// from GetTileFileName): zoom < 6 is a flat "{zoomStr}/{file}"; zoom < 10 adds one packed
// subfolder; zoom >= 10 adds a top-level "10-{shard}-{shard}" directory on top of that. Parsing
// only ever reads the file name (the directory structure is write-only information for GetTilePoint).
class EsTileNameCodec final : public ITileNameCodec {
public:
    [[nodiscard]] QString tilePath(geo::PointI xy, quint8 zoom) const override;
    [[nodiscard]] bool parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const override;
};

} // namespace cg::storage
