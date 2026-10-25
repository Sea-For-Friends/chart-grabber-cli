// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-014, FEAT-GEO-015. Ports: Src/GeoCalc/{i_GeoCalc,u_GeoCalc}.pas (docs/specs/geo.md
// §5). No legacy DUnit test exercises TGeoCalc directly. "Otherwise minimal" per the test policy:
// the length/perimeter composition rules (open line vs. closed contour, multi sums parts) pinned
// against a hand-derived value, one perimeter and one area case against a hand-computed value.
//
// The perimeter/line-length values below are hand-derived from elementary spherical geometry, not
// from running this port's own code: on a perfect sphere (flattening 0), a geodesic IS a great
// circle, so the distance between two points 90 degrees apart (equator-to-equator or
// equator-to-pole) is exactly R * pi / 2, independent of which such pair is chosen.

#include "cg/geo/geo_calc.h"
#include "cg/geo/hash_seed.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using cg::geo::DatumFactory;
using cg::geo::GeoCalc;
using cg::geo::GeometryLonLatContour;
using cg::geo::GeometryLonLatContourPtr;
using cg::geo::GeometryLonLatMultiLine;
using cg::geo::GeometryLonLatSingleLine;
using cg::geo::GeometryLonLatSingleLinePtr;
using cg::geo::GeometryLonLatSinglePolygon;
using cg::geo::HashSeed;
using cg::geo::LonLat;
using cg::geo::LonLatPointsPtr;
using cg::geo::PointsMeta;

namespace {

LonLatPointsPtr points(std::vector<LonLat> pts) {
    return std::make_shared<const std::vector<LonLat>>(std::move(pts));
}

// R * pi/2: the great-circle distance, on the ESRI sphere (radius 6371000 m), between any two
// points exactly 90 degrees apart (e.g. two equator points 90 degrees of longitude apart, or any
// equator point to a pole) -- elementary spherical geometry, independent of this port.
constexpr double kSphereRadius = 6371000.0;
const double kQuarterCircle = kSphereRadius * std::numbers::pi / 2.0;

} // namespace

TEST(GeoCalc, LineLengthIsOpen) {
    // A 3-point line along two quarter-circle legs: (0,0) -> (90,0) -> (0,90) (equator to equator
    // to pole). Length is the OPEN sum of the two legs only, 2 * kQuarterCircle -- NOT the 3-leg
    // closed perimeter (3 * kQuarterCircle) that GeoCalc.PolygonPerimeterHandComputedValue below
    // gets for the identical ring, which is exactly what "no closing segment" means.
    DatumFactory datums{HashSeed{}};
    const GeoCalc calc(datums.byRadius(kSphereRadius, kSphereRadius));

    const GeometryLonLatSingleLine line(cg::core::kNoHash, {}, points({{0, 0}, {90, 0}, {0, 90}}),
                                        PointsMeta{});
    EXPECT_NEAR(calc.calcLineLength(line), 2.0 * kQuarterCircle, 1.0);
}

TEST(GeoCalc, MultiLineLengthSumsParts) {
    // Two independent quarter-circle legs as two single lines; the multi-line's length must be
    // exactly their sum, not (say) a shared start point deduplicated or a wrap-around added.
    DatumFactory datums{HashSeed{}};
    const GeoCalc calc(datums.byRadius(kSphereRadius, kSphereRadius));

    const auto part1 = std::make_shared<const GeometryLonLatSingleLine>(
        cg::core::kNoHash, cg::geo::LonLatRect{}, points({{0, 0}, {90, 0}}), PointsMeta{});
    const auto part2 = std::make_shared<const GeometryLonLatSingleLine>(
        cg::core::kNoHash, cg::geo::LonLatRect{}, points({{0, 0}, {0, 90}}), PointsMeta{});
    const double part1Length = calc.calcLineLength(*part1);
    const double part2Length = calc.calcLineLength(*part2);
    ASSERT_NEAR(part1Length, kQuarterCircle, 1.0);
    ASSERT_NEAR(part2Length, kQuarterCircle, 1.0);

    const GeometryLonLatMultiLine multi(cg::core::kNoHash, cg::geo::LonLatRect{},
                                        std::vector<GeometryLonLatSingleLinePtr>{part1, part2});
    EXPECT_NEAR(calc.calcLineLength(multi), part1Length + part2Length, 1e-6);
}

TEST(GeoCalc, PolygonPerimeterHandComputedValue) {
    // Same 3-point ring as LineLengthIsOpen, but as a polygon's outer border: the perimeter is the
    // CLOSED loop, all three quarter-circle legs including (0,90) -> (0,0), so 3 * kQuarterCircle
    // -- not the 2-leg open sum a line over the identical points gets.
    DatumFactory datums{HashSeed{}};
    const GeoCalc calc(datums.byRadius(kSphereRadius, kSphereRadius));

    const auto outer = std::make_shared<const GeometryLonLatContour>(cg::core::kNoHash, cg::geo::LonLatRect{},
                                                                     points({{0, 0}, {90, 0}, {0, 90}}));
    const GeometryLonLatSinglePolygon polygon(cg::core::kNoHash, cg::geo::LonLatRect{}, outer,
                                              std::vector<GeometryLonLatContourPtr>{});
    EXPECT_NEAR(calc.calcPolygonPerimeter(polygon), 3.0 * kQuarterCircle, 1.0);
}

TEST(GeoCalc, PolygonAreaHandComputedValue) {
    // The exact box and value already independently verified in datum_test.cpp
    // (Datum.PolygonAreaMatchesGrassSeries: bit-for-bit checked against a from-scratch Python
    // re-implementation of u_PolygonAreaCalculator.pas, not sharing code with this port) --
    // GeoCalc.calcPolygonArea for a hole-free single polygon must delegate straight through to
    // that same number.
    DatumFactory datums{HashSeed{}};
    const GeoCalc calc(datums.byCode(6326)); // WGS84

    const auto outer = std::make_shared<const GeometryLonLatContour>(
        cg::core::kNoHash, cg::geo::LonLatRect{}, points({{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}));
    const GeometryLonLatSinglePolygon polygon(cg::core::kNoHash, cg::geo::LonLatRect{}, outer,
                                              std::vector<GeometryLonLatContourPtr>{});
    EXPECT_NEAR(calc.calcPolygonArea(polygon), 12308463893.80, 1.0);
}
