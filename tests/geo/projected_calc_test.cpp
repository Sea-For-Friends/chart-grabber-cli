// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-013. docs/specs/geo.md §3.6 / §11.2 projected_calc_test.cpp. No legacy DUnit
// test exercises u_ProjectedCalc.pas directly (grep of Test/*.pas confirms it), so this is the
// "otherwise a minimal set" branch of the test policy: one case per free function, built from the
// same fixtures §11.1 items 8/9 use (the 12-point concave polygon of u_ProjectedSingleLine_Test,
// bounds {1,1,13,8}; the outer/hole pair of u_ProjectedPolygonWithRect_Test). The six published
// u_ProjectedSingleLine_Test.pas methods are ported into IsRectIntersectPolygon below as one
// gtest case with six SCOPED_TRACE groups, not six separate TEST()s -- the same legacy constants,
// without multiplying case count for a predicate (IsRectIntersectPolygon) that geometry_projected
// already exercises exhaustively via checkRectIntersection/isRectIntersectPolygon (T-0017C1,
// R01-R14).

#include "cg/geo/projected_calc.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

using cg::geo::calcLineLength;
using cg::geo::calcPolygonArea;
using cg::geo::calcPolygonPerimeter;
using cg::geo::DoublePoint;
using cg::geo::DoubleRect;
using cg::geo::GeometryProjectedContour;
using cg::geo::GeometryProjectedContourPtr;
using cg::geo::GeometryProjectedMultiLine;
using cg::geo::GeometryProjectedMultiPolygon;
using cg::geo::GeometryProjectedSingleLine;
using cg::geo::GeometryProjectedSingleLinePtr;
using cg::geo::GeometryProjectedSinglePolygon;
using cg::geo::GeometryProjectedSinglePolygonPtr;
using cg::geo::IGeometryProjectedPolygon;
using cg::geo::isPointInPolygon;
using cg::geo::isPointOnLine;
using cg::geo::isPointOnPolygonBorder;
using cg::geo::isRectIntersectLine;
using cg::geo::isRectIntersectPolygon;
using cg::geo::isRectIntersectPolygonBorder;
using cg::geo::ProjectedPointsPtr;

namespace {

ProjectedPointsPtr makePoints(std::vector<DoublePoint> pts) {
    return std::make_shared<const std::vector<DoublePoint>>(std::move(pts));
}

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

// Test/u_ProjectedSingleLine_Test.pas:41-63, transcribed verbatim (§11.1 item 8). A single
// contour used directly as an (outer-only) IGeometryProjectedSinglePolygon, matching legacy's
// `TGeometryProjectedPolygon.Create` (the no-holes degenerate case, geometry_projected.h).
GeometryProjectedSinglePolygonPtr makeItem8Polygon() {
    const std::vector<DoublePoint> pts = {{1, 8},  {1, 4},  {4, 1}, {6, 1},  {3, 4},  {3, 7},
                                          {10, 7}, {10, 4}, {7, 1}, {10, 1}, {13, 4}, {13, 8}};
    const DoubleRect bounds{1, 1, 13, 8};
    auto outer = std::make_shared<const GeometryProjectedContour>(bounds, makePoints(pts));
    return std::make_shared<const GeometryProjectedSinglePolygon>(bounds, outer,
                                                                  std::vector<GeometryProjectedContourPtr>{});
}

// Test/u_ProjectedPolygonWithRect_Test.pas:62-97 (§11.1 item 9), outer #1 + its hole only (the
// "main" object of the R01-R14 fixture in geometry_projected_test.cpp).
GeometryProjectedSinglePolygonPtr makeItem9MainPolygon() {
    const std::vector<DoublePoint> outer1 = {{50, 40},  {220, 40},  {160, 80},  {230, 140}, {230, 80},
                                             {280, 80}, {280, 240}, {170, 240}, {170, 180}, {50, 240}};
    const std::vector<DoublePoint> hole1 = {{70, 60},   {90, 80},  {110, 50}, {130, 80}, {110, 80},
                                            {130, 120}, {90, 130}, {70, 160}, {80, 90}};
    const DoubleRect outer1Bounds = mbrByPoints(outer1);
    const DoubleRect hole1Bounds = mbrByPoints(hole1);
    auto outerContour = std::make_shared<const GeometryProjectedContour>(outer1Bounds, makePoints(outer1));
    auto holeContour = std::make_shared<const GeometryProjectedContour>(hole1Bounds, makePoints(hole1));
    return std::make_shared<const GeometryProjectedSinglePolygon>(
        outer1Bounds, outerContour, std::vector<GeometryProjectedContourPtr>{holeContour});
}

// A simple 3-4-5 / 3-4-5 path: (0,0)-(3,4)-(6,0), each leg length 5.
GeometryProjectedSingleLinePtr makeLegLine() {
    const std::vector<DoublePoint> pts = {{0, 0}, {3, 4}, {6, 0}};
    return std::make_shared<const GeometryProjectedSingleLine>(mbrByPoints(pts), makePoints(pts));
}

} // namespace

TEST(ProjectedCalc, IsPointOnLine) {
    const auto line = makeLegLine();
    // The midpoint of (0,0)-(3,4) lies exactly on the segment.
    EXPECT_TRUE(isPointOnLine(*line, DoublePoint(1.5, 2.0), 0.1));
    EXPECT_FALSE(isPointOnLine(*line, DoublePoint(100, 100), 0.1));
}

TEST(ProjectedCalc, IsRectIntersectLine) {
    const auto line = makeLegLine();
    EXPECT_TRUE(isRectIntersectLine(*line, DoubleRect{-1, -1, 4, 5}));
    EXPECT_FALSE(isRectIntersectLine(*line, DoubleRect{100, 100, 101, 101}));
}

TEST(ProjectedCalc, CalcLineLength) {
    const auto line = makeLegLine();
    EXPECT_DOUBLE_EQ(calcLineLength(*line), 10.0); // 5 + 5, no virtual for this in geometry_projected.h

    // MultiLine dispatch (TProjectedCalc.CalcMultiLineLength, u_ProjectedCalc.pas:426-431): sum
    // of each item's own length.
    std::vector<GeometryProjectedSingleLinePtr> items{line, line};
    const GeometryProjectedMultiLine multi(mbrByPoints({{0, 0}, {3, 4}, {6, 0}}), items);
    EXPECT_DOUBLE_EQ(calcLineLength(multi), 20.0);
}

TEST(ProjectedCalc, IsPointInPolygon) {
    const auto poly = makeItem8Polygon();
    EXPECT_TRUE(isPointInPolygon(*poly, DoublePoint(11, 5))); // inside the right lobe
    EXPECT_FALSE(isPointInPolygon(*poly, DoublePoint(5, 4))); // inside the concave notch, outside
}

TEST(ProjectedCalc, IsPointOnPolygonBorder) {
    const auto poly = makeItem8Polygon();
    EXPECT_TRUE(isPointOnPolygonBorder(*poly, DoublePoint(1, 6), 0.1));  // on the (1,8)-(1,4) edge
    EXPECT_FALSE(isPointOnPolygonBorder(*poly, DoublePoint(0, 0), 0.1)); // outside the bounds entirely
}

// Test/u_ProjectedSingleLine_Test.pas's six published TestIsRectIntersectPolygon* methods, ported
// verbatim (§11.1 item 8) as one gtest case exercising the projected_calc free function.
TEST(ProjectedCalc, IsRectIntersectPolygon) {
    const auto poly = makeItem8Polygon();
    {
        SCOPED_TRACE("OutsideFull");
        EXPECT_FALSE(isRectIntersectPolygon(*poly, DoubleRect{0, 0, 1, 1}));
        EXPECT_FALSE(isRectIntersectPolygon(*poly, DoubleRect{100, 1, 101, 2}));
        EXPECT_FALSE(isRectIntersectPolygon(*poly, DoubleRect{2, 100, 3, 101}));
    }
    {
        SCOPED_TRACE("OutsideMbrIntersect");
        EXPECT_FALSE(isRectIntersectPolygon(*poly, DoubleRect{1, 1, 2, 2}));
        EXPECT_FALSE(isRectIntersectPolygon(*poly, DoubleRect{12, 1, 13, 2}));
        EXPECT_FALSE(isRectIntersectPolygon(*poly, DoubleRect{4, 4, 5, 5}));
    }
    {
        SCOPED_TRACE("Inside");
        EXPECT_TRUE(isRectIntersectPolygon(*poly, DoubleRect{2, 4, 5, 6}));
        EXPECT_TRUE(isRectIntersectPolygon(*poly, DoubleRect{11, 4, 12, 5}));
    }
    {
        SCOPED_TRACE("FullInclude");
        EXPECT_TRUE(isRectIntersectPolygon(*poly, DoubleRect{1, 1, 13, 8}));
        EXPECT_TRUE(isRectIntersectPolygon(*poly, DoubleRect{0, 0, 100, 100}));
    }
    {
        SCOPED_TRACE("IntersectBorder");
        EXPECT_TRUE(isRectIntersectPolygon(*poly, DoubleRect{2, 2, 3, 3}));
    }
    {
        // Legacy comments this block "cases are debatable, behaviour may be changed" -- port the
        // expectations as they are (docs/specs/geo.md §11.1 item 8).
        SCOPED_TRACE("Touch");
        EXPECT_TRUE(isRectIntersectPolygon(*poly, DoubleRect{4, 0, 5, 1}));
        EXPECT_FALSE(isRectIntersectPolygon(*poly, DoubleRect{4, 6, 5, 7}));
        EXPECT_TRUE(isRectIntersectPolygon(*poly, DoubleRect{3, 5, 4, 6}));
        EXPECT_FALSE(isRectIntersectPolygon(*poly, DoubleRect{9, 5, 10, 6}));
        EXPECT_TRUE(isRectIntersectPolygon(*poly, DoubleRect{4, 8, 5, 9}));
    }
}

// R05 IntersectHole (docs/specs/geo.md §11.1 item 9): "main" (outer1+hole1)'s border row is
// T T F T F -- main (index 1) is T.
TEST(ProjectedCalc, IsRectIntersectPolygonBorder) {
    const auto main = makeItem9MainPolygon();
    EXPECT_TRUE(isRectIntersectPolygonBorder(*main, DoubleRect{80, 130, 120, 160}));
    EXPECT_FALSE(isRectIntersectPolygonBorder(*main, DoubleRect{540, 40, 580, 70})); // R12 FarAway
}

// The item-8 fixture's legacy-shoelace area/perimeter, independently computed.
TEST(ProjectedCalc, CalcPolygonArea) {
    const auto poly = makeItem8Polygon();
    EXPECT_DOUBLE_EQ(calcPolygonArea(*poly), 42.0);
}

TEST(ProjectedCalc, CalcPolygonPerimeter) {
    const auto poly = makeItem8Polygon();
    EXPECT_NEAR(calcPolygonPerimeter(*poly), 54.970562748477136, 1e-9);
}

// TProjectedCalc's Single/Multi dispatch (u_ProjectedCalc.pas Supports(...) else NaN/False):
// wrapping the same single polygon in a one-item MultiPolygon must give identical answers, and a
// bare IGeometryProjectedContour handed to the polygon functions must NOT reach its own (valid)
// predicate -- legacy's dispatcher only accepts Single/MultiPolygon (see projected_calc.h note).
TEST(ProjectedCalc, MultiDispatchMatchesSinglePolygonResult) {
    const auto single = makeItem8Polygon();
    const GeometryProjectedMultiPolygon multi(single->bounds(), {single});
    const DoublePoint inside(11, 5);
    const DoubleRect rect{2, 4, 5, 6};

    EXPECT_EQ(isPointInPolygon(multi, inside), isPointInPolygon(*single, inside));
    EXPECT_EQ(isRectIntersectPolygon(multi, rect), isRectIntersectPolygon(*single, rect));
    EXPECT_DOUBLE_EQ(calcPolygonArea(multi), calcPolygonArea(*single));
    EXPECT_DOUBLE_EQ(calcPolygonPerimeter(multi), calcPolygonPerimeter(*single));

    const IGeometryProjectedPolygon& bareContour = *single->outerBorder();
    EXPECT_TRUE(bareContour.isPointInPolygon(inside));   // the contour's own predicate says yes...
    EXPECT_FALSE(isPointInPolygon(bareContour, inside)); // ...but the free function's dispatcher
                                                         // does not accept a bare contour (legacy quirk)
    EXPECT_TRUE(std::isnan(calcPolygonArea(bareContour)));
}
