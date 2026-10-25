// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: t_GeometryPointSML.pas -- the on-disk point record of an SML geometry blob.

#include "cg/marks/sml/extended80.h"

#include <cstddef>
#include <cstdint>

namespace cg::marks {

// TGeometryPointSML = packed record X, Y: Extended; Reserved: LongWord (t_GeometryPointSML.pas:41-45).
// 10 + 10 + 4 bytes, no padding (packed), little-endian. Reserved is always written 0 and never read.
struct GeometryPointSml {
    std::uint8_t x[kExtended80Size];
    std::uint8_t y[kExtended80Size];
    std::uint8_t reserved[4];
};
static_assert(sizeof(GeometryPointSml) == 24 && alignof(GeometryPointSml) == 1);

inline constexpr std::size_t kGeometryPointSmlSize = sizeof(GeometryPointSml);

} // namespace cg::marks
