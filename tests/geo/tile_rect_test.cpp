// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_TileRect.pas TTileRect.IsEqual(ITileRect) (equal / different rect / different
// projection); docs/specs/geo.md §4.1 omits the method, legacy is the source of truth (T-0029).
#include "cg/geo/datum.h"
#include "cg/geo/projection.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type_factory.h"
#include "cg/geo/tile_rect.h"

#include <gtest/gtest.h>

#include <memory>

using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::ProjectionPtr;
using cg::geo::ProjectionSetFactory;
using cg::geo::RectI;
using cg::geo::TileRect;

namespace {

ProjectionSetFactory makeSetFactory() {
    auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    return ProjectionSetFactory{HashSeed{}, datums};
}

ProjectionPtr atZoom(ProjectionSetFactory& factory, int epsg, quint8 zoom) {
    return factory.byCode(epsg, kTileSplitQuadrate256x256)->zoom(zoom);
}

} // namespace

TEST(TileRect, IsEqualTrueForSameRectAndProjection) {
    auto factory = makeSetFactory();
    const TileRect a(atZoom(factory, 3857, 2), RectI{0, 0, 2, 2});
    const TileRect b(atZoom(factory, 3857, 2), RectI{0, 0, 2, 2});
    EXPECT_TRUE(a.isEqual(b));
    EXPECT_TRUE(a.isEqual(a));
}

TEST(TileRect, IsEqualFalseForDifferentRect) {
    auto factory = makeSetFactory();
    const auto projection = atZoom(factory, 3857, 2);
    const TileRect a(projection, RectI{0, 0, 2, 2});
    const TileRect b(projection, RectI{0, 0, 3, 2});
    EXPECT_FALSE(a.isEqual(b));
}

TEST(TileRect, IsEqualFalseForDifferentProjection) {
    auto factory = makeSetFactory();
    const TileRect a(atZoom(factory, 3857, 2), RectI{0, 0, 2, 2});
    const TileRect b(atZoom(factory, 3857, 3), RectI{0, 0, 2, 2});
    EXPECT_FALSE(a.isEqual(b));
}
