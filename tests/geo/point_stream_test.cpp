// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-012. Ports Test/u_EnumDoublePoint{ClosePoly,FilterEqual,Line2Poly,
// WithClip}_Test.pas (6+8+2+13 = 29 cases); docs/specs/geo.md §11.1 items 4-7, §11.2
// point_stream_test.cpp (4 new).
#include "cg/geo/geometry_factory.h"
#include "cg/geo/point_stream.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type_factory.h"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <vector>

#include "geo_test_util.h"

using cg::geo::clipByBottomBorder;
using cg::geo::clipByLeftBorder;
using cg::geo::clipByRect;
using cg::geo::clipByRightBorder;
using cg::geo::clipByTopBorder;
using cg::geo::closePoly;
using cg::geo::DatumFactory;
using cg::geo::DoublePoint;
using cg::geo::DoubleRect;
using cg::geo::emptyDoublePoint;
using cg::geo::filterEqual;
using cg::geo::GeometryLonLatFactory;
using cg::geo::HashSeed;
using cg::geo::IGeometryLonLatMultiLine;
using cg::geo::isSeparator;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::line2Poly;
using cg::geo::lineSetStream;
using cg::geo::LonLat;
using cg::geo::lonLatToMapPixel;
using cg::geo::pointsStream;
using cg::geo::PointStreamPtr;
using cg::geo::ProjectionPtr;
using cg::geo::ProjectionSetFactory;
using cg::geo::test::DoublePointsEqual;

namespace {

ProjectionSetFactory makeSetFactory() {
    auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    return ProjectionSetFactory{HashSeed{}, datums};
}

ProjectionPtr atZoom(ProjectionSetFactory& factory, int epsg, quint8 zoom) {
    return factory.byCode(epsg, kTileSplitQuadrate256x256)->zoom(zoom);
}

// next() that checks both the return value and the point, delta 0 unless given.
void expectPoint(PointStreamPtr& s, DoublePoint expected, double delta = 0.0) {
    DoublePoint out;
    ASSERT_TRUE(s->next(out));
    EXPECT_TRUE(DoublePointsEqual(expected, out, delta)) << "got (" << out.x() << ", " << out.y() << ")";
}

void expectSeparator(PointStreamPtr& s) {
    DoublePoint out;
    ASSERT_TRUE(s->next(out));
    EXPECT_TRUE(isSeparator(out));
}

void expectDone(PointStreamPtr& s) {
    DoublePoint out;
    EXPECT_FALSE(s->next(out));
}

} // namespace

// --- 4. u_EnumDoublePointClosePoly_Test.pas -> ported: 6 -----------------------------------

TEST(ClosePoly, NoPoints) {
    std::vector<DoublePoint> data;
    auto s = closePoly(pointsStream(data));
    expectDone(s);
}

TEST(ClosePoly, OnePoint) {
    std::vector<DoublePoint> data{{1, 1}};
    auto s = closePoly(pointsStream(data));
    expectPoint(s, {1, 1});
    expectDone(s);
}

TEST(ClosePoly, SimpleIfNeedAddPoint) {
    std::vector<DoublePoint> data{{1, 1}, {0, 1}, {1, 0}};
    auto s = closePoly(pointsStream(data));
    expectPoint(s, {1, 1});
    expectPoint(s, {0, 1});
    expectPoint(s, {1, 0});
    expectPoint(s, {1, 1});
    expectDone(s);
}

TEST(ClosePoly, SimpleIfNoAddPoint) {
    std::vector<DoublePoint> data{{1, 1}, {0, 1}, {1, 0}, {1, 1}};
    auto s = closePoly(pointsStream(data));
    expectPoint(s, {1, 1});
    expectPoint(s, {0, 1});
    expectPoint(s, {1, 0});
    expectPoint(s, {1, 1});
    expectDone(s);
}

TEST(ClosePoly, WithEmptyAtEnd) {
    std::vector<DoublePoint> data{{1, 1}, {0, 1}, {1, 0}, emptyDoublePoint()};
    auto s = closePoly(pointsStream(data));
    expectPoint(s, {1, 1});
    expectPoint(s, {0, 1});
    expectPoint(s, {1, 0});
    expectPoint(s, {1, 1});
    DoublePoint out;
    s->next(out); // one unchecked next() (the synthesized trailing break)
    expectDone(s);
}

TEST(ClosePoly, TwoPoly) {
    std::vector<DoublePoint> data{{1, 1}, {0, 1}, {1, 0}, emptyDoublePoint(), {4, 4}};
    auto s = closePoly(pointsStream(data));
    expectPoint(s, {1, 1});
    expectPoint(s, {0, 1});
    expectPoint(s, {1, 0});
    expectPoint(s, {1, 1});
    expectSeparator(s);
    expectPoint(s, {4, 4});
    expectDone(s);
}

// T-0110: (5, NaN) is a separator, as PointIsEmpty tests X or Y (u_GeoFunc.pas:552); the same
// expectations as TwoPoly with a half-NaN separator in place of emptyDoublePoint().
TEST(ClosePoly, TwoPolyWithNanYOnlySeparator) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<DoublePoint> data{{1, 1}, {0, 1}, {1, 0}, {5, nan}, {4, 4}};
    auto s = closePoly(pointsStream(data));
    expectPoint(s, {1, 1});
    expectPoint(s, {0, 1});
    expectPoint(s, {1, 0});
    expectPoint(s, {1, 1});
    expectSeparator(s);
    expectPoint(s, {4, 4});
    expectDone(s);
}

// --- 5. u_EnumDoublePointFilterEqual_Test.pas -> ported: 8 ----------------------------------

TEST(FilterEqual, NoPoints) {
    std::vector<DoublePoint> data;
    auto s = filterEqual(pointsStream(data));
    expectDone(s);
}

TEST(FilterEqual, OnePoint) {
    std::vector<DoublePoint> data{{2, 2}};
    auto s = filterEqual(pointsStream(data));
    expectPoint(s, {2, 2});
    expectDone(s);
}

TEST(FilterEqual, SimpleIfNeedDeletePoint) {
    std::vector<DoublePoint> data{{2, 2}, {2, 2}, {2, 0}};
    auto s = filterEqual(pointsStream(data));
    expectPoint(s, {2, 2});
    expectPoint(s, {2, 0});
    expectDone(s);
}

TEST(FilterEqual, SimpleIfNoDeletePoint) {
    std::vector<DoublePoint> data{{2, 2}, {0, 2}, {2, 0}, {2, 2}};
    auto s = filterEqual(pointsStream(data));
    expectPoint(s, {2, 2});
    expectPoint(s, {0, 2});
    expectPoint(s, {2, 0});
    expectPoint(s, {2, 2});
    expectDone(s);
}

TEST(FilterEqual, DeleteThreePoints) {
    std::vector<DoublePoint> data{{2, 2}, {2.1, 2.1}, {2.4, 2.3}, {2.1, 2.1}};
    auto s = filterEqual(pointsStream(data));
    expectPoint(s, {2, 2});
    expectDone(s);
}

TEST(FilterEqual, DeleteTwoPoints) {
    std::vector<DoublePoint> data{{2, 2}, {2.1, 2.1}, {2.4, 2.3}, {2, 0}};
    auto s = filterEqual(pointsStream(data));
    expectPoint(s, {2, 2});
    expectPoint(s, {2, 0});
    expectDone(s);
}

TEST(FilterEqual, WithEmptyAtEnd) {
    std::vector<DoublePoint> data{{2, 2}, {0, 2}, {2, 0}, emptyDoublePoint()};
    auto s = filterEqual(pointsStream(data));
    expectPoint(s, {2, 2});
    expectPoint(s, {0, 2});
    expectPoint(s, {2, 0});
    DoublePoint out;
    s->next(out); // one unchecked next() (the pass-through separator)
    expectDone(s);
}

TEST(FilterEqual, TwoLines) {
    std::vector<DoublePoint> data{{2, 2}, {0, 2}, {2, 0}, emptyDoublePoint(), {4, 4}};
    auto s = filterEqual(pointsStream(data));
    expectPoint(s, {2, 2});
    expectPoint(s, {0, 2});
    expectPoint(s, {2, 0});
    expectSeparator(s);
    expectPoint(s, {4, 4});
    expectDone(s);
}

// --- 6. u_EnumDoublePointLine2Poly_Test.pas -> ported: 2 ------------------------------------

namespace {

class Line2PolyFixture : public ::testing::Test {
protected:
    ProjectionSetFactory factory_ = makeSetFactory();
    ProjectionPtr projection_ = atZoom(factory_, kProjEpsgGoogle, 10);
    static constexpr double kRadius = 1000.0;
    static constexpr double kDelta = 1e-7;
};

} // namespace

TEST_F(Line2PolyFixture, TestTwoPoints) {
    std::vector<DoublePoint> data{{10, 10}, {11, 11}};
    std::vector<DoublePoint> scratch;
    auto s = line2Poly(pointsStream(data), kRadius, *projection_, scratch);
    expectPoint(s, {10.000109176, 9.9872758672}, kDelta);
    expectPoint(s, {11.01292017, 11.00010717}, kDelta);
    expectPoint(s, {10.999890824, 11.012682517}, kDelta);
    expectPoint(s, {9.9870798298, 9.9998924829}, kDelta);
    expectPoint(s, {10.000109176, 9.9872758672}, kDelta);
    expectDone(s);
}

TEST_F(Line2PolyFixture, TestFivePoints) {
    std::vector<DoublePoint> data{{10, 10}, {11, 11}, {12, 10}, {13, 11}, {14, 11}};
    std::vector<DoublePoint> scratch;
    auto s = line2Poly(pointsStream(data), kRadius, *projection_, scratch);
    expectPoint(s, {10.000109176, 9.9872758672}, kDelta);
    expectPoint(s, {11, 10.987207936}, kDelta);
    expectPoint(s, {12, 9.9871665133}, kDelta);
    expectPoint(s, {13.003835966, 10.991016706}, kDelta);
    expectPoint(s, {14.009151292, 10.991016706}, kDelta);
    expectPoint(s, {14.009151292, 11.00898302}, kDelta);
    expectPoint(s, {12.996164034, 11.00898302}, kDelta);
    expectPoint(s, {12, 10.01283298}, kDelta);
    expectPoint(s, {11, 11.012791509}, kDelta);
    expectPoint(s, {9.9870798298, 9.9998924829}, kDelta);
    expectPoint(s, {10.000109176, 9.9872758672}, kDelta);
    expectDone(s);
}

// --- 7. u_EnumDoublePointWithClip_Test.pas -> ported: 13 ------------------------------------

TEST(ClipByLeftBorder, AllPointsIn) {
    std::vector<DoublePoint> data{{2, 2}, {2.1, 2.1}, {2.4, 2.3}, {2.1, 2.1}};
    auto s = clipByLeftBorder(1, pointsStream(data));
    for (const auto& p : data) {
        expectPoint(s, p);
    }
    expectDone(s);
}

TEST(ClipByLeftBorder, AllPointsOut) {
    std::vector<DoublePoint> data{{0, 0}, {0.1, 0.1}, {0.4, 0.3}, {0.1, 0.1}};
    auto s = clipByLeftBorder(1, pointsStream(data));
    expectDone(s);
}

TEST(ClipByLeftBorder, FirstPointOnLine) {
    std::vector<DoublePoint> data{{1, 1}, {2.1, 2.1}, {2.4, 2.3}, {2.1, 2.1}};
    auto s = clipByLeftBorder(1, pointsStream(data));
    for (const auto& p : data) {
        expectPoint(s, p);
    }
    expectDone(s);
}

TEST(ClipByLeftBorder, FirstPointOut) {
    std::vector<DoublePoint> data{{0, 2}, {2, 2}, {2.4, 2.3}, {2.1, 2.1}};
    auto s = clipByLeftBorder(1, pointsStream(data));
    expectPoint(s, {1, 2});
    expectPoint(s, {2, 2});
    expectPoint(s, {2.4, 2.3});
    expectPoint(s, {2.1, 2.1});
    expectDone(s);
}

TEST(ClipByLeftBorder, LastPointOnLine) {
    std::vector<DoublePoint> data{{2, 2}, {3, 3}, {2, 1}, {1, 1}};
    auto s = clipByLeftBorder(1, pointsStream(data));
    for (const auto& p : data) {
        expectPoint(s, p);
    }
    expectDone(s);
}

TEST(ClipByLeftBorder, LastPointOut) {
    std::vector<DoublePoint> data{{2, 2}, {3, 3}, {2, 1}, {0, 1}};
    auto s = clipByLeftBorder(1, pointsStream(data));
    expectPoint(s, {2, 2});
    expectPoint(s, {3, 3});
    expectPoint(s, {2, 1});
    expectPoint(s, {1, 1});
    expectDone(s);
}

TEST(ClipByLeftBorder, TwoPointsIn) {
    std::vector<DoublePoint> data{{0, 2}, {2, 2}, {2, 1}, {0, 1}};
    auto s = clipByLeftBorder(1, pointsStream(data));
    expectPoint(s, {1, 2});
    expectPoint(s, {2, 2});
    expectPoint(s, {2, 1});
    expectPoint(s, {1, 1});
    expectDone(s);
}

TEST(ClipByLeftBorder, SecondPointOnLine) {
    std::vector<DoublePoint> data{{2, 0}, {1, 1}, {2, 2}};
    auto s = clipByLeftBorder(1, pointsStream(data));
    for (const auto& p : data) {
        expectPoint(s, p);
    }
    expectDone(s);
}

TEST(ClipByLeftBorder, SecondPointOut) {
    std::vector<DoublePoint> data{{2, 0}, {0, 2}, {2, 4}};
    auto s = clipByLeftBorder(1, pointsStream(data));
    expectPoint(s, {2, 0});
    expectPoint(s, {1, 1});
    expectPoint(s, {1, 3});
    expectPoint(s, {2, 4});
    expectDone(s);
}

// T-0110: (0.5, NaN) has a finite X outside the border and a NaN Y. PointIsEmpty tests X or Y
// (u_GeoFunc.pas:552), so it is a separator (ClipByLeftBorder code 3, u_EnumDoublePointWithClip)
// and passes through; it is not an outside point that would intersect the segment.
TEST(ClipByLeftBorder, NanYOnlyPointIsSeparatorNotOutside) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<DoublePoint> data{{2, 2}, {0.5, nan}, {2.1, 2.1}};
    auto s = clipByLeftBorder(1, pointsStream(data));
    expectPoint(s, {2, 2});
    expectSeparator(s);
    expectPoint(s, {2.1, 2.1});
    expectDone(s);
}

TEST(ClipByRectPorted, ClosedFirstPointOut) {
    const DoubleRect rect{2, 0, 7, 5};
    std::vector<DoublePoint> data{{1, 0}, {4, 3}, {3, 4}, {1, 0}};
    auto s = clipByRect(true, rect, pointsStream(data));
    expectPoint(s, {2, 1});
    expectPoint(s, {4, 3});
    expectPoint(s, {3, 4});
    expectPoint(s, {2, 2});
    expectPoint(s, {2, 1});
    expectDone(s);
}

TEST(ClipByRectPorted, ClosedSecondPointOut) {
    const DoubleRect rect{2, 0, 7, 5};
    std::vector<DoublePoint> data{{4, 3}, {1, 0}, {3, 4}, {4, 3}};
    auto s = clipByRect(true, rect, pointsStream(data));
    expectPoint(s, {4, 3});
    expectPoint(s, {2, 1});
    expectPoint(s, {2, 2});
    expectPoint(s, {3, 4});
    expectPoint(s, {4, 3});
    expectDone(s);
}

TEST(ClipByRectPorted, ClosedAllPointsOut) {
    const DoubleRect rect{2, 0, 7, 5};
    std::vector<DoublePoint> data{{1, 0}, {1, 1}, {0, 1}, {1, 0}};
    auto s = clipByRect(true, rect, pointsStream(data));
    expectDone(s);
}

TEST(ClipByRectPorted, ClosedAllPointsAround) {
    const DoubleRect rect{2, 0, 7, 5};
    std::vector<DoublePoint> data{{1, 0}, {8, 0}, {10, 10}, {0, 10}, {1, 0}};
    auto s = clipByRect(true, rect, pointsStream(data));
    expectPoint(s, {2, 0});
    expectPoint(s, {7, 0});
    expectPoint(s, {7, 5});
    expectPoint(s, {2, 5});
    expectPoint(s, {2, 0});
    expectDone(s);
}

// --- additional coverage (§11.2 point_stream_test.cpp, 4) ------------------------------------

TEST(PointStreamNew, LineSetInjectsSeparatorsBetweenNotAround) {
    GeometryLonLatFactory factory{HashSeed{}};
    auto builder = factory.makeLineBuilder();
    builder.addLine(std::make_shared<const std::vector<LonLat>>(std::vector<LonLat>{{1, 1}, {2, 2}}));
    builder.addLine(std::make_shared<const std::vector<LonLat>>(std::vector<LonLat>{{3, 3}}));
    builder.addLine(std::make_shared<const std::vector<LonLat>>(std::vector<LonLat>{{4, 4}, {5, 5}, {6, 6}}));
    auto geom = builder.makeStaticAndClear();
    const auto* multi = dynamic_cast<const IGeometryLonLatMultiLine*>(geom.get());
    ASSERT_NE(multi, nullptr);
    ASSERT_EQ(multi->count(), 3u);

    auto s = lineSetStream(*multi);
    expectPoint(s, {1, 1});
    expectPoint(s, {2, 2});
    expectSeparator(s); // between sub-line 0 and 1, not before the first point
    expectPoint(s, {3, 3});
    expectSeparator(s); // between sub-line 1 and 2
    expectPoint(s, {4, 4});
    expectPoint(s, {5, 5});
    expectPoint(s, {6, 6});
    expectDone(s); // no trailing separator after the last sub-line
}

TEST(PointStreamNew, ClipByRectClosedUsesNineStages) {
    // The closed branch is closePoly interleaved between every border (9 stages); reproduce that
    // composition independently here and require clipByRect(true, ...) to match it exactly.
    const DoubleRect rect{2, 0, 7, 5};
    std::vector<DoublePoint> data{{1, 0}, {8, 0}, {10, 10}, {0, 10}, {1, 0}};

    auto expected = closePoly(pointsStream(data));
    expected = clipByBottomBorder(rect.bottom, std::move(expected));
    expected = closePoly(std::move(expected));
    expected = clipByRightBorder(rect.right, std::move(expected));
    expected = closePoly(std::move(expected));
    expected = clipByTopBorder(rect.top, std::move(expected));
    expected = closePoly(std::move(expected));
    expected = clipByLeftBorder(rect.left, std::move(expected));
    expected = closePoly(std::move(expected));

    auto actual = clipByRect(true, rect, pointsStream(data));

    DoublePoint expectedPoint;
    DoublePoint actualPoint;
    bool expectedHasNext = expected->next(expectedPoint);
    bool actualHasNext = actual->next(actualPoint);
    int guard = 0;
    while (expectedHasNext || actualHasNext) {
        ASSERT_LT(guard++, 100) << "runaway comparison loop";
        ASSERT_EQ(expectedHasNext, actualHasNext);
        EXPECT_TRUE(DoublePointsEqual(expectedPoint, actualPoint, 0.0));
        expectedHasNext = expected->next(expectedPoint);
        actualHasNext = actual->next(actualPoint);
    }
}

TEST(PointStreamNew, ClipByRectOpenUsesBottomTopLeftRightOrder) {
    // The open branch has no closePoly at all, composed strictly Bottom, Top, Left, Right;
    // reproduce that order independently and require clipByRect(false, ...) to match it exactly.
    const DoubleRect rect{2, 0, 7, 5};
    std::vector<DoublePoint> data{{1, 0}, {8, 0}, {10, 10}, {0, 10}, {1, 0}};

    auto expected = clipByBottomBorder(rect.bottom, pointsStream(data));
    expected = clipByTopBorder(rect.top, std::move(expected));
    expected = clipByLeftBorder(rect.left, std::move(expected));
    expected = clipByRightBorder(rect.right, std::move(expected));

    auto actual = clipByRect(false, rect, pointsStream(data));

    DoublePoint expectedPoint;
    DoublePoint actualPoint;
    bool expectedHasNext = expected->next(expectedPoint);
    bool actualHasNext = actual->next(actualPoint);
    int guard = 0;
    while (expectedHasNext || actualHasNext) {
        ASSERT_LT(guard++, 100) << "runaway comparison loop";
        ASSERT_EQ(expectedHasNext, actualHasNext);
        EXPECT_TRUE(DoublePointsEqual(expectedPoint, actualPoint, 0.0));
        expectedHasNext = expected->next(expectedPoint);
        actualHasNext = actual->next(actualPoint);
    }
}

TEST(PointStreamNew, LonLatToMapPixelValidatesBeforeProjecting) {
    auto factory = makeSetFactory();
    ProjectionPtr projection = atZoom(factory, kProjEpsgGoogle, 10);

    LonLat raw{200.0, 100.0}; // out of range on both axes for Mercator
    LonLat clamped = raw;
    projection->projectionType()->validateLonLatPos(clamped);
    const DoublePoint expectedFirst = projection->lonLat2PixelPosFloat(clamped);

    const LonLat inRange{10.0, 10.0};
    const DoublePoint expectedThird = projection->lonLat2PixelPosFloat(inRange);

    std::vector<DoublePoint> data{raw.toPoint(), emptyDoublePoint(), inRange.toPoint()};
    auto s = lonLatToMapPixel(*projection, pointsStream(data));
    expectPoint(s, expectedFirst, 1e-9);
    expectSeparator(s);
    expectPoint(s, expectedThird, 1e-9);
    expectDone(s);
}
