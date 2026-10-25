// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileFileNameMOBAC.pas -- the Mobile Atlas Creator (MOBAC) folder cache (CacheType id
// 43), default root folder `cache_ma`. FEAT-STO-008.

#include "cg/storage/tile_name_codec.h"

namespace cg::storage {

// tilePath: "{Z}/{X}/{Y}" -- X before Y (unlike GM1/GM2/GM3). Ports TTileFileNameMOBAC.
class MobacTileNameCodec final : public ITileNameCodec {
public:
    [[nodiscard]] QString tilePath(geo::PointI xy, quint8 zoom) const override;
    [[nodiscard]] bool parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const override;
};

} // namespace cg::storage
