// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileFileNameTMS.pas -- the Tile Map Service (TMS) folder cache (CacheType id 45),
// default root folder `cache_tms`. FEAT-STO-010.

#include "cg/storage/tile_name_codec.h"

namespace cg::storage {

// tilePath: "{Z}/{X}/{Y'}" where Y' = (1 << Z) - Y - 1 -- TMS numbers tiles from the bottom, so
// generation and parsing both flip the Y axis. Ports TTileFileNameTMS.
class TmsTileNameCodec final : public ITileNameCodec {
public:
    [[nodiscard]] QString tilePath(geo::PointI xy, quint8 zoom) const override;
    [[nodiscard]] bool parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const override;
};

} // namespace cg::storage
