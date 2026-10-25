// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileFileNameGMV.pas -- the GoogleMV (classic/old) quad-key folder cache (CacheType
// id 1), default root folder `cache_old`. FEAT-STO-003.

#include "cg/storage/tile_name_codec.h"

namespace cg::storage {

// tilePath: "{zoomStr}/t{quad}", zoomStr = Z+1 zero-padded to 2 digits below zoom 9, quad built
// MSB-first from the q/t/r/s quadrant of X/Y at each bit. Ports TTileFileNameGMV. Zoom 0 is the
// degenerate single file "01/t". Parsing derives the zoom from the quad string's length, not
// from the leading directory (a mis-named directory is silently accepted, matching legacy).
class GmvTileNameCodec final : public ITileNameCodec {
public:
    [[nodiscard]] QString tilePath(geo::PointI xy, quint8 zoom) const override;
    [[nodiscard]] bool parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const override;
};

} // namespace cg::storage
