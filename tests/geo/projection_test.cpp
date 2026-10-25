// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-001, FEAT-GEO-014. Ports Test/u_ProjectionSet_Test.pas (48 cases, sixteen
// checks x three types); docs/specs/geo.md §11.1 item 3, §11.2 projection_test.cpp (22 new).
#include "cg/geo/datum.h"
#include "cg/geo/projection.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type_factory.h"

#include <gtest/gtest.h>

#include <memory>

#include "geo_test_util.h"

using cg::geo::DatumFactory;
using cg::geo::DoublePoint;
using cg::geo::DoubleRect;
using cg::geo::HashSeed;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::PointI;
using cg::geo::PointRounding;
using cg::geo::ProjectionPtr;
using cg::geo::ProjectionSetFactory;
using cg::geo::RectI;
using cg::geo::roundPoint;
using cg::geo::roundRect;
using cg::geo::test::kProjectionSetEpsilon;

namespace {

ProjectionSetFactory makeSetFactory() {
    auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    return ProjectionSetFactory{HashSeed{}, datums};
}

ProjectionPtr atZoom(ProjectionSetFactory& factory, int epsg, quint8 zoom) {
    return factory.byCode(epsg, kTileSplitQuadrate256x256)->zoom(zoom);
}

} // namespace

// --- 3. u_ProjectionSet_Test.pas -> ported: 16 checks x {4326, 3395, 3857} = 48 -----------------

class ProjectionSetMatrix : public ::testing::TestWithParam<int> {
protected:
    ProjectionSetFactory factory_ = makeSetFactory();
};

INSTANTIATE_TEST_SUITE_P(ThreeTypes, ProjectionSetMatrix, ::testing::Values(4326, 3395, 3857));

TEST_P(ProjectionSetMatrix, TilesAtZoom) {
    const int epsg = GetParam();
    EXPECT_EQ(atZoom(factory_, epsg, 0)->tileRect().width(), 1);
    EXPECT_EQ(atZoom(factory_, epsg, 1)->tileRect().width(), 2);
    EXPECT_EQ(atZoom(factory_, epsg, 23)->tileRect().width(), 8388608);
}

TEST_P(ProjectionSetMatrix, TilePos2PixelPos) {
    const int epsg = GetParam();
    EXPECT_EQ(atZoom(factory_, epsg, 0)->tilePos2PixelPos({0, 0}), PointI(0, 0));
    EXPECT_EQ(atZoom(factory_, epsg, 1)->tilePos2PixelPos({0, 1}), PointI(0, 256));
    EXPECT_EQ(atZoom(factory_, epsg, 1)->tilePos2PixelPos({1, 1}), PointI(256, 256));
    EXPECT_EQ(atZoom(factory_, epsg, 23)->tilePos2PixelPos({1, 1}), PointI(256, 256));
    EXPECT_EQ(atZoom(factory_, epsg, 23)->tilePos2PixelPos({8388607, 8388607}),
              PointI(2147483392, 2147483392));
}

TEST_P(ProjectionSetMatrix, TilePos2PixelRect) {
    const int epsg = GetParam();
    EXPECT_EQ(atZoom(factory_, epsg, 0)->tilePos2PixelRect({0, 0}), (RectI{0, 0, 256, 256}));
    EXPECT_EQ(atZoom(factory_, epsg, 1)->tilePos2PixelRect({1, 0}), (RectI{256, 0, 512, 256}));
    EXPECT_EQ(atZoom(factory_, epsg, 23)->tilePos2PixelRect({8388607, 8388607}),
              (RectI{2147483392, 2147483392, 2147483647, 2147483647}));
}

TEST_P(ProjectionSetMatrix, TilePos2Relative) {
    const int epsg = GetParam();
    EXPECT_EQ(atZoom(factory_, epsg, 0)->tilePos2Relative({0, 0}), DoublePoint(0.0, 0.0));
    EXPECT_EQ(atZoom(factory_, epsg, 1)->tilePos2Relative({0, 0}), DoublePoint(0.0, 0.0));
    EXPECT_EQ(atZoom(factory_, epsg, 1)->tilePos2Relative({1, 1}), DoublePoint(0.5, 0.5));
    EXPECT_EQ(atZoom(factory_, epsg, 1)->tilePos2Relative({2, 2}), DoublePoint(1.0, 1.0));
    EXPECT_EQ(atZoom(factory_, epsg, 23)->tilePos2Relative({0, 0}), DoublePoint(0.0, 0.0));
    const auto r = atZoom(factory_, epsg, 23)->tilePos2Relative({1, 1});
    EXPECT_NEAR(r.x(), 1.1920928955e-07, kProjectionSetEpsilon);
    EXPECT_NEAR(r.y(), 1.1920928955e-07, kProjectionSetEpsilon);
    EXPECT_EQ(atZoom(factory_, epsg, 23)->tilePos2Relative({8388608, 8388608}), DoublePoint(1.0, 1.0));
}

TEST_P(ProjectionSetMatrix, TilePos2RelativeRect) {
    const int epsg = GetParam();
    EXPECT_EQ(atZoom(factory_, epsg, 0)->tilePos2RelativeRect({0, 0}), (DoubleRect{0, 0, 1, 1}));
    EXPECT_EQ(atZoom(factory_, epsg, 1)->tilePos2RelativeRect({1, 1}), (DoubleRect{0.5, 0.5, 1, 1}));
    const auto r = atZoom(factory_, epsg, 23)->tilePos2RelativeRect({8388607, 8388607});
    const double edge = 1.0 - 1.1920928955e-07;
    EXPECT_NEAR(r.left, edge, kProjectionSetEpsilon);
    EXPECT_NEAR(r.top, edge, kProjectionSetEpsilon);
    EXPECT_NEAR(r.right, 1.0, kProjectionSetEpsilon);
    EXPECT_NEAR(r.bottom, 1.0, kProjectionSetEpsilon);
}

TEST_P(ProjectionSetMatrix, PixelPos2TilePos) {
    const int epsg = GetParam();
    auto pixelPos2TilePos = [](const ProjectionPtr& p, PointI px) {
        return roundPoint(p->pixelPos2TilePosFloat(px), PointRounding::ToTopLeft);
    };
    EXPECT_EQ(pixelPos2TilePos(atZoom(factory_, epsg, 0), {0, 0}), PointI(0, 0));
    EXPECT_EQ(pixelPos2TilePos(atZoom(factory_, epsg, 0), {156, 73}), PointI(0, 0));
    EXPECT_EQ(pixelPos2TilePos(atZoom(factory_, epsg, 0), {255, 255}), PointI(0, 0));
    EXPECT_EQ(pixelPos2TilePos(atZoom(factory_, epsg, 23), {255, 255}), PointI(0, 0));
    EXPECT_EQ(pixelPos2TilePos(atZoom(factory_, epsg, 23), {2147483392, 2147483647}),
              PointI(8388607, 8388607));
}

TEST_P(ProjectionSetMatrix, PixelPos2Relative) {
    const int epsg = GetParam();
    auto near = [](DoublePoint a, DoublePoint b) {
        EXPECT_NEAR(a.x(), b.x(), kProjectionSetEpsilon);
        EXPECT_NEAR(a.y(), b.y(), kProjectionSetEpsilon);
    };
    near(atZoom(factory_, epsg, 0)->pixelPos2Relative({0, 128}), {0.0, 0.5});
    near(atZoom(factory_, epsg, 0)->pixelPos2Relative({255, 256}), {1.0 - 1.0 / 256.0, 1.0});
    near(atZoom(factory_, epsg, 23)->pixelPos2Relative({0, 1073741824}), {0.0, 0.5});
    near(atZoom(factory_, epsg, 23)->pixelPos2Relative({2147483647, 2147483647}),
         {1.0 - 1.0 / 2147483647.0, 1.0});
}

TEST_P(ProjectionSetMatrix, PixelRect2TileRect) {
    const int epsg = GetParam();
    EXPECT_EQ(atZoom(factory_, epsg, 0)->pixelRect2TileRect({0, 0, 255, 255}), (RectI{0, 0, 1, 1}));
    EXPECT_EQ(atZoom(factory_, epsg, 1)->pixelRect2TileRect({0, 0, 255, 255}), (RectI{0, 0, 1, 1}));
    EXPECT_EQ(atZoom(factory_, epsg, 1)->pixelRect2TileRect({0, 0, 511, 255}), (RectI{0, 0, 2, 1}));
    EXPECT_EQ(atZoom(factory_, epsg, 23)->pixelRect2TileRect({0, 0, 511, 255}), (RectI{0, 0, 2, 1}));
    EXPECT_EQ(
        atZoom(factory_, epsg, 23)->pixelRect2TileRect({2147483392, 2147483647, 2147483392, 2147483647}),
        (RectI{8388607, 8388607, 8388607, 8388608}));
    EXPECT_EQ(atZoom(factory_, epsg, 23)->pixelRect2TileRect({0, 0, 2147483392, 2147483647}),
              (RectI{0, 0, 8388607, 8388608}));
}

TEST_P(ProjectionSetMatrix, PixelRect2RelativeRect) {
    const int epsg = GetParam();
    auto near = [](const DoubleRect& a, const DoubleRect& b) {
        EXPECT_NEAR(a.left, b.left, kProjectionSetEpsilon);
        EXPECT_NEAR(a.top, b.top, kProjectionSetEpsilon);
        EXPECT_NEAR(a.right, b.right, kProjectionSetEpsilon);
        EXPECT_NEAR(a.bottom, b.bottom, kProjectionSetEpsilon);
    };
    near(atZoom(factory_, epsg, 0)->pixelRect2RelativeRect({0, 0, 0, 0}), {0, 0, 0, 0});
    near(atZoom(factory_, epsg, 0)->pixelRect2RelativeRect({0, 0, 256, 256}), {0, 0, 1, 1});
    near(atZoom(factory_, epsg, 23)->pixelRect2RelativeRect({0, 1073741824, 255, 2147483647}),
         {0, 0.5, 1.0 / 8388608.0, 1});
}

TEST_P(ProjectionSetMatrix, Relative2PixelPos) {
    const int epsg = GetParam();
    auto relative2PixelPos = [](const ProjectionPtr& p, DoublePoint r) {
        return roundPoint(p->relative2PixelPosFloat(r), PointRounding::ToTopLeft);
    };
    EXPECT_EQ(relative2PixelPos(atZoom(factory_, epsg, 0), {1.0 / 256.0, 1.0 / 500.0}), PointI(1, 0));
    EXPECT_EQ(relative2PixelPos(atZoom(factory_, epsg, 0), {1.0, 1.0}), PointI(256, 256));
    EXPECT_EQ(relative2PixelPos(atZoom(factory_, epsg, 23), {1.0, 1.0}), PointI(2147483647, 2147483647));
}

TEST_P(ProjectionSetMatrix, Relative2TilePos) {
    const int epsg = GetParam();
    auto relative2TilePos = [](const ProjectionPtr& p, DoublePoint r) {
        return roundPoint(p->relative2TilePosFloat(r), PointRounding::ToTopLeft);
    };
    EXPECT_EQ(relative2TilePos(atZoom(factory_, epsg, 0), {1.0 / 256.0, 1.0 / 500.0}), PointI(0, 0));
    EXPECT_EQ(relative2TilePos(atZoom(factory_, epsg, 0), {1.0, 1.0}), PointI(1, 1));
    EXPECT_EQ(relative2TilePos(atZoom(factory_, epsg, 23), {1.0, 1.0}), PointI(8388608, 8388608));
}

TEST_P(ProjectionSetMatrix, RelativeRect2PixelRect) {
    const int epsg = GetParam();
    const DoubleRect input{0.0, 1.0 / 256.0, 1.0, 511.0 / 512.0};
    auto relativeRect2PixelRect = [](const ProjectionPtr& p, const DoubleRect& r) {
        return roundRect(p->relativeRect2PixelRectFloat(r), cg::geo::RectRounding::ToTopLeft);
    };
    EXPECT_EQ(relativeRect2PixelRect(atZoom(factory_, epsg, 0), input), (RectI{0, 1, 256, 255}));
    EXPECT_EQ(relativeRect2PixelRect(atZoom(factory_, epsg, 23), input),
              (RectI{0, 8388608, 2147483647, 2143289344}));
}

TEST_P(ProjectionSetMatrix, RelativeRect2TileRect) {
    const int epsg = GetParam();
    const DoubleRect input{0.0, 1.0 / 256.0, 1.0, 511.0 / 512.0};
    auto relativeRect2TileRect = [](const ProjectionPtr& p, const DoubleRect& r) {
        return roundRect(p->relativeRect2TileRectFloat(r), cg::geo::RectRounding::ToTopLeft);
    };
    EXPECT_EQ(relativeRect2TileRect(atZoom(factory_, epsg, 0), input), (RectI{0, 0, 1, 0}));
    EXPECT_EQ(relativeRect2TileRect(atZoom(factory_, epsg, 23), input), (RectI{0, 32768, 8388608, 8372224}));
}

TEST_P(ProjectionSetMatrix, TilePos2LonLat2TilePos) {
    const int epsg = GetParam();
    auto roundTrip = [](const ProjectionPtr& p, PointI t) {
        return roundPoint(p->lonLat2TilePosFloat(p->tilePos2LonLat(t)), PointRounding::ToTopLeft);
    };
    for (int z : {8, 10, 14, 20, 23}) {
        EXPECT_EQ(roundTrip(atZoom(factory_, epsg, static_cast<quint8>(z)), {10, 99}), (PointI{10, 99}))
            << "zoom " << z;
    }
    for (int z : {10, 11, 14, 20, 22}) {
        EXPECT_EQ(roundTrip(atZoom(factory_, epsg, static_cast<quint8>(z)), {1024, 768}), (PointI{1024, 768}))
            << "zoom " << z;
    }
}

TEST_P(ProjectionSetMatrix, Relative2LonLat2Relative) {
    const int epsg = GetParam();
    const auto p = atZoom(factory_, epsg, 10); // any zoom: the projectionType formula is zoom-independent
    const double x = 1.0 / 256.0;
    double y = 0.0;
    double maxDelta = 0.0;
    while (y <= 1.0) {
        const auto ll = p->projectionType()->relative2LonLat({x, y});
        const auto back = p->projectionType()->lonLat2Relative(ll);
        const double delta = std::abs(y - back.y());
        EXPECT_LT(delta, 1e-8) << "y=" << y;
        maxDelta = std::max(maxDelta, delta);
        y += 0.00001;
    }
    EXPECT_LT(maxDelta, 2.3283064365e-10);
}

TEST_P(ProjectionSetMatrix, MonotonicRelative2LonLat) {
    const int epsg = GetParam();
    const auto p22 = atZoom(factory_, epsg, 22);
    const double delta = 1.0 / p22->pixelsFloat();
    double y = 0.0;
    double prevLat = p22->projectionType()->relative2LonLat({0.5, y}).lat;
    y += delta;
    while (y < delta * 100000) {
        const double curLat = p22->projectionType()->relative2LonLat({0.5, y}).lat;
        EXPECT_GT(prevLat - curLat, 0.0) << "y=" << y;
        prevLat = curLat;
        y += delta;
    }
}

// --- new coverage --------------------------------------------------------------------------

TEST(Projection, ZoomTwentyThreePixelsSaturateAtIntMax) {
    auto factory = makeSetFactory();
    EXPECT_EQ(atZoom(factory, 3857, 23)->pixelRect().right, 2147483647);
}
TEST(Projection, ZoomTwentyThreePixelsFloatIsExact) {
    auto factory = makeSetFactory();
    EXPECT_DOUBLE_EQ(atZoom(factory, 3857, 23)->pixelsFloat(), 2147483648.0);
}
TEST(Projection, TilePos2PixelRectPatchesZoom23Overflow) {
    auto factory = makeSetFactory();
    const auto rect = atZoom(factory, 3857, 23)->tilePos2PixelRect({8388607, 8388607});
    EXPECT_EQ(rect.right, 2147483647);
    EXPECT_EQ(rect.bottom, 2147483647);
}
TEST(Projection, TileRect2PixelRectPatchesZoom23Overflow) {
    auto factory = makeSetFactory();
    const auto rect = atZoom(factory, 3857, 23)->tileRect2PixelRect({0, 0, 8388608, 8388608});
    EXPECT_EQ(rect.right, 2147483647);
    EXPECT_EQ(rect.bottom, 2147483647);
}
TEST(Projection, PixelRect2TileRectRoundsBottomRightOutward) {
    auto factory = makeSetFactory();
    const auto rect = atZoom(factory, 3857, 1)->pixelRect2TileRect({0, 0, 1, 1});
    EXPECT_EQ(rect, (RectI{0, 0, 1, 1})); // (1+255)/256 = 1, covers the single touched tile
}
TEST(Projection, PixelPos2TilePosFloatDoesNotClamp) {
    auto factory = makeSetFactory();
    const auto p = atZoom(factory, 3857, 0)->pixelPos2TilePosFloat({-1000, -1000});
    EXPECT_LT(p.x(), 0.0);
    EXPECT_LT(p.y(), 0.0);
}
TEST(Projection, ValidatePixelPosFloatWrapsWithTruncNotFloor) {
    auto factory = makeSetFactory();
    const auto p0 = atZoom(factory, 3857, 0); // pixelsFloat = 256
    DoublePoint pos{-10.0, 0.0};
    p0->validatePixelPosFloat(pos, true);
    EXPECT_NEAR(pos.x(), 246.0, 1e-9);
}
TEST(Projection, ValidatePixelPosStrictClampsToPixelsMinusOne) {
    auto factory = makeSetFactory();
    const auto p0 = atZoom(factory, 3857, 0);
    PointI pos{9999, 0};
    p0->validatePixelPosStrict(pos, false);
    EXPECT_EQ(pos.x(), 255);
}
TEST(Projection, ValidateTilePosWrapsXOnlyWhenCycleMap) {
    auto factory = makeSetFactory();
    const auto p0 = atZoom(factory, 3857, 1); // tiles = 2
    PointI pos{-1, 0};
    p0->validateTilePos(pos, true);
    EXPECT_EQ(pos.x(), 1);
    PointI pos2{-1, 0};
    p0->validateTilePos(pos2, false);
    EXPECT_EQ(pos2.x(), 0);
}
TEST(Projection, ValidateTilePosNeverWrapsY) {
    auto factory = makeSetFactory();
    const auto p0 = atZoom(factory, 3857, 1);
    PointI pos{0, -1};
    p0->validateTilePos(pos, true);
    EXPECT_EQ(pos.y(), 0);
}
TEST(Projection, ValidatePixelPosReproducesLegacyYBug) {
    // The D-1 anchor (u_ProjectionBasic256x256.pas:1648-1652, docs/specs/geo.md §2.5.3/§10 D-1):
    // every non-negative Y is replaced by pixels_, regardless of whether it was already in range.
    auto factory = makeSetFactory();
    const auto p0 = atZoom(factory, 3857, 0);
    PointI pos{5, 5};
    p0->validatePixelPos(pos, false);
    EXPECT_EQ(pos.y(), p0->pixelRect().right); // pixels_, NOT 5
}
TEST(Projection, ValidateRectNeverWraps) {
    auto factory = makeSetFactory();
    const auto p0 = atZoom(factory, 3857, 1);
    RectI r{-5, -5, 100, 100};
    p0->validateTileRect(r);
    EXPECT_EQ(r.left, 0);
    EXPECT_EQ(r.top, 0);
}
TEST(Projection, CheckMirrorsValidateBoundsPos) {
    auto factory = makeSetFactory();
    const auto p0 = atZoom(factory, 3857, 0);
    EXPECT_FALSE(p0->checkPixelPos({-1, 0}));
    EXPECT_TRUE(p0->checkPixelPos({256, 256}));
}
TEST(Projection, CheckMirrorsValidateBoundsPosFloat) {
    auto factory = makeSetFactory();
    const auto p0 = atZoom(factory, 3857, 0);
    EXPECT_FALSE(p0->checkPixelPosFloat({256.1, 0}));
    EXPECT_TRUE(p0->checkPixelPosFloat({256.0, 256.0}));
}
TEST(Projection, CheckMirrorsValidateBoundsStrict) {
    auto factory = makeSetFactory();
    const auto p0 = atZoom(factory, 3857, 0);
    EXPECT_FALSE(p0->checkPixelPosStrict({256, 0}));
    EXPECT_TRUE(p0->checkPixelPosStrict({255, 255}));
}
TEST(Projection, CheckMirrorsValidateBoundsRect) {
    auto factory = makeSetFactory();
    const auto p0 = atZoom(factory, 3857, 0);
    EXPECT_FALSE(p0->checkPixelRect({-1, 0, 100, 100}));
    EXPECT_TRUE(p0->checkPixelRect({0, 0, 256, 256}));
}
TEST(Projection, IsSameByPointer) {
    auto factory = makeSetFactory();
    const auto p0 = atZoom(factory, 3857, 5);
    EXPECT_TRUE(p0->isSame(*p0));
}
TEST(Projection, IsSameByHash) {
    auto factory = makeSetFactory();
    EXPECT_TRUE(atZoom(factory, 3857, 5)->isSame(*atZoom(factory, 3857, 5)));
}
TEST(Projection, IsSameByZoomAndType) {
    auto factory = makeSetFactory();
    EXPECT_FALSE(atZoom(factory, 3857, 5)->isSame(*atZoom(factory, 3857, 6)));
    EXPECT_FALSE(atZoom(factory, 3857, 5)->isSame(*atZoom(factory, 3395, 5)));
}
TEST(Projection, HashDiffersPerZoom) {
    auto factory = makeSetFactory();
    EXPECT_NE(atZoom(factory, 3857, 5)->hash(), atZoom(factory, 3857, 6)->hash());
}
TEST(Projection, TileSizeIsAlways256) {
    auto factory = makeSetFactory();
    EXPECT_EQ(atZoom(factory, 3857, 5)->tileSize({0, 0}), PointI(256, 256));
}
TEST(Projection, TileSplitCodeIsOne) {
    auto factory = makeSetFactory();
    EXPECT_EQ(atZoom(factory, 3857, 5)->tileSplitCode(), 1);
}
