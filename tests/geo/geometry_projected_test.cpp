// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-013. Ports Test/u_ProjectedPolygonWithRect_Test.pas (14 cases, R01-R14, all
// legacy constants transcribed verbatim) plus CalcAreaUsesLegacyShoelaceForm, which pins
// §3.3.1's `(x1+x2)*(y1-y2)` shoelace form (both T-0017C1). T-0017C2 adds GeometryProjectedFactory
// and GeometryProjectedProvider coverage below (no legacy DUnit test exercises either directly --
// projected_calc_test.cpp covers projected_calc.h).

#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_projected.h"
#include "cg/geo/point_stream.h"
#include "cg/geo/projection_set.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <span>
#include <vector>

using cg::geo::DatumFactory;
using cg::geo::DoublePoint;
using cg::geo::DoubleRect;
using cg::geo::GeometryLonLatFactory;
using cg::geo::GeometryProjectedContour;
using cg::geo::GeometryProjectedContourPtr;
using cg::geo::GeometryProjectedFactory;
using cg::geo::GeometryProjectedMultiPolygon;
using cg::geo::GeometryProjectedProvider;
using cg::geo::GeometryProjectedSinglePolygon;
using cg::geo::GeometryProjectedSinglePolygonPtr;
using cg::geo::HashSeed;
using cg::geo::IGeometryLonLatLine;
using cg::geo::IGeometryLonLatPolygon;
using cg::geo::IGeometryProjectedLine;
using cg::geo::IGeometryProjectedPolygon;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::LonLat;
using cg::geo::ProjectedPointsPtr;
using cg::geo::ProjectionPtr;
using cg::geo::ProjectionSetFactory;
using cg::geo::RectWithPolygonIntersection;

namespace {

using RWP = RectWithPolygonIntersection;

ProjectedPointsPtr makePoints(std::vector<DoublePoint> pts) {
    return std::make_shared<const std::vector<DoublePoint>>(std::move(pts));
}

// u_GeoFunc.pas:362-375 ProjectedMBRByPoints -- used only to build this test's fixture; the real
// factory (GeometryProjectedFactory) is T-0017C2.
DoubleRect mbrByPoints(const std::vector<DoublePoint>& pts) {
    DoubleRect r{pts[0].x(), pts[0].y(), pts[0].x(), pts[0].y()};
    for (std::size_t i = 1; i < pts.size(); ++i) {
        r.left = std::min(r.left, pts[i].x());
        r.top = std::min(r.top, pts[i].y());
        r.right = std::max(r.right, pts[i].x());
        r.bottom = std::max(r.bottom, pts[i].y());
    }
    return r;
}

// u_GeoFunc.pas:298-... UnionProjectedRects.
DoubleRect unionRect(const DoubleRect& a, const DoubleRect& b) {
    return {std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right),
            std::max(a.bottom, b.bottom)};
}

} // namespace

// --- 9. u_ProjectedPolygonWithRect_Test.pas ------------------------------------------------
// Fixture (u_ProjectedPolygonWithRect_Test.pas:62-132): outer #1 (10 pts) with one hole (9 pts),
// outer #2 (4 pts, no hole), assembled the way TGeometryProjectedPolygonBuilder does --
// AddOuter/AddHole/AddOuter, single-polygon bounds = the OUTER ring's own MBR (not unioned with
// its hole), multi-polygon bounds = union of each single polygon's bounds
// (u_GeometryProjectedFactory.pas:224-246,270-283).
class ProjectedPolygonWithRect : public ::testing::Test {
protected:
    void SetUp() override {
        const std::vector<DoublePoint> outer1 = {{50, 40},  {220, 40},  {160, 80},  {230, 140}, {230, 80},
                                                 {280, 80}, {280, 240}, {170, 240}, {170, 180}, {50, 240}};
        const std::vector<DoublePoint> hole1 = {{70, 60},   {90, 80},  {110, 50}, {130, 80}, {110, 80},
                                                {130, 120}, {90, 130}, {70, 160}, {80, 90}};
        const std::vector<DoublePoint> outer2 = {{420, 40}, {480, 40}, {480, 240}, {420, 240}};

        const DoubleRect outer1Bounds = mbrByPoints(outer1);
        const DoubleRect hole1Bounds = mbrByPoints(hole1);
        const DoubleRect outer2Bounds = mbrByPoints(outer2);

        auto outer1Contour =
            std::make_shared<const GeometryProjectedContour>(outer1Bounds, makePoints(outer1));
        auto hole1Contour = std::make_shared<const GeometryProjectedContour>(hole1Bounds, makePoints(hole1));
        auto outer2Contour =
            std::make_shared<const GeometryProjectedContour>(outer2Bounds, makePoints(outer2));

        auto main = std::make_shared<const GeometryProjectedSinglePolygon>(
            outer1Bounds, outer1Contour, std::vector<GeometryProjectedContourPtr>{hole1Contour});
        auto second = std::make_shared<const GeometryProjectedSinglePolygon>(
            outer2Bounds, outer2Contour, std::vector<GeometryProjectedContourPtr>{});

        multi_ = std::make_shared<const GeometryProjectedMultiPolygon>(
            unionRect(outer1Bounds, outer2Bounds),
            std::vector<GeometryProjectedSinglePolygonPtr>{main, second});
        main_ = main;
        mainOuter_ = outer1Contour;
        mainHole_ = hole1Contour;
        second_ = second;
    }

    // Order matches the legacy test's Check calls exactly: multi, main = item(0),
    // mainOuter = item(0).outerBorder(), mainHole = item(0).holeBorder(0), second = item(1).
    void expectRect(const DoubleRect& rect, std::array<RWP, 5> check, std::array<bool, 5> poly,
                    std::array<bool, 5> border) const {
        const std::array<const IGeometryProjectedPolygon*, 5> ordered{
            multi_.get(), main_.get(), mainOuter_.get(), mainHole_.get(), second_.get()};
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            EXPECT_EQ(ordered[i]->checkRectIntersection(rect), check[i]) << "object index " << i;
        }
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            EXPECT_EQ(ordered[i]->isRectIntersectPolygon(rect), poly[i]) << "object index " << i;
        }
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            EXPECT_EQ(ordered[i]->isRectIntersectBorder(rect), border[i]) << "object index " << i;
        }
    }

    std::shared_ptr<const IGeometryProjectedPolygon> multi_;
    std::shared_ptr<const IGeometryProjectedPolygon> main_;
    std::shared_ptr<const IGeometryProjectedPolygon> mainOuter_;
    std::shared_ptr<const IGeometryProjectedPolygon> mainHole_;
    std::shared_ptr<const IGeometryProjectedPolygon> second_;
};

TEST_F(ProjectedPolygonWithRect, R01IntersectPolygonAndHole) {
    expectRect({20, 10, 90, 90},
               {RWP::IntersectPartial, RWP::IntersectPartial, RWP::IntersectPartial, RWP::IntersectPartial,
                RWP::NoIntersect},
               {true, true, true, true, false}, {true, true, true, true, false});
}

TEST_F(ProjectedPolygonWithRect, R02NearPolygon) {
    expectRect({240, 40, 280, 70},
               {RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect},
               {false, false, false, false, false}, {false, false, false, false, false});
}

TEST_F(ProjectedPolygonWithRect, R03NearPolygon) {
    expectRect({190, 80, 220, 90},
               {RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect},
               {false, false, false, false, false}, {false, false, false, false, false});
}

TEST_F(ProjectedPolygonWithRect, R04InHole) {
    expectRect({90, 100, 110, 110},
               {RWP::NoIntersect, RWP::NoIntersect, RWP::RectInPolygon, RWP::RectInPolygon, RWP::NoIntersect},
               {false, false, true, true, false}, {false, false, false, false, false});
}

TEST_F(ProjectedPolygonWithRect, R05IntersectHole) {
    expectRect({80, 130, 120, 160},
               {RWP::IntersectPartial, RWP::IntersectPartial, RWP::RectInPolygon, RWP::IntersectPartial,
                RWP::NoIntersect},
               {true, true, true, true, false}, {true, true, false, true, false});
}

TEST_F(ProjectedPolygonWithRect, R06InMainPolygon) {
    expectRect(
        {220, 160, 260, 190},
        {RWP::RectInPolygon, RWP::RectInPolygon, RWP::RectInPolygon, RWP::NoIntersect, RWP::NoIntersect},
        {true, true, true, false, false}, {false, false, false, false, false});
}

TEST_F(ProjectedPolygonWithRect, R07BetweenMainAndSecond) {
    expectRect({300, 160, 350, 190},
               {RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect},
               {false, false, false, false, false}, {false, false, false, false, false});
}

TEST_F(ProjectedPolygonWithRect, R08InSecondPolygon) {
    expectRect({430, 140, 470, 170},
               {RWP::RectInPolygon, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::RectInPolygon},
               {true, false, false, false, true}, {false, false, false, false, false});
}

TEST_F(ProjectedPolygonWithRect, R09IntersectSecondPolygon) {
    expectRect(
        {390, 190, 430, 220},
        {RWP::IntersectPartial, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::IntersectPartial},
        {true, false, false, false, true}, {true, false, false, false, true});
}

TEST_F(ProjectedPolygonWithRect, R10NearPolygon) {
    expectRect({130, 210, 160, 230},
               {RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect},
               {false, false, false, false, false}, {false, false, false, false, false});
}

TEST_F(ProjectedPolygonWithRect, R11IntersectPolygon) {
    expectRect({40, 220, 80, 250},
               {RWP::IntersectPartial, RWP::IntersectPartial, RWP::IntersectPartial, RWP::NoIntersect,
                RWP::NoIntersect},
               {true, true, true, false, false}, {true, true, true, false, false});
}

TEST_F(ProjectedPolygonWithRect, R12FarAway) {
    expectRect({540, 40, 580, 70},
               {RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect},
               {false, false, false, false, false}, {false, false, false, false, false});
}

TEST_F(ProjectedPolygonWithRect, R13AroundPolygon) {
    expectRect(
        {10, 20, 520, 260},
        {RWP::PolygonInRect, RWP::PolygonInRect, RWP::PolygonInRect, RWP::PolygonInRect, RWP::PolygonInRect},
        {true, true, true, true, true}, {false, false, false, false, false});
}

TEST_F(ProjectedPolygonWithRect, R14AroundSecond) {
    expectRect(
        {400, 10, 500, 250},
        {RWP::IntersectPartial, RWP::NoIntersect, RWP::NoIntersect, RWP::NoIntersect, RWP::PolygonInRect},
        {true, false, false, false, true}, {false, false, false, false, false});
}

// docs/specs/geo.md §3.3.1: `calcArea()` uses the legacy `(x1+x2)*(y1-y2)` shoelace form,
// starting from the LAST point, not `x1*y2-x2*y1` -- algebraically equal, not the same
// implementation. This pins the transcribed form with a hand-verified value
// (u_GeometryProjected.pas:353-371): triangle (0,0)-(4,0)-(0,3), legacy area 6.
TEST(GeometryProjectedContour, CalcAreaUsesLegacyShoelaceForm) {
    const std::vector<DoublePoint> triangle = {{0, 0}, {4, 0}, {0, 3}};
    const GeometryProjectedContour contour(mbrByPoints(triangle), makePoints(triangle));
    EXPECT_DOUBLE_EQ(contour.calcArea(), 6.0);
}

// =================================================================================================
// GeometryProjectedFactory / GeometryProjectedProvider (docs/specs/geo.md §3.5.1-§3.5.3, T-0017C2)
// =================================================================================================

namespace {

ProjectionSetFactory makeSetFactory() {
    auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    return ProjectionSetFactory{HashSeed{}, datums};
}

ProjectionPtr atZoom(ProjectionSetFactory& factory, int epsg, quint8 zoom) {
    return factory.byCode(epsg, kTileSplitQuadrate256x256)->zoom(zoom);
}

cg::geo::GeometryLonLatPtr makeLonLatMultiLine() {
    GeometryLonLatFactory factory;
    auto builder = factory.makeLineBuilder();
    builder.addLine(
        std::make_shared<const std::vector<LonLat>>(std::vector<LonLat>{{10.0, 50.0}, {10.01, 50.0}}));
    builder.addLine(
        std::make_shared<const std::vector<LonLat>>(std::vector<LonLat>{{11.0, 51.0}, {11.02, 51.02}}));
    return builder.makeStaticAndClear();
}

cg::geo::GeometryLonLatPtr makeLonLatSinglePolygonWithHole() {
    GeometryLonLatFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(std::make_shared<const std::vector<LonLat>>(
        std::vector<LonLat>{{10.0, 50.0}, {10.1, 50.0}, {10.1, 50.1}, {10.0, 50.1}}));
    builder.addHole(std::make_shared<const std::vector<LonLat>>(
        std::vector<LonLat>{{10.03, 50.03}, {10.06, 50.03}, {10.06, 50.06}}));
    return builder.makeStaticAndClear();
}

} // namespace

// The fused EnumToPoints pass (projectRing, geometry_factory.cpp) must agree with an MBR computed
// independently over the same projected points -- "BoundsComputedInTheSamePassAsPoints"
// (docs/specs/geo.md §11.2 geometry_projected_test.cpp new-case list).
TEST(GeometryProjectedFactory, CreateLineByLonLatLineProjectsEachSubLineWithCorrectBounds) {
    auto setFactory = makeSetFactory();
    const ProjectionPtr projection = atZoom(setFactory, kProjEpsgGoogle, 10);
    const auto lonLatLine = makeLonLatMultiLine();
    const auto& asLine = dynamic_cast<const IGeometryLonLatLine&>(*lonLatLine);

    GeometryProjectedFactory factory;
    std::vector<DoublePoint> scratch;
    const auto result = factory.createLineByLonLatLine(*projection, asLine, scratch);
    ASSERT_TRUE(result);

    const auto* multi = dynamic_cast<const cg::geo::IGeometryProjectedMultiLine*>(result.get());
    ASSERT_NE(multi, nullptr);
    ASSERT_EQ(multi->count(), 2u);

    // Independently project the first sub-line's two points and compare against item(0).
    const DoublePoint p0 = projection->lonLat2PixelPosFloat({10.0, 50.0});
    const DoublePoint p1 = projection->lonLat2PixelPosFloat({10.01, 50.0});
    const DoubleRect expectedBounds = mbrByPoints({p0, p1});
    EXPECT_EQ(multi->item(0)->bounds(), expectedBounds);
    ASSERT_EQ(multi->item(0)->points().size(), 2u);
    EXPECT_DOUBLE_EQ(multi->item(0)->points()[0].x(), p0.x());
    EXPECT_DOUBLE_EQ(multi->item(0)->points()[0].y(), p0.y());
}

TEST(GeometryProjectedFactory, CreatePolygonByLonLatPolygonProjectsOuterAndHoles) {
    auto setFactory = makeSetFactory();
    const ProjectionPtr projection = atZoom(setFactory, kProjEpsgGoogle, 10);
    const auto lonLatPolygon = makeLonLatSinglePolygonWithHole();
    const auto& asPolygon = dynamic_cast<const IGeometryLonLatPolygon&>(*lonLatPolygon);

    GeometryProjectedFactory factory;
    std::vector<DoublePoint> scratch;
    const auto result = factory.createPolygonByLonLatPolygon(*projection, asPolygon, scratch);
    ASSERT_TRUE(result);

    const auto* single = dynamic_cast<const cg::geo::IGeometryProjectedSinglePolygon*>(result.get());
    ASSERT_NE(single, nullptr);
    EXPECT_EQ(single->outerBorder()->points().size(), 4u);
    ASSERT_EQ(single->holeCount(), 1u);
    EXPECT_EQ(single->holeBorder(0)->points().size(), 3u);
    // Single-polygon bounds is the outer ring's own MBR, per the builder's documented contract
    // (buildPending, geometry_factory.cpp) -- not unioned with the hole's bounds.
    EXPECT_EQ(single->bounds(), single->outerBorder()->bounds());
}

// The "buffering-path" case: a lon/lat line buffered into a polygon by the already-landed
// GeometryLonLatFactory::createPolygonByLine (T-0017B, line2Poly/§3.5.2), then run through this
// ticket's projected polygon factory -- the two halves of geometry_factory.cpp composed exactly
// as the renderer composes them for a "line with width" mark.
TEST(GeometryProjectedFactory, CreatePolygonByLonLatPolygonAfterBufferingALine) {
    auto setFactory = makeSetFactory();
    const ProjectionPtr projection = atZoom(setFactory, kProjEpsgGoogle, 10);
    const auto lonLatLine = makeLonLatMultiLine();
    const auto& asLine = dynamic_cast<const IGeometryLonLatLine&>(*lonLatLine);

    GeometryLonLatFactory lonLatFactory;
    std::vector<DoublePoint> bufferScratch;
    const auto buffered = lonLatFactory.createPolygonByLine(asLine, 50.0, *projection, bufferScratch);
    ASSERT_TRUE(buffered);
    const auto& asPolygon = dynamic_cast<const IGeometryLonLatPolygon&>(*buffered);

    GeometryProjectedFactory projectedFactory;
    std::vector<DoublePoint> scratch;
    const auto result = projectedFactory.createPolygonByLonLatPolygon(*projection, asPolygon, scratch);
    ASSERT_TRUE(result);
    EXPECT_GT(result->bounds().right - result->bounds().left, 0.0);
    EXPECT_GT(result->bounds().bottom - result->bounds().top, 0.0);
}

TEST(GeometryProjectedProvider, CachesAndReturnsTheSameInstance) {
    auto setFactory = makeSetFactory();
    const ProjectionPtr projection = atZoom(setFactory, kProjEpsgGoogle, 10);
    const auto lonLatLine = makeLonLatMultiLine();
    const auto asLine = std::dynamic_pointer_cast<const IGeometryLonLatLine>(lonLatLine);
    ASSERT_TRUE(asLine);

    GeometryProjectedProvider provider;
    const auto first = provider.projectedPath(projection, asLine);
    const auto second = provider.projectedPath(projection, asLine);
    ASSERT_TRUE(first);
    EXPECT_EQ(first.get(), second.get());
}

// TGeometryProjectedProvider.CreateByKey's CMinProjectedPolygonSize=10 guard
// (u_GeometryProjectedProvider.pas:87,150-151): a polygon whose lon/lat bounds project to <= 10
// pixels in BOTH dimensions is too small to matter and the cached answer is nullptr, same as a
// real miss -- and it stays nullptr (cached, not recomputed) on a second call.
TEST(GeometryProjectedProvider, DegeneratePolygonCachesNullptr) {
    GeometryLonLatFactory lonLatFactory;
    auto builder = lonLatFactory.makePolygonBuilder();
    // A one-metre square at zoom 1 projects to well under 10 pixels.
    builder.addOuter(std::make_shared<const std::vector<LonLat>>(
        std::vector<LonLat>{{10.0, 50.0}, {10.00001, 50.0}, {10.00001, 50.00001}, {10.0, 50.00001}}));
    const auto lonLatPolygon = builder.makeStaticAndClear();
    const auto asPolygon = std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(lonLatPolygon);
    ASSERT_TRUE(asPolygon);

    auto setFactory = makeSetFactory();
    const ProjectionPtr projection = atZoom(setFactory, kProjEpsgGoogle, 1);

    GeometryProjectedProvider provider;
    EXPECT_FALSE(provider.projectedPolygon(projection, asPolygon));
    EXPECT_FALSE(provider.projectedPolygon(projection, asPolygon)); // cached miss, no crash/recompute
}

// =================================================================================================
// Ports Test/u_VectorItmesFactorySimple_Test.pas, suites TestGeometryProjectedLineBuilderSimple and
// TestGeometryProjectedPolygonBuilderSimple (T-0040; the LonLat suites are in geometry_lonlat_test.cpp).
// Legacy enumerates via IEnumDoublePoint; the port reads points() (contours are stored open, as in the
// LonLat port -- docs/specs/geo.md §3.2 point 2); the legacy closing enumeration is asserted via
// singleLineStream(points, nullptr, closed=true), the port's IEnumDoublePoint over a contour.
// =================================================================================================

namespace {
// Legacy IEnumDoublePoint over a closed contour (u_EnumDoublePointBySingleLine.pas:142): the points,
// then points[0] again when the contour has more than one point.
std::vector<DoublePoint> drainClosed(std::span<const DoublePoint> pts) {
    auto stream = cg::geo::singleLineStream(pts, nullptr, true);
    std::vector<DoublePoint> out;
    DoublePoint p;
    while (stream->next(p)) {
        out.push_back(p);
    }
    return out;
}

ProjectedPointsPtr makeProjPoints(std::vector<DoublePoint> pts) {
    return std::make_shared<const std::vector<DoublePoint>>(std::move(pts));
}
} // namespace

TEST(GeometryProjectedLineBuilder, NoLines) {
    GeometryProjectedFactory factory;
    auto builder = factory.makeLineBuilder();
    EXPECT_EQ(builder.makeStaticAndClear(), nullptr);
}

TEST(GeometryProjectedLineBuilder, Simple) {
    GeometryProjectedFactory factory;
    auto builder = factory.makeLineBuilder();
    builder.addLine(DoubleRect(0, 0, 1, 1), makeProjPoints({{0, 1}, {1, 1}, {1, 0}}));
    auto result =
        std::dynamic_pointer_cast<const cg::geo::IGeometryProjectedSingleLine>(builder.makeStaticAndClear());
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->points().size(), 3u);
    EXPECT_EQ(result->points()[0], (DoublePoint{0, 1}));
    EXPECT_EQ(result->points()[1], (DoublePoint{1, 1}));
    EXPECT_EQ(result->points()[2], (DoublePoint{1, 0}));
}

TEST(GeometryProjectedLineBuilder, TwoLines) {
    GeometryProjectedFactory factory;
    auto builder = factory.makeLineBuilder();
    builder.addLine(DoubleRect(0, 0, 1, 1), makeProjPoints({{0, 1}}));
    builder.addLine(DoubleRect(0, 0, 1, 1), makeProjPoints({{1, 1}, {1, 0}}));
    auto result =
        std::dynamic_pointer_cast<const cg::geo::IGeometryProjectedMultiLine>(builder.makeStaticAndClear());
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->count(), 2u);
    ASSERT_EQ(result->item(0)->points().size(), 1u);
    EXPECT_EQ(result->item(0)->points()[0], (DoublePoint{0, 1}));
    ASSERT_EQ(result->item(1)->points().size(), 2u);
    EXPECT_EQ(result->item(1)->points()[0], (DoublePoint{1, 1}));
    EXPECT_EQ(result->item(1)->points()[1], (DoublePoint{1, 0}));
}

TEST(GeometryProjectedPolygonBuilder, NoLines) {
    GeometryProjectedFactory factory;
    auto builder = factory.makePolygonBuilder();
    EXPECT_EQ(builder.makeStaticAndClear(), nullptr);
}

TEST(GeometryProjectedPolygonBuilder, Simple) {
    GeometryProjectedFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(DoubleRect(0, 0, 1, 1), makeProjPoints({{0, 1}, {1, 1}, {1, 0}}));
    auto result = std::dynamic_pointer_cast<const cg::geo::IGeometryProjectedSinglePolygon>(
        builder.makeStaticAndClear());
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->outerBorder()->points().size(), 3u);
    EXPECT_EQ(result->outerBorder()->points()[0], (DoublePoint{0, 1}));
    EXPECT_EQ(result->outerBorder()->points()[1], (DoublePoint{1, 1}));
    EXPECT_EQ(result->outerBorder()->points()[2], (DoublePoint{1, 0}));
    const auto en = drainClosed(result->outerBorder()->points());
    ASSERT_EQ(en.size(), 4u); // p0, p1, p2, p0, then end
    EXPECT_EQ(en[0], (DoublePoint{0, 1}));
    EXPECT_EQ(en[1], (DoublePoint{1, 1}));
    EXPECT_EQ(en[2], (DoublePoint{1, 0}));
    EXPECT_EQ(en[3], (DoublePoint{0, 1}));
}

TEST(GeometryProjectedPolygonBuilder, TwoLines) {
    GeometryProjectedFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(DoubleRect(0, 1, 0, 1), makeProjPoints({{0, 1}}));
    builder.addOuter(DoubleRect(0, 1, 0, 1), makeProjPoints({{1, 1}, {1, 0}}));
    auto result = std::dynamic_pointer_cast<const cg::geo::IGeometryProjectedMultiPolygon>(
        builder.makeStaticAndClear());
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->count(), 2u);
    EXPECT_EQ(result->item(0)->holeCount(), 0u);
    ASSERT_EQ(result->item(0)->outerBorder()->points().size(), 1u);
    EXPECT_EQ(result->item(0)->outerBorder()->points()[0], (DoublePoint{0, 1}));
    EXPECT_EQ(result->item(1)->holeCount(), 0u);
    ASSERT_EQ(result->item(1)->outerBorder()->points().size(), 2u);
    EXPECT_EQ(result->item(1)->outerBorder()->points()[0], (DoublePoint{1, 1}));
    EXPECT_EQ(result->item(1)->outerBorder()->points()[1], (DoublePoint{1, 0}));
    // Closing enumeration: the 1-point contour yields no closing point; the 2-point one yields p0 third.
    const auto e0 = drainClosed(result->item(0)->outerBorder()->points());
    ASSERT_EQ(e0.size(), 1u);
    EXPECT_EQ(e0[0], (DoublePoint{0, 1}));
    const auto e1 = drainClosed(result->item(1)->outerBorder()->points());
    ASSERT_EQ(e1.size(), 3u);
    EXPECT_EQ(e1[0], (DoublePoint{1, 1}));
    EXPECT_EQ(e1[1], (DoublePoint{1, 0}));
    EXPECT_EQ(e1[2], (DoublePoint{1, 1}));
}
