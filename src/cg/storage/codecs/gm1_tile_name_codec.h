// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileFileNameGM1.pas -- the GlobalMapper Tiles folder cache (CacheType id 4), default
// root folder `cache_gmt_tiles`. FEAT-STO-005.

#include "cg/storage/tile_name_codec.h"

namespace cg::storage {

// tilePath: "z{Z}/{Y}/{X}" -- (!) Y before X, exactly as the legacy generator/parser comment
// warns. Ports TTileFileNameGM1.
class Gm1TileNameCodec final : public ITileNameCodec {
public:
    [[nodiscard]] QString tilePath(geo::PointI xy, quint8 zoom) const override;
    [[nodiscard]] bool parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const override;
};

} // namespace cg::storage
