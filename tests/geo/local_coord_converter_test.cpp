// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-001. New coverage only; docs/specs/geo.md §11.2 local_coord_converter_test.cpp
// (16), §2.7.
#include "cg/geo/datum.h"
#include "cg/geo/local_coord_converter.h"
#include "cg/geo/projection_set.h"

#include <gtest/gtest.h>

#include <memory>

using cg::geo::DatumFactory;
using cg::geo::DoublePoint;
using cg::geo::HashSeed;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::LocalCoordConverterFactory;
using cg::geo::LonLat;
using cg::geo::PointI;
using cg::geo::PointRounding;
using cg::geo::ProjectionPtr;
using cg::geo::ProjectionSetFactory;
using cg::geo::RectI;
using cg::geo::RectRounding;

namespace {

ProjectionSetFactory makeSets() {
    auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    return ProjectionSetFactory{HashSeed{}, datums};
}

} // namespace

TEST(LocalCoordConverter, LocalToMapAndBack) {
    auto sets = makeSets();
    const ProjectionPtr proj = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(10);
    LocalCoordConverterFactory factory{HashSeed{}};
    const auto conv = factory.createNoScaleIntDelta({0, 0, 800, 600}, proj, {1000, 2000});
    const PointI local{100, 50};
    const PointI mapPixel = conv->localPixel2MapPixel(local, PointRounding::Closest);
    EXPECT_EQ(mapPixel, PointI(1100, 2050));
    EXPECT_EQ(conv->mapPixel2LocalPixel(mapPixel, PointRounding::Closest), local);
}

TEST(LocalCoordConverter, ScaledLocalToMapAndBack) {
    auto sets = makeSets();
    const ProjectionPtr proj = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(10);
    LocalCoordConverterFactory factory{HashSeed{}};
    const auto conv = factory.createScaled({0, 0, 800, 600}, proj, 2.0, {1000.0, 2000.0});
    const DoublePoint mapPixel = conv->localPixelFloat2MapPixelFloat({100.0, 50.0});
    EXPECT_DOUBLE_EQ(mapPixel.x(), 1050.0); // 100/2 + 1000
    EXPECT_DOUBLE_EQ(mapPixel.y(), 2025.0); // 50/2 + 2000
    const DoublePoint back = conv->mapPixelFloat2LocalPixelFloat(mapPixel);
    EXPECT_NEAR(back.x(), 100.0, 1e-9);
    EXPECT_NEAR(back.y(), 50.0, 1e-9);
}

TEST(LocalCoordConverter, ScaleOneTakesTheFastPath) {
    auto sets = makeSets();
    const ProjectionPtr proj = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(10);
    LocalCoordConverterFactory factory{HashSeed{}};
    const auto conv = factory.createScaled({0, 0, 800, 600}, proj, 1.0, {1000.0, 2000.0});
    // scale_ == 1.0 must behave identically to the plain-add formula (division by 1 is exact).
    EXPECT_DOUBLE_EQ(conv->localPixelFloat2MapPixelFloat({7.0, 9.0}).x(), 1007.0);
    EXPECT_DOUBLE_EQ(conv->localPixelFloat2MapPixelFloat({7.0, 9.0}).y(), 2009.0);
}

class RoundingModesLocalToMap : public ::testing::TestWithParam<RectRounding> {};
INSTANTIATE_TEST_SUITE_P(FourModes, RoundingModesLocalToMap,
                         ::testing::Values(RectRounding::Closest, RectRounding::Outside, RectRounding::Inside,
                                           RectRounding::ToTopLeft));

TEST_P(RoundingModesLocalToMap, LocalRect2MapRectDoesNotCrashAndCoversTheFloatRect) {
    auto sets = makeSets();
    const ProjectionPtr proj = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(10);
    LocalCoordConverterFactory factory{HashSeed{}};
    const auto conv = factory.createScaled({0, 0, 800, 600}, proj, 1.5, {100.0, 200.0});
    const RectI mapped = conv->localRect2MapRect({10, 10, 90, 90}, GetParam());
    const auto mappedFloat = conv->localRect2MapRectFloat({10, 10, 90, 90});
    EXPECT_LE(mapped.left, static_cast<int>(mappedFloat.left) + 1);
    EXPECT_GE(mapped.right, static_cast<int>(mappedFloat.right) - 1);
}

TEST(LocalCoordConverter, CenterLonLatMatchesCenterPixel) {
    auto sets = makeSets();
    const ProjectionPtr proj = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(10);
    LocalCoordConverterFactory factory{HashSeed{}};
    const auto conv = factory.createNoScaleIntDelta({0, 0, 256, 256}, proj, {0, 0});
    const LonLat centerLonLat = conv->centerLonLat();
    const DoublePoint expectedPixel = conv->centerMapPixelFloat();
    const LonLat expectedLonLat = proj->pixelPosFloat2LonLat(expectedPixel);
    EXPECT_NEAR(centerLonLat.lon, expectedLonLat.lon, 1e-9);
    EXPECT_NEAR(centerLonLat.lat, expectedLonLat.lat, 1e-9);
}

TEST(LocalCoordConverter, CreateForTileCoversExactlyTheTile) {
    auto sets = makeSets();
    const ProjectionPtr proj = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(5);
    LocalCoordConverterFactory factory{HashSeed{}};
    const auto conv = factory.createForTile(proj, {3, 4});
    EXPECT_EQ(conv->localRect(), (RectI{0, 0, 256, 256}));
    EXPECT_EQ(conv->rectInMapPixel(), proj->tilePos2PixelRect({3, 4}));
}

TEST(LocalCoordConverter, ChangeCenterLonLatKeepsSize) {
    auto sets = makeSets();
    const ProjectionPtr proj = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(10);
    LocalCoordConverterFactory factory{HashSeed{}};
    const auto conv = factory.createNoScaleIntDelta({0, 0, 800, 600}, proj, {1000, 2000});
    const auto moved = factory.changeCenterLonLat(conv, {30.0, 40.0});
    EXPECT_EQ(moved->localRect(), conv->localRect());
    EXPECT_NEAR(moved->centerLonLat().lon, 30.0, 1e-6);
    EXPECT_NEAR(moved->centerLonLat().lat, 40.0, 1e-6);
}

TEST(LocalCoordConverter, ChangeByLocalDeltaShiftsOrigin) {
    auto sets = makeSets();
    const ProjectionPtr proj = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(10);
    LocalCoordConverterFactory factory{HashSeed{}};
    const auto conv = factory.createNoScaleIntDelta({0, 0, 800, 600}, proj, {1000, 2000});
    const auto shifted = factory.changeByLocalDelta(conv, {50.0, -20.0});
    const DoublePoint before = conv->localPixel2MapPixelFloat({0, 0});
    const DoublePoint after = shifted->localPixel2MapPixelFloat({0, 0});
    EXPECT_NEAR(after.x() - before.x(), 50.0, 1e-6);
    EXPECT_NEAR(after.y() - before.y(), -20.0, 1e-6);
}

TEST(LocalCoordConverter, ChangeProjectionWithFreezeAtVisualPointKeepsLonLatUnderTheCursor) {
    auto sets = makeSets();
    const ProjectionPtr projA = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(10);
    const ProjectionPtr projB = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(12);
    LocalCoordConverterFactory factory{HashSeed{}};
    const auto conv = factory.createNoScaleIntDelta({0, 0, 800, 600}, projA, {10000, 20000});
    const PointI cursor{400, 300};
    const LonLat before = projA->pixelPosFloat2LonLat(conv->localPixel2MapPixelFloat(cursor));
    const auto zoomed = factory.changeProjectionWithFreezeAtVisualPoint(conv, projB, cursor);
    const LonLat after = projB->pixelPosFloat2LonLat(zoomed->localPixel2MapPixelFloat(cursor));
    EXPECT_NEAR(before.lon, after.lon, 1e-6);
    EXPECT_NEAR(before.lat, after.lat, 1e-6);
}

TEST(LocalCoordConverter, ChangeProjectionWithFreezeAtCenterKeepsCenter) {
    auto sets = makeSets();
    const ProjectionPtr projA = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(10);
    const ProjectionPtr projB = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(12);
    LocalCoordConverterFactory factory{HashSeed{}};
    const auto conv = factory.createNoScaleIntDelta({0, 0, 800, 600}, projA, {10000, 20000});
    const LonLat before = conv->centerLonLat();
    const auto changed = factory.changeProjectionWithFreezeAtCenter(conv, projB);
    const LonLat after = changed->centerLonLat();
    EXPECT_NEAR(before.lon, after.lon, 1e-6);
    EXPECT_NEAR(before.lat, after.lat, 1e-6);
}

TEST(LocalCoordConverter, ChangeProjectionWithScaleUpdateScalesByPixelsRatio) {
    auto sets = makeSets();
    const ProjectionPtr projA = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(10);
    const ProjectionPtr projB = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(11);
    LocalCoordConverterFactory factory{HashSeed{}};
    const auto conv = factory.createNoScaleIntDelta({0, 0, 800, 600}, projA, {10000, 20000});
    const auto updated = factory.changeProjectionWithScaleUpdate(conv, projB);
    EXPECT_NEAR(updated->scale(), conv->scale() * (projA->pixelsFloat() / projB->pixelsFloat()), 1e-9);
}

TEST(LocalCoordConverter, IsSameConverter) {
    auto sets = makeSets();
    const ProjectionPtr proj = sets.byCode(3857, kTileSplitQuadrate256x256)->zoom(10);
    LocalCoordConverterFactory factory{HashSeed{}};
    const auto a = factory.createNoScaleIntDelta({0, 0, 800, 600}, proj, {1000, 2000});
    const auto b = factory.createNoScaleIntDelta({0, 0, 800, 600}, proj, {1000, 2000});
    const auto c = factory.createNoScaleIntDelta({0, 0, 800, 600}, proj, {1001, 2000});
    EXPECT_TRUE(a->isSameConverter(*b));
    EXPECT_FALSE(a->isSameConverter(*c));
}
