// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileFileNameSAS.pas -- the SAS.Planet native folder cache (CacheType id 2), default
// root folder `cache`. FEAT-STO-002.

#include "cg/storage/tile_name_codec.h"

namespace cg::storage {

// tilePath: "z{zoom+1}/{x>>10}/x{x}/{y>>10}/y{y}". Ports TTileFileNameSAS.
class SasTileNameCodec final : public ITileNameCodec {
public:
    [[nodiscard]] QString tilePath(geo::PointI xy, quint8 zoom) const override;
    [[nodiscard]] bool parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const override;
};

} // namespace cg::storage
