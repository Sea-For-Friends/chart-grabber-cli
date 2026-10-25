// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-011, FEAT-GEO-016. Rework 1 (docs/handoffs/T-0017A-review.md F6):
// geometry_func.cpp shipped with zero tests even though it was verified 1:1 against legacy by
// inspection. A handful of cases for the three pure, non-projection LonLat helpers, derived from
// u_GeometryFunc.pas:679-732 (IsValidLonLatLine / IsValidLonLatPolygon) and :825-857
// (IsLonLatPolygonSimpleRect). nearestPoint (the fourth exported symbol) needs an IProjection and
// is out of scope here.
//
// geometryToPlainText (T-0019A2, u_GeometryFunc.pas:1017-1148) lands here too: a small dispatch
// test per shape. It uses converter.lonLatConvert() itself (already fully covered by
// coord_to_string_test.cpp) as its own oracle for a single point's text, so these tests are only
// exercising this file's join/dispatch/auto-close logic, not CoordToStringConverter's formatting.

#include "cg/geo/coord_representation.h"
#include "cg/geo/coord_to_string.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_func.h"
#include "cg/geo/geometry_lonlat.h"

#include <QString>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using cg::geo::CoordRepresentationSettings;
using cg::geo::CoordToStringConverter;
using cg::geo::GeometryLonLatFactory;
using cg::geo::geometryToPlainText;
using cg::geo::IGeometryLonLatLine;
using cg::geo::IGeometryLonLatPolygon;
using cg::geo::isLonLatPolygonSimpleRect;
using cg::geo::isValidLonLatLine;
using cg::geo::isValidLonLatPolygon;
using cg::geo::LonLat;
using cg::geo::LonLatPointsPtr;

namespace {
LonLatPointsPtr makePoints(std::vector<LonLat> pts) {
    return std::make_shared<const std::vector<LonLat>>(std::move(pts));
}
} // namespace

// --- IsValidLonLatLine (u_GeometryFunc.pas:679-694) ---------------------------------------------

TEST(GeometryFunc, IsValidLonLatLineRejectsSinglePointLine) {
    // VSingleLine.Count > 1 -- a 1-point line is invalid.
    GeometryLonLatFactory factory;
    auto builder = factory.makeLineBuilder();
    builder.addLine(makePoints({{0, 0}}));
    auto line = std::dynamic_pointer_cast<const IGeometryLonLatLine>(builder.makeStaticAndClear());
    ASSERT_NE(line, nullptr);
    EXPECT_FALSE(isValidLonLatLine(*line));
}

TEST(GeometryFunc, IsValidLonLatLineAcceptsTwoPointLine) {
    GeometryLonLatFactory factory;
    auto builder = factory.makeLineBuilder();
    builder.addLine(makePoints({{0, 0}, {1, 1}}));
    auto line = std::dynamic_pointer_cast<const IGeometryLonLatLine>(builder.makeStaticAndClear());
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE(isValidLonLatLine(*line));
}

TEST(GeometryFunc, IsValidLonLatLineAcceptsMultiLineByCountAlone) {
    // VMultiLine.Count > 1 -- true regardless of each sub-line's own length.
    GeometryLonLatFactory factory;
    auto builder = factory.makeLineBuilder();
    builder.addLine(makePoints({{0, 0}}));
    builder.addLine(makePoints({{1, 1}}));
    auto line = std::dynamic_pointer_cast<const IGeometryLonLatLine>(builder.makeStaticAndClear());
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE(isValidLonLatLine(*line));
}

// --- IsValidLonLatPolygon (u_GeometryFunc.pas:696-732) ------------------------------------------

TEST(GeometryFunc, IsValidLonLatPolygonRejectsTwoPointOuter) {
    // IsValidLonLatContour: AGeometry.Count > 2 -- a 2-point ring is invalid.
    GeometryLonLatFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(makePoints({{0, 0}, {1, 1}}));
    auto polygon = std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(builder.makeStaticAndClear());
    ASSERT_NE(polygon, nullptr);
    EXPECT_FALSE(isValidLonLatPolygon(*polygon));
}

TEST(GeometryFunc, IsValidLonLatPolygonAcceptsThreePointOuter) {
    GeometryLonLatFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(makePoints({{0, 0}, {1, 1}, {1, 0}}));
    auto polygon = std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(builder.makeStaticAndClear());
    ASSERT_NE(polygon, nullptr);
    EXPECT_TRUE(isValidLonLatPolygon(*polygon));
}

TEST(GeometryFunc, IsValidLonLatPolygonAcceptsMultiPolygonByCountAlone) {
    // IsValidLonLatMultiPolygon: AGeometry.Count > 1 -- true regardless of each item's own shape.
    GeometryLonLatFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(makePoints({{0, 0}, {1, 1}}));
    builder.addOuter(makePoints({{2, 2}, {3, 3}}));
    auto polygon = std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(builder.makeStaticAndClear());
    ASSERT_NE(polygon, nullptr);
    EXPECT_TRUE(isValidLonLatPolygon(*polygon));
}

// --- IsLonLatPolygonSimpleRect (u_GeometryFunc.pas:825-857) -------------------------------------

TEST(GeometryFunc, IsLonLatPolygonSimpleRectAcceptsBothWindingOrders) {
    GeometryLonLatFactory factory;
    // createPolygonByRect's own winding: (left,top) (right,top) (right,bottom) (left,bottom).
    auto clockwise = factory.createPolygonByRect({0, 10, 10, 0});
    EXPECT_TRUE(isLonLatPolygonSimpleRect(*clockwise));

    auto builder = factory.makePolygonBuilder();
    // The other winding: (left,top) (left,bottom) (right,bottom) (right,top).
    builder.addOuter(makePoints({{0, 10}, {0, 0}, {10, 0}, {10, 10}}));
    auto counterClockwise =
        std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(builder.makeStaticAndClear());
    ASSERT_NE(counterClockwise, nullptr);
    EXPECT_TRUE(isLonLatPolygonSimpleRect(*counterClockwise));
}

TEST(GeometryFunc, IsLonLatPolygonSimpleRectRejectsNonRectQuadrilateral) {
    GeometryLonLatFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(makePoints({{0, 10}, {10, 10}, {10, 0}, {2, 0}})); // not axis-aligned
    auto polygon = std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(builder.makeStaticAndClear());
    ASSERT_NE(polygon, nullptr);
    EXPECT_FALSE(isLonLatPolygonSimpleRect(*polygon));
}

TEST(GeometryFunc, IsLonLatPolygonSimpleRectRejectsHoledRect) {
    GeometryLonLatFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(makePoints({{0, 10}, {10, 10}, {10, 0}, {0, 0}}));
    builder.addHole(makePoints({{2, 8}, {8, 8}, {8, 2}, {2, 2}}));
    auto polygon = std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(builder.makeStaticAndClear());
    ASSERT_NE(polygon, nullptr);
    EXPECT_EQ(std::dynamic_pointer_cast<const cg::geo::IGeometryLonLatSinglePolygon>(polygon)->holeCount(),
              1u);
    EXPECT_FALSE(isLonLatPolygonSimpleRect(*polygon)); // VSingle.HoleCount = 0 required
}

TEST(GeometryFunc, IsLonLatPolygonSimpleRectRejectsNonQuadrilateral) {
    GeometryLonLatFactory factory;
    auto triangle = factory.createPolygon(std::vector<LonLat>{{0, 0}, {10, 0}, {5, 10}});
    ASSERT_NE(triangle, nullptr);
    EXPECT_FALSE(
        isLonLatPolygonSimpleRect(*std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(triangle)));
}

// --- geometryToPlainText (u_GeometryFunc.pas:1017-1148) -----------------------------------------

TEST(GeometryFunc, GeometryToPlainTextDispatchesOnShapeAndJoinsWithTheGivenSeparators) {
    const CoordToStringConverter converter{CoordRepresentationSettings{}};
    const QString pointSep = QStringLiteral(";");
    const QString contourSep = QStringLiteral("|");
    const QString multiSep = QStringLiteral("#");

    GeometryLonLatFactory factory;

    // Point: GeometryLonLatPointToPlainText -- just converter.lonLatConvert(point), no separators.
    const LonLat p{1, 2};
    const auto point = factory.createPoint(p);
    EXPECT_EQ(geometryToPlainText(*point, converter, pointSep, contourSep, multiSep),
              converter.lonLatConvert(p));

    // Single line: every vertex joined by pointSep, no auto-close (a line is never closed).
    auto lineBuilder = factory.makeLineBuilder();
    lineBuilder.addLine(makePoints({{0, 0}, {1, 1}, {2, 0}}));
    const auto singleLine =
        std::dynamic_pointer_cast<const IGeometryLonLatLine>(lineBuilder.makeStaticAndClear());
    ASSERT_NE(singleLine, nullptr);
    const QString expectedSingleLine = converter.lonLatConvert({0, 0}) + pointSep +
                                       converter.lonLatConvert({1, 1}) + pointSep +
                                       converter.lonLatConvert({2, 0});
    EXPECT_EQ(geometryToPlainText(*singleLine, converter, pointSep, contourSep, multiSep),
              expectedSingleLine);

    // Multi-line: each sub-line as above, joined by multiSep (not pointSep).
    auto multiLineBuilder = factory.makeLineBuilder();
    multiLineBuilder.addLine(makePoints({{0, 0}, {1, 1}}));
    multiLineBuilder.addLine(makePoints({{3, 3}, {4, 4}}));
    const auto multiLine =
        std::dynamic_pointer_cast<const IGeometryLonLatLine>(multiLineBuilder.makeStaticAndClear());
    ASSERT_NE(multiLine, nullptr);
    const QString expectedMultiLine =
        (converter.lonLatConvert({0, 0}) + pointSep + converter.lonLatConvert({1, 1})) + multiSep +
        (converter.lonLatConvert({3, 3}) + pointSep + converter.lonLatConvert({4, 4}));
    EXPECT_EQ(geometryToPlainText(*multiLine, converter, pointSep, contourSep, multiSep), expectedMultiLine);

    // Single polygon with one hole: each contour auto-closes (repeats its first vertex at the
    // end, even though points() itself stays open, geometry_lonlat.h), outer and hole joined by
    // contourSep (not multiSep).
    auto polyBuilder = factory.makePolygonBuilder();
    polyBuilder.addOuter(makePoints({{0, 10}, {10, 10}, {10, 0}, {0, 0}}));
    polyBuilder.addHole(makePoints({{2, 8}, {8, 8}, {8, 2}}));
    const auto singlePolygon =
        std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(polyBuilder.makeStaticAndClear());
    ASSERT_NE(singlePolygon, nullptr);
    const QString expectedOuter =
        converter.lonLatConvert({0, 10}) + pointSep + converter.lonLatConvert({10, 10}) + pointSep +
        converter.lonLatConvert({10, 0}) + pointSep + converter.lonLatConvert({0, 0}) + pointSep +
        converter.lonLatConvert({0, 10}); // auto-closed: repeats the first vertex
    const QString expectedHole =
        converter.lonLatConvert({2, 8}) + pointSep + converter.lonLatConvert({8, 8}) + pointSep +
        converter.lonLatConvert({8, 2}) + pointSep + converter.lonLatConvert({2, 8}); // auto-closed
    EXPECT_EQ(geometryToPlainText(*singlePolygon, converter, pointSep, contourSep, multiSep),
              expectedOuter + contourSep + expectedHole);

    // Multi-polygon: each single polygon as above, joined by multiSep.
    auto multiPolyBuilder = factory.makePolygonBuilder();
    multiPolyBuilder.addOuter(makePoints({{0, 1}, {1, 1}, {1, 0}}));
    multiPolyBuilder.addOuter(makePoints({{5, 6}, {6, 6}, {6, 5}}));
    const auto multiPolygon =
        std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(multiPolyBuilder.makeStaticAndClear());
    ASSERT_NE(multiPolygon, nullptr);
    const QString expectedPoly0 =
        converter.lonLatConvert({0, 1}) + pointSep + converter.lonLatConvert({1, 1}) + pointSep +
        converter.lonLatConvert({1, 0}) + pointSep + converter.lonLatConvert({0, 1});
    const QString expectedPoly1 =
        converter.lonLatConvert({5, 6}) + pointSep + converter.lonLatConvert({6, 6}) + pointSep +
        converter.lonLatConvert({6, 5}) + pointSep + converter.lonLatConvert({5, 6});
    EXPECT_EQ(geometryToPlainText(*multiPolygon, converter, pointSep, contourSep, multiSep),
              expectedPoly0 + multiSep + expectedPoly1);
}
