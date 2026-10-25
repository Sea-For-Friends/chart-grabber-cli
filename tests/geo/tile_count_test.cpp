// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-015. Ports Test/u_CalcTilesInPolygon_Test.pas (4 cases, exact legacy fixture
// and constants) plus, per the test policy, a minimal "otherwise" set: the rectangle-shortcut
// vertex-order rule, the multi-polygon double-count quirk (hand-derived, not from the DUnit
// suite), the factory's rect-vs-polygon dispatch, and the -1-sentinel -> nullopt mapping.

#include "cg/geo/datum.h"
#include "cg/geo/geo_calc.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_projected.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/geo/tile_count.h"

#include <gtest/gtest.h>

#include <memory>
#include <numbers>
#include <vector>

using cg::geo::DatumFactory;
using cg::geo::DoublePoint;
using cg::geo::DoubleRect;
using cg::geo::GeometryProjectedContour;
using cg::geo::GeometryProjectedContourPtr;
using cg::geo::GeometryProjectedFactory;
using cg::geo::GeometryProjectedSinglePolygon;
using cg::geo::GeometryProjectedSinglePolygonPtr;
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
using cg::geo::TileIteratorFactory;

namespace {

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

GeometryProjectedSinglePolygonPtr makeSingle(const DoubleRect& bounds, std::vector<DoublePoint> outer,
                                             std::vector<std::vector<DoublePoint>> holes = {}) {
    GeometryProjectedFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(bounds, makePoints(std::move(outer)));
    for (auto& hole : holes) {
        // Bounds IS used: the hole contour's checkRectIntersection reads bounds_
        // (geometry_projected.cpp:310-313). Legacy TestSimplePolygonWithHole
        // (Test/u_CalcTilesInPolygon_Test.pas) passes the literal
        // DoubleRect(256*0.9, 256*0.9, 256*2.1, 256*2.1); reproduced here exactly rather than
        // re-derived from the hole's own points.
        const DoubleRect holeBounds{256 * 0.9, 256 * 0.9, 256 * 2.1, 256 * 2.1};
        builder.addHole(holeBounds, makePoints(std::move(hole)));
    }
    return std::static_pointer_cast<const GeometryProjectedSinglePolygon>(builder.makeStaticAndClear());
}

} // namespace

// --- 10. u_CalcTilesInPolygon_Test.pas -> ported (4 cases) ---------------------------------------
// Fixture: Projection(hash 0, type = byCode(3857), zoom = 20) -- Test/u_CalcTilesInPolygon_Test.pas
// SetUp. Legacy constants (256 * N pixel coordinates) reproduced exactly.

class CalcTilesInPolygon : public ::testing::Test {
protected:
    ProjectionSetFactory factory_ = makeSetFactory();
    ProjectionPtr projection_ = atZoom(factory_, kProjEpsgGoogle, 20);
};

TEST_F(CalcTilesInPolygon, TestSimpleRect) {
    const auto polygon = makeSingle(DoubleRect{0, 0, 256 * 10, 256 * 50},
                                    {{0, 0}, {256 * 10, 0}, {256 * 10, 256 * 50}, {0, 256 * 50}});
    EXPECT_EQ(cg::geo::calcTileCountInProjectedPolygon(*projection_, *polygon), 500);
}

TEST_F(CalcTilesInPolygon, TestSimplePolygon) {
    const auto polygon =
        makeSingle(DoubleRect{0, 0, 256 * 9.9, 256 * 49.9},
                   {{0, 0}, {256 * 9.9, 0}, {256 * 9.9, 256 * 49.9}, {256 * 3, 256 * 10}, {0, 256 * 49.9}});
    EXPECT_EQ(cg::geo::calcTileCountInProjectedPolygon(*projection_, *polygon), 345);
}

TEST_F(CalcTilesInPolygon, TestSimplePolygonWithHole) {
    const auto polygon = makeSingle(
        DoubleRect{0, 0, 256 * 9.9, 256 * 49.9},
        {{0, 0}, {256 * 9.9, 0}, {256 * 9.9, 256 * 49.9}, {256 * 3, 256 * 10}, {0, 256 * 49.9}},
        {{{256 * 0.9, 256 * 0.9}, {256 * 2.1, 256 * 0.9}, {256 * 2.1, 256 * 2.1}, {256 * 0.9, 256 * 2.1}}});
    EXPECT_EQ(cg::geo::calcTileCountInProjectedPolygon(*projection_, *polygon), 344);
}

TEST_F(CalcTilesInPolygon, TestMultiPolygon) {
    GeometryProjectedFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(
        DoubleRect{0, 0, 256 * 9.9, 256 * 49.9},
        makePoints({{0, 0}, {256 * 9.9, 0}, {256 * 9.9, 256 * 49.9}, {256 * 3, 256 * 10}, {0, 256 * 49.9}}));
    builder.addOuter(DoubleRect{256 * 20.1, 256 * 20.1, 256 * 20.9, 256 * 20.9},
                     makePoints({{256 * 20.1, 256 * 20.1},
                                 {256 * 20.9, 256 * 20.1},
                                 {256 * 20.9, 256 * 20.9},
                                 {256 * 20.1, 256 * 20.9}}));
    const auto polygon = builder.makeStaticAndClear();
    EXPECT_EQ(cg::geo::calcTileCountInProjectedPolygon(*projection_, *polygon), 346);
}

// --- minimal "otherwise" coverage ----------------------------------------------------------------

TEST(TryProjectedPolygonToTileRect, AcceptsBothVertexOrders) {
    auto factory = makeSetFactory();
    const ProjectionPtr projection = atZoom(factory, kProjEpsgGoogle, 10);

    // Clockwise (TopLeft, TopRight, BottomRight, BottomLeft) -- tile-aligned, 2x3 tiles.
    const auto cw = makeSingle(DoubleRect{0, 0, 512, 768}, {{0, 0}, {512, 0}, {512, 768}, {0, 768}});
    const auto cwRect = cg::geo::tryProjectedPolygonToTileRect(*projection, *cw);
    ASSERT_TRUE(cwRect.has_value());
    EXPECT_EQ(*cwRect, (RectI{0, 0, 2, 3}));

    // Counter-clockwise (TopLeft, BottomLeft, BottomRight, TopRight) -- same rect, other winding.
    const auto ccw = makeSingle(DoubleRect{0, 0, 512, 768}, {{0, 0}, {0, 768}, {512, 768}, {512, 0}});
    const auto ccwRect = cg::geo::tryProjectedPolygonToTileRect(*projection, *ccw);
    ASSERT_TRUE(ccwRect.has_value());
    EXPECT_EQ(*ccwRect, (RectI{0, 0, 2, 3}));
}

TEST(TryProjectedPolygonToTileRect, RejectsHoles) {
    auto factory = makeSetFactory();
    const ProjectionPtr projection = atZoom(factory, kProjEpsgGoogle, 10);
    const auto withHole = makeSingle(DoubleRect{0, 0, 512, 768}, {{0, 0}, {512, 0}, {512, 768}, {0, 768}},
                                     {{{10, 10}, {20, 10}, {20, 20}, {10, 20}}});
    EXPECT_FALSE(cg::geo::tryProjectedPolygonToTileRect(*projection, *withHole).has_value());
}

TEST(CalcTileCountInProjectedPolygon, MultiPolygonOverlapDoubleCounts) {
    // Hand-derived: two identical axis-aligned single-tile rectangles (both exactly tile [2,3] in
    // pixel space at this zoom -- 256*2..256*3 on each axis), combined into one multi-polygon. Each
    // part independently takes the TryProjectedPolygonToTileRect shortcut and reports area 1, and
    // calcTileCountInProjectedPolygon (u_GeometryFunc.pas:802-823) is a PLAIN SUM over parts, so
    // the two identical, fully-overlapping tiles are counted twice: 1 + 1 = 2, even though only one
    // distinct tile exists (docs/parity/deviations.md D-5).
    auto factory = makeSetFactory();
    const ProjectionPtr projection = atZoom(factory, kProjEpsgGoogle, 10);

    GeometryProjectedFactory geomFactory;
    auto builder = geomFactory.makePolygonBuilder();
    const DoubleRect tileBounds{256 * 2, 256 * 2, 256 * 3, 256 * 3};
    const std::vector<DoublePoint> tileCorners = {
        {256 * 2, 256 * 2}, {256 * 3, 256 * 2}, {256 * 3, 256 * 3}, {256 * 2, 256 * 3}};
    builder.addOuter(tileBounds, makePoints(tileCorners));
    builder.addOuter(tileBounds, makePoints(tileCorners));
    const auto polygon = builder.makeStaticAndClear();

    EXPECT_EQ(cg::geo::calcTileCountInProjectedPolygon(*projection, *polygon), 2);
}

TEST(TileIteratorFactory, PicksRectIteratorForFullExtentRectangleAndPolygonOtherwise) {
    auto factory = makeSetFactory();
    const ProjectionPtr projection = atZoom(factory, kProjEpsgGoogle, 2);
    const auto geomFactory = std::make_shared<const GeometryProjectedFactory>();
    const TileIteratorFactory tileFactory(geomFactory);

    // A lon/lat rectangle built from this projection's own full tile extent reprojects back to
    // exactly that extent (tileRect2LonLatRect and the forward lon/lat->pixel conversion are exact
    // inverses at a tile-grid corner), so it hits the TryProjectedPolygonToTileRect shortcut
    // deterministically -- not by luck of hand-picked coordinates.
    const RectI fullTileRect = projection->tileRect();
    const cg::geo::LonLatRect llRect = projection->tileRect2LonLatRect(fullTileRect);
    const std::vector<cg::geo::LonLat> rectCorners = {{llRect.left, llRect.top},
                                                      {llRect.right, llRect.top},
                                                      {llRect.right, llRect.bottom},
                                                      {llRect.left, llRect.bottom}};

    cg::geo::GeometryLonLatPolygonBuilder rectBuilder;
    rectBuilder.addOuter(llRect, std::make_shared<const std::vector<cg::geo::LonLat>>(rectCorners));
    const auto rectPolygon =
        std::static_pointer_cast<const cg::geo::IGeometryLonLatPolygon>(rectBuilder.makeStaticAndClear());

    const auto rectIter = tileFactory.makeTileIterator(projection, rectPolygon);
    EXPECT_EQ(rectIter->scanOrder(), ScanOrder::Rows);
    EXPECT_EQ(rectIter->tilesRect().rect(), fullTileRect);

    // A triangle never satisfies the exactly-4-vertex rectangle shortcut, regardless of
    // coordinates, so it always falls through to the column-major polygon iterator.
    cg::geo::GeometryLonLatPolygonBuilder triBuilder;
    triBuilder.addOuter(llRect,
                        std::make_shared<const std::vector<cg::geo::LonLat>>(
                            std::vector<cg::geo::LonLat>{rectCorners[0], rectCorners[1], rectCorners[3]}));
    const auto trianglePolygon =
        std::static_pointer_cast<const cg::geo::IGeometryLonLatPolygon>(triBuilder.makeStaticAndClear());

    const auto triIter = tileFactory.makeTileIterator(projection, trianglePolygon);
    EXPECT_EQ(triIter->scanOrder(), ScanOrder::Cols);
}

TEST(TileIteratorFactory, NulloptStartPointMapsToTopLeftLikeLegacyNegativeOneSentinel) {
    // Legacy ITileIteratorFactory.MakeTileIterator takes AStartPointX/AStartPointY, Integer,
    // defaulting to -1, and treats *either* being -1 as "no start point" -> TopLeft
    // (u_TileIteratorByRect.pas:116, u_TileIteratorByPolygon.pas:126). The port folds both into
    // one std::optional<PointI>; std::nullopt (the default) must reproduce that TopLeft default,
    // exactly like TileIteratorByRect/TileIteratorByPolygon's own startPoint.value_or(topLeft).
    TileIteratorByRect rectIter(cg::geo::TileRect(cg::geo::ProjectionPtr(), RectI{2, 5, 6, 8}));
    PointI first;
    ASSERT_TRUE(rectIter.next(first));
    EXPECT_EQ(first, (PointI{2, 5})); // rect.topLeft(), not (-1,-1) or any other sentinel value.
}

// --- T-0018B rework regression (docs/handoffs/T-0018B-review.md findings 1 and 2) ---------------

TEST(T0018BRework, NullProjectedPolygonEmptyIteratorAndTwoPointPerimeterDoubled) {
    // F1: TileIteratorFactory::makeTileIterator falls through to
    // TileIteratorByPolygon(projection, projected, ...) unguarded whenever
    // GeometryProjectedFactory::createPolygonByLonLatPolygon returns nullptr (no ring survives
    // projection, geometry_factory.cpp's makeStaticAndClear/collapseProjectedPolygons -- mirrors
    // legacy u_GeometryProjectedFactory.pas:272). The exact contract under test is
    // TileIteratorByPolygon's own handling of that null pointer -- the object makeTileIterator
    // constructs in that fallthrough (tile_count.cpp:199-207) -- which must be an EMPTY iterator
    // (tilesTotal() == 0, next() == false), no crash, exactly like legacy
    // u_TileIteratorFactory.pas:91's Supports(nil) == False fallthrough to
    // TTileIteratorByPolygon.Create(AProjection, nil, ...) -> Assert(False); MakeEmptySetup (a
    // no-op in every legacy build, SASPlanet.dproj DCC_AssertionsAtRuntime=False).
    {
        auto factory = makeSetFactory();
        const ProjectionPtr projection = atZoom(factory, kProjEpsgGoogle, 10);
        const std::shared_ptr<const cg::geo::IGeometryProjectedPolygon> nullProjected;

        TileIteratorByPolygon iterator(projection, nullProjected);
        EXPECT_EQ(iterator.tilesTotal(), 0);
        PointI tile;
        EXPECT_FALSE(iterator.next(tile));
    }

    // F2: a 2-point contour's perimeter must be 2*d. Legacy's closed enumerator
    // (u_EnumDoublePointBySingleLine.pas:142, "FClosed and (FCount > 1)") emits the closing point
    // for a 2-point contour, giving CalcContourPerimeter (u_GeoCalc.pas:167-181) dist(p0,p1) +
    // dist(p1,p0) = 2d -- NOT Datum::calcPolygonPerimeter's own stricter size() > 2 guard
    // (datum.cpp:222), which would give just d.
    {
        DatumFactory datums{HashSeed{}};
        constexpr double kSphereRadius = 6371000.0;
        const cg::geo::GeoCalc calc(datums.byRadius(kSphereRadius, kSphereRadius));

        const auto outer = std::make_shared<const cg::geo::GeometryLonLatContour>(
            cg::core::kNoHash, cg::geo::LonLatRect{},
            std::make_shared<const std::vector<cg::geo::LonLat>>(
                std::vector<cg::geo::LonLat>{{0, 0}, {90, 0}}));
        const cg::geo::GeometryLonLatSinglePolygon polygon(cg::core::kNoHash, cg::geo::LonLatRect{}, outer,
                                                           std::vector<cg::geo::GeometryLonLatContourPtr>{});

        // Two points 90 degrees apart on a perfect sphere: great-circle distance is exactly
        // R * pi / 2, independent of this port's own code (elementary spherical geometry).
        const double distance = kSphereRadius * std::numbers::pi / 2.0;
        EXPECT_NEAR(calc.calcPolygonPerimeter(polygon), 2.0 * distance, 1.0);
    }
}
