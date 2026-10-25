// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-015. No legacy DUnit test exercises TileIterator/*.pas directly (grep of
// Test/*.pas finds only u_CalcTilesInPolygon_Test.pas, which is tile_count.cpp, slice B of this
// ticket's Tech Lead split). "Otherwise minimal" branch of the test policy: row-major order,
// Int64 widening, empty rect, reset/start-point-wrap semantics (§4.2.1), the spiral order asserted
// tile-by-tile for one odd- and one even-sized rect (§4.2.2), and one polygon-clipped column-major
// case (§4.2.3).

#include "cg/geo/datum.h"
#include "cg/geo/geometry_projected.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/geo/tile_iterator.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

using cg::geo::DatumFactory;
using cg::geo::DoublePoint;
using cg::geo::DoubleRect;
using cg::geo::GeometryProjectedContour;
using cg::geo::GeometryProjectedContourPtr;
using cg::geo::GeometryProjectedSinglePolygon;
using cg::geo::HashSeed;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::PointI;
using cg::geo::ProjectedPointsPtr;
using cg::geo::ProjectionPtr;
using cg::geo::ProjectionSetFactory;
using cg::geo::RectI;
using cg::geo::ScanOrder;
using cg::geo::TileIteratorByPolygon;
using cg::geo::TileIteratorByRect;
using cg::geo::TileIteratorSpiralByRect;
using cg::geo::TileRect;

namespace {

// TileIteratorByRect/TileIteratorSpiralByRect never dereference TileRect::projection() (only
// TileIteratorByPolygon does, to convert pixel<->tile); a null projection is safe for those two
// and keeps these cases free of ProjectionSetFactory/DatumFactory setup.
TileRect makeRect(RectI r) {
    return TileRect(ProjectionPtr(), r);
}

ProjectionSetFactory makeSetFactory() {
    auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    return ProjectionSetFactory{HashSeed{}, datums};
}

ProjectionPtr atZoom(ProjectionSetFactory& factory, int epsg, quint8 zoom) {
    return factory.byCode(epsg, kTileSplitQuadrate256x256)->zoom(zoom);
}

ProjectedPointsPtr makePoints(std::vector<DoublePoint> pts) {
    return std::make_shared<const std::vector<DoublePoint>>(std::move(pts));
}

std::vector<PointI> collectAll(cg::geo::ITileIterator& iter) {
    std::vector<PointI> out;
    PointI t;
    while (iter.next(t)) {
        out.push_back(t);
    }
    return out;
}

} // namespace

TEST(TileIteratorByRect, RowMajorOrder) {
    // u_TileIteratorByRect.pas:130-147. X sweeps [left,right) then wraps to the next Y.
    TileIteratorByRect iter(makeRect({0, 0, 3, 2}));
    EXPECT_EQ(iter.scanOrder(), ScanOrder::Rows);
    EXPECT_EQ(iter.tilesTotal(), 6);
    const std::vector<PointI> expected = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}};
    EXPECT_EQ(collectAll(iter), expected);
}

TEST(TileIteratorByRect, TilesTotalWidensToInt64) {
    // u_TileIteratorByRect.pas:87-92: FTilesTotal := (Right-Left); FTilesTotal := FTilesTotal *
    // (Bottom-Top) -- two statements so the multiply happens in Int64, not Integer. 100000 *
    // 100000 = 1e10 overflows a 32-bit int (max ~2.1e9).
    TileIteratorByRect iter(makeRect({0, 0, 100000, 100000}));
    EXPECT_EQ(iter.tilesTotal(), static_cast<qint64>(100000) * 100000);
}

TEST(TileIteratorByRect, EmptyRectYieldsNothing) {
    TileIteratorByRect iter(makeRect({5, 5, 5, 5}));
    EXPECT_EQ(iter.tilesTotal(), 0);
    PointI t;
    EXPECT_FALSE(iter.next(t));
    iter.reset();
    EXPECT_FALSE(iter.next(t));
}

TEST(TileIteratorByRect, ResetAndStartPointWrapsToRectLeftNotStartX) {
    // docs/specs/geo.md §4.2.1: "from here, row-major, N tiles" -- the X wrap on overflow resets
    // to rect.left, never to the supplied start X, and termination is by processed count, not by
    // reaching the start row again. Rect {2,5,6,8} (width 4, height 3, area 12), start (4,5),
    // tilesToProcess=5: (4,5),(5,5) fill the rest of the start row, then wrap to x=2 (not x=4) on
    // row 6 for the remaining 3.
    TileIteratorByRect iter(makeRect({2, 5, 6, 8}), 5, PointI{4, 5});
    EXPECT_EQ(iter.tilesTotal(), 5); // tilesToProcess overwrites the area (12) since area > 0.
    const std::vector<PointI> expected = {{4, 5}, {5, 5}, {2, 6}, {3, 6}, {4, 6}};
    EXPECT_EQ(collectAll(iter), expected);

    iter.reset(); // Reset once: replays the identical sequence from the same start point.
    EXPECT_EQ(collectAll(iter), expected);
}

TEST(TileIteratorSpiralByRect, OddSizedRectClockwiseOrder) {
    // Hand derivation for rect {-1,-1,2,2} (3x3, odd): centre = ((-1+2)/2, (-1+2)/2) = (0,0)
    // (Pascal/C++ truncating division). Ring 0 is just the centre. Ring 1 (R=1, 1 tile/segment,
    // 8 segments) walks GetDeltaByRingAndIndex lineIndex 0..7 with indexInLine=0 throughout:
    //   0: (+1,-1)->(1,-1)  1: (+1, 0)->(1,0)   2: (+1,+1)->(1,1)   3: ( 0,+1)->(0,1)
    //   4: (-1,+1)->(-1,1)  5: (-1, 0)->(-1,0)  6: (-1,-1)->(-1,-1) 7: ( 0,-1)->(0,-1)
    // all 8 fall inside the 3x3 rect (x,y in {-1,0,1}), giving all 9 cells. maxRing overshoots to
    // 2 (GetMaxRing uses right/bottom unadjusted), so ring 2's 16 candidates are generated and
    // rejected by CheckPoint before EOI -- they produce no extra output.
    TileIteratorSpiralByRect iter(makeRect({-1, -1, 2, 2}));
    EXPECT_EQ(iter.scanOrder(), ScanOrder::Spiral);
    EXPECT_EQ(iter.tilesTotal(), 9); // rect area, per spec never consulted by next().
    const std::vector<PointI> expected = {{0, 0},  {1, -1}, {1, 0},   {1, 1}, {0, 1},
                                          {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}};
    EXPECT_EQ(collectAll(iter), expected);
}

TEST(TileIteratorSpiralByRect, EvenSizedRectClockwiseOrder) {
    // Hand derivation for rect {0,0,2,2} (2x2, even): centre = (1,1). GetMaxRing = 1. Ring 0
    // yields (1,1). Ring 1 (R=1) walks the same 8 lineIndex deltas as above but centred on (1,1):
    // lineIndex 0,1,2,3,4 land on (2,0),(2,1),(2,2),(1,2),(0,2) -- all rejected, x or y == 2 is
    // outside the half-open [0,2) rect. lineIndex 5,6,7 land on (0,1),(0,0),(1,0) -- all accepted.
    TileIteratorSpiralByRect iter(makeRect({0, 0, 2, 2}));
    EXPECT_EQ(iter.tilesTotal(), 4);
    const std::vector<PointI> expected = {{1, 1}, {0, 1}, {0, 0}, {1, 0}};
    EXPECT_EQ(collectAll(iter), expected);
}

TEST(TileIteratorByPolygon, ColumnMajorClippedOrder) {
    // Right triangle (0,0)-(600,0)-(0,600), hypotenuse x+y=600, at zoom 5 (pixel range up to
    // 8192, comfortably above 600 so no clamping). bounds/256 = {0,0,2.34,2.34}, rounded Outside
    // -> tiles rect {0,0,3,3} (9 candidate tiles). A tile [x0,x1)x[y0,y1) with x0,y0 >= 0 overlaps
    // the triangle iff its nearest-to-origin corner satisfies x0+y0 <= 600 (the triangle is
    // exactly the quadrant x,y>=0 intersected with that half-plane). Per-tile x0+y0 (tile edge is
    // 256): (0,0)=0 (0,1)=256 (0,2)=512 (1,0)=256 (1,1)=512 (1,2)=768 (2,0)=512 (2,1)=768
    // (2,2)=1024 -- every included/excluded tile clears the <=600 threshold by at least 88 (not a
    // boundary tangent), so (1,2),(2,1),(2,2) are cleanly excluded.
    auto factory = makeSetFactory();
    const ProjectionPtr projection = atZoom(factory, kProjEpsgGoogle, 5);

    const DoubleRect bounds{0, 0, 600, 600};
    const GeometryProjectedContourPtr outer =
        std::make_shared<const GeometryProjectedContour>(bounds, makePoints({{0, 0}, {600, 0}, {0, 600}}));
    const auto polygon = std::make_shared<const GeometryProjectedSinglePolygon>(
        bounds, outer, std::vector<GeometryProjectedContourPtr>{});

    TileIteratorByPolygon iter(projection, polygon);
    EXPECT_EQ(iter.scanOrder(), ScanOrder::Cols);
    ASSERT_EQ(iter.tilesRect().rect(), (RectI{0, 0, 3, 3}));

    const std::vector<PointI> expected = {{0, 0}, {0, 1}, {0, 2}, {1, 0}, {1, 1}, {2, 0}};
    EXPECT_EQ(collectAll(iter), expected);
    EXPECT_EQ(iter.tilesTotal(), static_cast<qint64>(expected.size()));
}
