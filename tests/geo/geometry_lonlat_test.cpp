// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-009, FEAT-GEO-010, FEAT-GEO-012. Ports Test/u_VectorItmesFactorySimple_Test.pas
// (LonLat suites only -- the Projected builder suites need u_GeometryProjectedFactory.pas,
// T-0017C scope, see docs/handoffs/T-0017A.md) and docs/specs/geo.md §11.2 geometry_lonlat_test.cpp
// (8 new), plus 1 more added in Rework 1 (docs/handoffs/T-0017A-review.md F2c) -- 15 cases total.
// Plus 3 more (T-0017B, not in that ticket's 33-case acceptance) for the createLineByStream /
// createPolygonByLine / createPolygonByLineAndFilter factory methods added by that ticket.

#include "cg/core/platform/qt_compat.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/geo/point_stream.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type_factory.h"

#include <QDateTime>

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <span>
#include <vector>

#include "geo_test_util.h"

using cg::geo::DatumFactory;
using cg::geo::DoublePoint;
using cg::geo::GeometryLonLatFactory;
using cg::geo::HashSeed;
using cg::geo::IGeometryLonLatMultiLine;
using cg::geo::IGeometryLonLatMultiPolygon;
using cg::geo::IGeometryLonLatPolygon;
using cg::geo::IGeometryLonLatSingleLine;
using cg::geo::IGeometryLonLatSinglePolygon;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::LonLat;
using cg::geo::LonLatPointsPtr;
using cg::geo::LonLatStream;
using cg::geo::PointsMeta;
using cg::geo::pointsStream;
using cg::geo::ProjectionPtr;
using cg::geo::ProjectionSetFactory;

namespace {
LonLatPointsPtr makePoints(std::vector<LonLat> pts) {
    return std::make_shared<const std::vector<LonLat>>(std::move(pts));
}

// Legacy IEnumLonLatPoint over a closed contour: the port's singleLineStream(closed=true) over the same
// coordinates (a LonLat is a DoublePoint-shaped pair; see GeometryLonLatFactory::createLineByStream).
std::vector<DoublePoint> drainClosed(std::span<const LonLat> pts) {
    std::vector<DoublePoint> raw;
    for (const LonLat& p : pts) {
        raw.push_back(p.toPoint());
    }
    auto stream = cg::geo::singleLineStream(raw, nullptr, true);
    std::vector<DoublePoint> out;
    DoublePoint p;
    while (stream->next(p)) {
        out.push_back(p);
    }
    return out;
}
} // namespace

// --- 12. u_VectorItmesFactorySimple_Test.pas -> ported (LonLat suites: 6 of the legacy 12) -----

TEST(GeometryLonLatLineBuilder, NoLines) {
    GeometryLonLatFactory factory;
    auto builder = factory.makeLineBuilder();
    EXPECT_EQ(builder.makeStaticAndClear(), nullptr);
}

TEST(GeometryLonLatLineBuilder, Simple) {
    GeometryLonLatFactory factory;
    auto builder = factory.makeLineBuilder();
    builder.addLine(makePoints({{0, 1}, {1, 1}, {1, 0}}));
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatSingleLine>(builder.makeStaticAndClear());
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->points().size(), 3u);
    EXPECT_EQ(result->points()[0], (LonLat{0, 1}));
    EXPECT_EQ(result->points()[1], (LonLat{1, 1}));
    EXPECT_EQ(result->points()[2], (LonLat{1, 0}));
}

TEST(GeometryLonLatLineBuilder, TwoLines) {
    GeometryLonLatFactory factory;
    auto builder = factory.makeLineBuilder();
    builder.addLine(makePoints({{0, 1}}));
    builder.addLine(makePoints({{1, 1}, {1, 0}}));
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatMultiLine>(builder.makeStaticAndClear());
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->count(), 2u);
    EXPECT_EQ(result->item(0)->points().size(), 1u);
    EXPECT_EQ(result->item(0)->points()[0], (LonLat{0, 1}));
    EXPECT_EQ(result->item(1)->points().size(), 2u);
    EXPECT_EQ(result->item(1)->points()[0], (LonLat{1, 1}));
    EXPECT_EQ(result->item(1)->points()[1], (LonLat{1, 0}));
}

TEST(GeometryLonLatPolygonBuilder, NoLines) {
    GeometryLonLatFactory factory;
    auto builder = factory.makePolygonBuilder();
    EXPECT_EQ(builder.makeStaticAndClear(), nullptr);
}

TEST(GeometryLonLatPolygonBuilder, Simple) {
    GeometryLonLatFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(makePoints({{0, 1}, {1, 1}, {1, 0}}));
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatSinglePolygon>(builder.makeStaticAndClear());
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->holeCount(), 0u);
    // Contours are stored OPEN (docs/specs/geo.md §3.2 point 2): points() is the 3 input points;
    // the legacy 4th point comes from the closing enumeration, asserted below (T-0040).
    ASSERT_EQ(result->outerBorder()->points().size(), 3u);
    EXPECT_EQ(result->outerBorder()->points()[0], (LonLat{0, 1}));
    EXPECT_EQ(result->outerBorder()->points()[1], (LonLat{1, 1}));
    EXPECT_EQ(result->outerBorder()->points()[2], (LonLat{1, 0}));
    const auto en = drainClosed(result->outerBorder()->points());
    ASSERT_EQ(en.size(), 4u); // legacy: p0, p1, p2, p0, then end
    EXPECT_EQ(en[0], (DoublePoint{0, 1}));
    EXPECT_EQ(en[1], (DoublePoint{1, 1}));
    EXPECT_EQ(en[2], (DoublePoint{1, 0}));
    EXPECT_EQ(en[3], (DoublePoint{0, 1}));
}

TEST(GeometryLonLatPolygonBuilder, TwoLines) {
    GeometryLonLatFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(makePoints({{0, 1}}));
    builder.addOuter(makePoints({{1, 1}, {1, 0}}));
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatMultiPolygon>(builder.makeStaticAndClear());
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->count(), 2u);
    EXPECT_EQ(result->item(0)->holeCount(), 0u);
    EXPECT_EQ(result->item(0)->outerBorder()->points().size(), 1u);
    EXPECT_EQ(result->item(0)->outerBorder()->points()[0], (LonLat{0, 1}));
    EXPECT_EQ(result->item(1)->outerBorder()->points().size(), 2u);
    EXPECT_EQ(result->item(1)->outerBorder()->points()[0], (LonLat{1, 1}));
    EXPECT_EQ(result->item(1)->outerBorder()->points()[1], (LonLat{1, 0}));
    // Legacy closing enumeration: no closing point for the 1-point contour, p0 third for the 2-point one.
    const auto e0 = drainClosed(result->item(0)->outerBorder()->points());
    ASSERT_EQ(e0.size(), 1u);
    EXPECT_EQ(e0[0], (DoublePoint{0, 1}));
    const auto e1 = drainClosed(result->item(1)->outerBorder()->points());
    ASSERT_EQ(e1.size(), 3u);
    EXPECT_EQ(e1[0], (DoublePoint{1, 1}));
    EXPECT_EQ(e1[1], (DoublePoint{1, 0}));
    EXPECT_EQ(e1[2], (DoublePoint{1, 1}));
}

// --- new coverage: docs/specs/geo.md §11.2 geometry_lonlat_test.cpp (8 cases) ------------------

TEST(GeometryLonLat, ContourIsAlsoAPolygon) {
    // i_GeometryLonLat.pas:99 -- a bare ring satisfies a dynamic_pointer_cast<IGeometryLonLatPolygon>.
    GeometryLonLatFactory factory;
    auto polygon = factory.createPolygonByRect({0, 1, 1, 0});
    auto asPolygon = std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(polygon->outerBorder());
    EXPECT_NE(asPolygon, nullptr);
}

TEST(GeometryLonLat, ContourStoresOpenRing) {
    GeometryLonLatFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(makePoints({{0, 1}, {1, 1}, {1, 0}}));
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatSinglePolygon>(builder.makeStaticAndClear());
    ASSERT_NE(result, nullptr);
    const auto pts = result->outerBorder()->points();
    ASSERT_EQ(pts.size(), 3u); // never normalised by appending points()[0]
    EXPECT_EQ(pts.back(), (LonLat{1, 0}));
}

TEST(GeometryLonLat, MetaOnlyOnSingleLine) {
    GeometryLonLatFactory factory;
    auto builder = factory.makeLineBuilder();
    PointsMeta meta;
    meta.elevation = {1.0, 2.0};
    builder.addLine(makePoints({{0, 0}, {1, 1}}), meta);
    auto line = std::dynamic_pointer_cast<const IGeometryLonLatSingleLine>(builder.makeStaticAndClear());
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE(line->meta().hasElevation());
    EXPECT_EQ(line->meta().elevation, (std::vector<double>{1.0, 2.0}));
    EXPECT_FALSE(line->meta().hasTimestamp());
    // IGeometryLonLatContour and IGeometryLonLatMultiPoint simply have no meta() member at all --
    // a compile-time guarantee (docs/specs/geo.md §3.2 point 3), not a runtime one.
}

TEST(GeometryLonLat, MetaSliceMatchesCopyMeta) {
    PointsMeta meta;
    meta.elevation = {10.0, 20.0, 30.0, 40.0};
    meta.timestamp = {cg::core::platform::utcFromMSecsSinceEpoch(0),
                      cg::core::platform::utcFromMSecsSinceEpoch(60'000),
                      cg::core::platform::utcFromMSecsSinceEpoch(120'000),
                      cg::core::platform::utcFromMSecsSinceEpoch(180'000)};
    const PointsMeta sliced = meta.slice(1, 2);
    EXPECT_EQ(sliced.elevation, (std::vector<double>{20.0, 30.0}));
    ASSERT_EQ(sliced.timestamp.size(), 2u);
    EXPECT_EQ(sliced.timestamp[0], meta.timestamp[1]);
    EXPECT_EQ(sliced.timestamp[1], meta.timestamp[2]);
}

TEST(GeometryLonLat, HashIsEagerAndStable) {
    GeometryLonLatFactory factory;
    auto point = factory.createPoint({10, 20});
    const auto h1 = point->hash();
    const auto h2 = point->hash();
    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, cg::core::kNoHash);
}

TEST(GeometryLonLat, BoundsAreEagerAndStable) {
    GeometryLonLatFactory factory;
    const std::vector<LonLat> pts = {{0, 0}, {2, 2}, {1, 1}};
    auto line = factory.createLine(pts, nullptr);
    const auto b1 = line->bounds();
    const auto b2 = line->bounds();
    EXPECT_EQ(b1, b2);
    EXPECT_EQ(b1.left, 0.0);
    EXPECT_EQ(b1.right, 2.0);
    EXPECT_EQ(b1.top, 2.0);
    EXPECT_EQ(b1.bottom, 0.0);
}

TEST(GeometryLonLat, IsSameGeometryShortCircuitsOnHash) {
    GeometryLonLatFactory factory;
    auto a = factory.createPoint({1, 2});
    auto b = factory.createPoint({1, 2}); // same coords -> same hash, deep-equal too
    auto c = factory.createPoint({3, 4}); // different hash -> false without a deep compare
    EXPECT_TRUE(a->isSameGeometry(*a));   // identity fast path
    EXPECT_TRUE(a->isSameGeometry(*b));
    EXPECT_TRUE(b->isSameGeometry(*a));
    EXPECT_FALSE(a->isSameGeometry(*c));
}

TEST(GeometryLonLat, CreateLineSplitsOnSeparator) {
    GeometryLonLatFactory factory;
    const std::vector<LonLat> pts = {
        {0, 1}, {1, 1}, {1, 0}, cg::geo::LonLat::fromPoint(cg::geo::emptyDoublePoint()),
        {2, 2}, {3, 3}, {4, 4}};
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatMultiLine>(factory.createLine(pts, nullptr));
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->count(), 2u);
    EXPECT_EQ(result->item(0)->points().size(), 3u);
    EXPECT_EQ(result->item(1)->points().size(), 3u);
    EXPECT_EQ(result->item(1)->points()[0], (LonLat{2, 2}));
}

// T-0110: a point with a finite X and a NaN Y is empty, as legacy PointIsEmpty tests X or Y
// (u_GeoFunc.pas:552), so CreateLonLatLine (u_GeometryLonLatFactory.pas:945) splits the line there.
TEST(GeometryLonLat, CreateLineSplitsOnPointWithNanYOnly) {
    GeometryLonLatFactory factory;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<LonLat> pts = {{0, 1}, {1, 1}, {1, nan}, {2, 2}, {3, 3}};
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatMultiLine>(factory.createLine(pts, nullptr));
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->count(), 2u);
    EXPECT_EQ(result->item(0)->points().size(), 2u);
    EXPECT_EQ(result->item(1)->points().size(), 2u);
    EXPECT_EQ(result->item(1)->points()[0], (LonLat{2, 2}));
}

// T-0110: the same split for polygons (CreateLonLatPolygon, u_GeometryLonLatFactory.pas:1063).
TEST(GeometryLonLat, CreatePolygonSplitsOnPointWithNanYOnly) {
    GeometryLonLatFactory factory;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<LonLat> pts = {{0, 1}, {1, 1}, {1, 0}, {5, nan}, {2, 2}, {3, 3}, {2, 3}};
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatMultiPolygon>(factory.createPolygon(pts));
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->count(), 2u);
    EXPECT_EQ(result->item(0)->outerBorder()->points().size(), 3u);
    EXPECT_EQ(result->item(1)->outerBorder()->points().size(), 3u);
    EXPECT_EQ(result->item(1)->outerBorder()->points()[0], (LonLat{2, 2}));
}

// --- Rework 1: docs/handoffs/T-0017A-review.md F2c / deviations audit --------------------------

TEST(GeometryLonLatPolygonBuilder, AddHoleBeforeAddOuterBecomesOuter) {
    // u_GeometryLonLatFactory.pas:493-511's FOuterDataExists quirk: a hole added before any outer
    // silently becomes the first outer instead. Reproduced per Tech Lead ruling (cheap to match
    // legacy rather than record a deviation).
    GeometryLonLatFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addHole(makePoints({{0, 1}, {1, 1}, {1, 0}}));
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatSinglePolygon>(builder.makeStaticAndClear());
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->holeCount(), 0u);
    ASSERT_EQ(result->outerBorder()->points().size(), 3u);
    EXPECT_EQ(result->outerBorder()->points()[0], (LonLat{0, 1}));
    EXPECT_EQ(result->outerBorder()->points()[1], (LonLat{1, 1}));
    EXPECT_EQ(result->outerBorder()->points()[2], (LonLat{1, 0}));
}

// --- T-0017B: createLineByStream / createPolygonByLineAndFilter / createPolygonByLine ----------
// Not in the ticket's 33-case acceptance (that budget is point_stream_test.cpp only), but these
// are new non-trivial factory methods -- a minimal smoke test each, per the coding standard's
// "every new .cpp gets tests" (docs/handoffs/T-0017A-review.md F6 precedent).

TEST(GeometryLonLatFactoryStream, CreateLineByStreamDrainsAndSplitsOnSeparator) {
    GeometryLonLatFactory factory;
    std::vector<DoublePoint> data{{0, 1}, {1, 1}, {1, 0}, cg::geo::emptyDoublePoint(), {2, 2}, {3, 3}};
    std::vector<LonLat> scratch;
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatMultiLine>(
        factory.createLineByStream(LonLatStream(pointsStream(data)), scratch));
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->count(), 2u);
    EXPECT_EQ(result->item(0)->points().size(), 3u);
    EXPECT_EQ(result->item(1)->points().size(), 2u);
    EXPECT_EQ(result->item(1)->points()[0], (LonLat{2, 2}));
}

TEST(GeometryLonLatFactoryStream, CreatePolygonByLineAndFilterAppliesFilterPerSubLine) {
    // An identity filter: the outer ring comes back exactly as the source line's points.
    GeometryLonLatFactory factory;
    auto lineBuilder = factory.makeLineBuilder();
    lineBuilder.addLine(makePoints({{0, 1}, {1, 1}, {1, 0}}));
    auto line =
        std::dynamic_pointer_cast<const cg::geo::IGeometryLonLatLine>(lineBuilder.makeStaticAndClear());
    ASSERT_NE(line, nullptr);

    cg::geo::LonLatPointFilter identity = [](LonLatStream s) { return s; };
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatSinglePolygon>(
        factory.createPolygonByLineAndFilter(*line, identity));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->holeCount(), 0u);
    ASSERT_EQ(result->outerBorder()->points().size(), 3u);
    EXPECT_EQ(result->outerBorder()->points()[0], (LonLat{0, 1}));
    EXPECT_EQ(result->outerBorder()->points()[2], (LonLat{1, 0}));
}

TEST(GeometryLonLatFactoryStream, CreatePolygonByLineUsesLine2PolyPerD3) {
    // D-3 (docs/specs/geo.md §10/§3.5.2): createPolygonByLine always buffers via line2Poly.
    // Cross-check against point_stream_test.cpp's Line2Poly.TestTwoPoints fixture: the outer
    // ring is that 5-point sequence with the trailing closing duplicate trimmed away (4 points).
    auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    ProjectionSetFactory setFactory{HashSeed{}, datums};
    ProjectionPtr projection = setFactory.byCode(kProjEpsgGoogle, kTileSplitQuadrate256x256)->zoom(10);

    GeometryLonLatFactory factory;
    auto lineBuilder = factory.makeLineBuilder();
    lineBuilder.addLine(makePoints({{10, 10}, {11, 11}}));
    auto line =
        std::dynamic_pointer_cast<const cg::geo::IGeometryLonLatLine>(lineBuilder.makeStaticAndClear());
    ASSERT_NE(line, nullptr);

    std::vector<DoublePoint> scratch;
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatSinglePolygon>(
        factory.createPolygonByLine(*line, 1000.0, *projection, scratch));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->holeCount(), 0u);
    ASSERT_EQ(result->outerBorder()->points().size(), 4u);
    const LonLat first = result->outerBorder()->points()[0];
    EXPECT_TRUE(cg::geo::test::DoublePointsEqual({10.000109176, 9.9872758672}, first.toPoint(), 1e-7));
}
