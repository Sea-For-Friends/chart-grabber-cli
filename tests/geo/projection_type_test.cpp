// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-001, FEAT-GEO-002. Ports Test/u_ProjectionType_Test.pas (3 cases); docs/specs/
// geo.md §11.1 item 2, §11.2 projection_type_test.cpp (17 new).
#include "cg/geo/proj_converter.h"
#include "cg/geo/projection_type.h"
#include "cg/geo/projection_type_factory.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <numbers>

#include "geo_test_util.h"

using cg::geo::DatumFactory;
using cg::geo::DoublePoint;
using cg::geo::GeographicProjectionType;
using cg::geo::HashSeed;
using cg::geo::LonLat;
using cg::geo::MercatorEllipsoidProjectionType;
using cg::geo::MercatorSphereProjectionType;
using cg::geo::ProjConverterFactory;
using cg::geo::ProjectionTypeFactory;

namespace {
ProjectionTypeFactory makeFactory() {
    return ProjectionTypeFactory{HashSeed{}, std::make_shared<const DatumFactory>(HashSeed{})};
}
} // namespace

// --- 2. u_ProjectionType_Test.pas -> ported --------------------------------------------------

class ProjectionTypeRoundTrip : public ::testing::TestWithParam<int> {};

TEST_P(ProjectionTypeRoundTrip, ValidatesAndCheckWithinBoundsForCanonicalPoints) {
    ProjectionTypeFactory factory = makeFactory();
    const auto type = factory.byCode(GetParam());
    const std::array<LonLat, 5> points = {
        {{360.0, 360.0}, {-360.0, 360.0}, {360.0, -360.0}, {-360.0, -360.0}, {0.0, 0.0}}};
    for (LonLat p : points) {
        type->validateLonLatPos(p);
        EXPECT_TRUE(type->checkLonLatPos(p));
        const auto r = type->lonLat2Relative(p);
        EXPECT_TRUE(type->checkRelativePos(r));
    }
}

INSTANTIATE_TEST_SUITE_P(ThreeNativeTypes, ProjectionTypeRoundTrip, ::testing::Values(4326, 3395, 3857));

// --- new coverage ------------------------------------------------------------------------------

TEST(ProjectionType, GeographicLatitudeUsesThreeSixtyNotOneEighty) {
    ProjectionTypeFactory factory = makeFactory();
    const auto geo = factory.byCode(4326);
    // North pole (lat 90) sits at ry = 0.25, not 0 -- the 2:1 world in a unit square.
    const auto r = geo->lonLat2Relative({0.0, 90.0});
    EXPECT_NEAR(r.y(), 0.25, 1e-12);
}

TEST(ProjectionType, GeographicClampsToNinety) {
    ProjectionTypeFactory factory = makeFactory();
    const auto geo = factory.byCode(4326);
    LonLat p{200.0, 150.0};
    geo->validateLonLatPos(p);
    EXPECT_DOUBLE_EQ(p.lon, 180.0);
    EXPECT_DOUBLE_EQ(p.lat, 90.0); // CMaxLatitude, not +/-180
}

TEST(ProjectionType, SphereSnapsToZeroWithin1e12) {
    ProjectionTypeFactory factory = makeFactory();
    const auto sphere = factory.byCode(3857);
    const auto r = sphere->lonLat2Relative({0.0, 0.0});
    EXPECT_EQ(r.y(), 0.5);
}

TEST(ProjectionType, SphereSnapsToOneWithin1e12) {
    ProjectionTypeFactory factory = makeFactory();
    const auto sphere = factory.byCode(3857);
    // The valid band's own top/bottom must land exactly on 0 / 1 thanks to the 1e-12 snap.
    const auto validRect = sphere->relativeRect2LonLatRect({0.0, 0.0, 1.0, 1.0});
    const auto back = sphere->lonLat2Relative({validRect.left, validRect.bottom});
    EXPECT_NEAR(back.y(), 1.0, 1e-9);
}

TEST(ProjectionType, SphereValidLatBandIsDerivedNotLiteral) {
    ProjectionTypeFactory factory = makeFactory();
    const auto sphere = factory.byCode(3857);
    const auto rect = sphere->relativeRect2LonLatRect({0.0, 0.0, 1.0, 1.0});
    EXPECT_NEAR(rect.top, 85.05112877980659, 1e-9);
    EXPECT_NEAR(rect.bottom, -85.05112877980659, 1e-9);
}

TEST(ProjectionType, SphereAssertsEqualRadii) {
    // u_ProjectionTypeMercatorOnSphere.pas:64: Assert(Abs(a-b) < 1). Just confirm the interned
    // type (equal radii) constructs without incident; a debug-mode Q_ASSERT would abort on
    // mismatched radii, which is exactly the legacy contract (a programmer error).
    DatumFactory datums{HashSeed{}};
    const auto d = datums.byCode(6055);
    EXPECT_NO_THROW((MercatorSphereProjectionType{0, d, 3857}));
}

TEST(ProjectionType, EllipsoidEccentricityFromDatum) {
    DatumFactory datums{HashSeed{}};
    const auto wgs84 = datums.byCode(6326);
    const double expectedE =
        std::sqrt(wgs84->radiusA() * wgs84->radiusA() - wgs84->radiusB() * wgs84->radiusB()) /
        wgs84->radiusA();
    MercatorEllipsoidProjectionType type{0, wgs84, 3395};
    // Indirect probe: lonLat2Relative at a known latitude must match the closed formula using e.
    const double lat = 45.0;
    const double z = std::sin(lat * std::numbers::pi / 180.0);
    const double expectedRy =
        0.5 - (std::atanh(z) - expectedE * std::atanh(expectedE * z)) / (2.0 * std::numbers::pi);
    EXPECT_NEAR(type.lonLat2Relative({0.0, lat}).y(), expectedRy, 1e-12);
}

TEST(ProjectionType, EllipsoidInverseStopsAt1e9) {
    ProjectionTypeFactory factory = makeFactory();
    const auto yandex = factory.byCode(3395);
    const auto forward = yandex->lonLat2Relative({10.0, 55.0});
    const auto back = yandex->relative2LonLat(forward);
    EXPECT_NEAR(back.lat, 55.0, 1e-7);
    EXPECT_NEAR(back.lon, 10.0, 1e-9);
}

TEST(ProjectionType, EllipsoidInverseKeepsFirstGuessOnNaN) {
    ProjectionTypeFactory factory = makeFactory();
    const auto yandex = factory.byCode(3395);
    // Every relative Y in [0,1] must produce a finite latitude -- the NaN fallback (if it were
    // ever hit) still returns the finite first guess, never lets NaN escape.
    for (double ry = 0.0; ry <= 1.0; ry += 0.05) {
        const auto ll = yandex->relative2LonLat({0.5, ry});
        EXPECT_TRUE(std::isfinite(ll.lat)) << "ry=" << ry;
    }
}

TEST(ProjectionType, EllipsoidForwardInverseRoundTripBelow1e9Degrees) {
    // ADR-0006 D-11, restated by the T-0016 review/ruling in the units the legacy stopping
    // constant is actually in: round-trip error < 1e-9 RADIANS (≡ 5.73e-8 degrees), not < 1e-9
    // degrees as the test's own name suggests. The legacy stopping rule is "|Zum1 - Zu| < 1e-9" on
    // the RADIAN step size (§2.2.3 rule 1, u_ProjectionTypeMercatorOnEllipsoid.pas:110), which
    // bounds how much the iterate moved on its last step, not the true remaining error. A
    // brute-force sweep at this exact grid (lat -85..85 step 0.01, lon -180..180 step 1; see
    // docs/handoffs/T-0016.md) measures a worst-case round-trip error of ~2.84e-8 degrees at
    // lat=-0.11 -- comfortably under the 5.73e-8-degree bound the stopping rule actually provides,
    // in both `double` and `long double` (EllipsoidForwardDoubleAgreesWithLongDoubleWithinD11Bound
    // below). This gate keeps the empirically verified `< 6e-8` degrees tolerance.
    constexpr double kGateTolerance = 6e-8;
    ProjectionTypeFactory factory = makeFactory();
    const auto yandex = factory.byCode(3395);
    for (double lat = -85.0; lat <= 85.0; lat += 0.01) {
        for (double lon = -180.0; lon <= 180.0; lon += 1.0) {
            const auto r = yandex->lonLat2Relative({lon, lat});
            const auto back = yandex->relative2LonLat(r);
            ASSERT_LT(std::abs(back.lat - lat), kGateTolerance) << "lat=" << lat << " lon=" << lon;
            ASSERT_LT(std::abs(back.lon - lon), kGateTolerance) << "lat=" << lat << " lon=" << lon;
        }
    }
}

namespace {
// Pure-`double` reimplementation of ellipsoidForward (src/cg/geo/projection_type.cpp), matching
// its formula exactly, used only to measure whether that function's arithmetic precision changes
// the round-trip result (T-0016 review finding 3 / Q-B). Not a production code path.
DoublePoint ellipsoidForwardDouble(LonLat p, double e) noexcept {
    const double rx = 0.5 + p.lon / 360.0;
    const double z = std::sin(p.lat * std::numbers::pi / 180.0);
    const double ry = 0.5 - (std::atanh(z) - e * std::atanh(e * z)) / (2.0 * std::numbers::pi);
    return {rx, ry};
}
} // namespace

TEST(ProjectionType, EllipsoidForwardDoubleAgreesWithLongDoubleWithinD11Bound) {
    // T-0016 review finding 3 / Q-B: ellipsoidForward was written in `long double`, but ADR-0006
    // D-11 permits that only in the ellipsoidal INVERSE (deviation 6). This sweeps the D-11 grid's
    // latitudes feeding BOTH a `double`-only forward (ellipsoidForwardDouble above) and the
    // production forward through the same (unchanged, `long double`) inverse, and asserts the two
    // round trips agree to well inside the D-11 bound (1e-9 radians, ~5.73e-8 degrees) -- in fact
    // to under 1e-9 degrees, per the review's sweep. Longitude does not enter the ellipsoid math
    // (finding 11), so one representative longitude is enough.
    ProjectionTypeFactory factory = makeFactory();
    const auto yandex = factory.byCode(3395);
    const double a = yandex->datum()->radiusA();
    const double b = yandex->datum()->radiusB();
    const double e = std::sqrt(a * a - b * b) / a;
    for (double lat = -85.0; lat <= 85.0; lat += 0.01) {
        const LonLat p{10.0, lat};
        const auto rProduction = yandex->lonLat2Relative(p);
        const auto rDouble = ellipsoidForwardDouble(p, e);
        const auto backProduction = yandex->relative2LonLat(rProduction);
        const auto backDouble = yandex->relative2LonLat(rDouble);
        ASSERT_NEAR(backDouble.lat, backProduction.lat, 1e-9) << "lat=" << lat;
    }
}

TEST(ProjectionType, LonLat2MetrUsesRadiusA) {
    ProjectionTypeFactory factory = makeFactory();
    const auto google = factory.byCode(3857);
    const auto m = google->lonLat2Metr({0.0, 0.0});
    EXPECT_NEAR(m.x(), 0.0, 1e-6);
    EXPECT_NEAR(m.y(), 0.0, 1e-6);
    const auto mEast = google->lonLat2Metr({180.0, 0.0});
    EXPECT_NEAR(mEast.x(), 2.0 * std::numbers::pi * google->datum()->radiusA() * 0.5, 1e-3);
}

TEST(ProjectionType, Metr2LonLatIsNotTheInverse) {
    // Pins the legacy sign defect at u_ProjectionTypeBase.pas:322: `x * mul - 0.5` where `+ 0.5`
    // is the inverse of LonLat2Metr (docs/specs/geo.md §13 Q-3). Reproduced, not fixed.
    ProjectionTypeFactory factory = makeFactory();
    const auto google = factory.byCode(3857);
    const LonLat original{20.0, 30.0};
    const auto metr = google->lonLat2Metr(original);
    const auto roundTripped = google->metr2LonLat(metr);
    EXPECT_GT(std::abs(roundTripped.lon - original.lon), 1.0); // very much NOT the inverse
}

TEST(ProjectionType, IsSameByPointer) {
    ProjectionTypeFactory factory = makeFactory();
    const auto a = factory.byCode(3857);
    EXPECT_TRUE(a->isSame(*a));
}

TEST(ProjectionType, IsSameByHash) {
    ProjectionTypeFactory factory = makeFactory();
    EXPECT_TRUE(factory.byCode(3857)->isSame(*factory.byCode(3785))); // alias, same interned type
}

TEST(ProjectionType, IsSameByDatumAndEpsg) {
    ProjectionTypeFactory factory = makeFactory();
    EXPECT_FALSE(factory.byCode(3857)->isSame(*factory.byCode(53004))); // same class, diff datum/epsg
    EXPECT_FALSE(factory.byCode(3395)->isSame(*factory.byCode(4326)));  // same datum, diff epsg
}

TEST(ProjectionType, ValidateClampsBeforeForward) {
    ProjectionTypeFactory factory = makeFactory();
    const auto geo = factory.byCode(4326);
    const auto r = geo->lonLat2Relative({-500.0, 40.0}); // clamps to -180 internally
    const auto expected = geo->lonLat2Relative({-180.0, 40.0});
    EXPECT_EQ(r, expected);
}

TEST(ProjectionType, NativeAgreesWithProjWithin1e7) {
    // ADR-0006 "Harder": the built-in Mercator (u_ProjectionTypeMercatorOnSphere.pas, used for
    // every tile index) and PROJ's EPSG:3857 (custom-CRS display/import only) are two different
    // code paths that happen to compute the same formula; this pins them agreeing to within 1e-7
    // degrees round-trip, so a future PROJ change to 3857's definition is caught here rather than
    // in a rendered tile.
    ProjConverterFactory projFactory;
    if (!projFactory.isAvailable()) {
        GTEST_SKIP() << "PROJ not available (not built with PROJ, or proj.db not found)";
    }
    const auto proj = projFactory.byEpsg(3857);
    ASSERT_NE(proj, nullptr);

    ProjectionTypeFactory factory = makeFactory();
    const auto google = factory.byCode(3857);

    const LonLat p{37.5, 55.0};
    const auto nativeMetr = google->lonLat2Metr(p);
    const auto projXy = proj->lonLat2XY(p);
    ASSERT_TRUE(projXy.has_value());
    EXPECT_NEAR(nativeMetr.x(), projXy->x(), 1e-3);
    EXPECT_NEAR(nativeMetr.y(), projXy->y(), 1e-3);

    const auto projLonLat = proj->xy2LonLat(*projXy);
    ASSERT_TRUE(projLonLat.has_value());
    EXPECT_NEAR(projLonLat->lon, p.lon, 1e-7);
    EXPECT_NEAR(projLonLat->lat, p.lat, 1e-7);
}
