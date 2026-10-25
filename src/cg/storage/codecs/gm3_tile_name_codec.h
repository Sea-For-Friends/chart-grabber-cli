// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileFileNameGM3.pas -- the GlobalMapper Bing folder cache (CacheType id 42), default
// root folder `cache_gmt_bing`. FEAT-STO-007.

#include "cg/storage/tile_name_codec.h"

namespace cg::storage {

// tilePath: "{Z}/{Y}/{X}" -- no leading 'z', and (!) Y before X, exactly as the legacy
// generator/parser comment warns. Ports TTileFileNameGM3.
class Gm3TileNameCodec final : public ITileNameCodec {
public:
    [[nodiscard]] QString tilePath(geo::PointI xy, quint8 zoom) const override;
    [[nodiscard]] bool parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const override;
};

} // namespace cg::storage
