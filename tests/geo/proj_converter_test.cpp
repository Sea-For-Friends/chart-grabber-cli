// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-002, FEAT-GEO-003. No legacy DUnit coverage exists for u_ProjConverterByProj4/
// u_ProjConverterFactory (Test/ has none) so there is nothing to port verbatim; per the 2026-09-19
// "port, don't multiply" ruling this keeps only what's needed to prove the adapter works -- a few
// transform vectors (one per distinct code path: the degrees-not-radians pin, a built-in-CRS
// cross-check, a legacy-Helmert datum shift, the proj.db fallback), the table test the ticket
// names explicitly, the D-10 interning contract, one invalid-init-string case, and (per the
// T-0020 review's MAJOR finding) one regression case for the per-thread PJ cache. 8 cases, well
// under the ticket's 10-case ceiling; docs/specs/geo.md §11.2.
//
// Whole file skips when PROJ isn't available -- either the build has no PROJ at all
// (CG_HAVE_PROJ==0, the stub in proj/proj_converter.cpp) or this machine's PROJ can't find
// proj.db (isAvailable() re-checks that at runtime, docs/specs/geo.md §7.3 rule 8).
#include "cg/geo/proj/proj4_defines.h"
#include "cg/geo/proj_converter.h"

#include <gtest/gtest.h>

using cg::geo::DoublePoint;
using cg::geo::LonLat;
using cg::geo::proj4ArgsByEpsg;
using cg::geo::ProjConverterFactory;

namespace {

class ProjConverterTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!factory_.isAvailable()) {
            GTEST_SKIP() << "PROJ not available (not built with PROJ, or proj.db not found)";
        }
    }
    ProjConverterFactory factory_;
};

} // namespace

// The degrees-not-radians anchor (docs/specs/geo.md §7.3 rule 2): a crs_to_crs object takes
// degrees for a +proj=longlat source, not radians like legacy's pj_transform. Keeping legacy's
// `* DEG_TO_RAD` would be wrong by a factor of 57.3 and land nowhere near zone 37N. Oracle:
// `echo "37.5 55.0" | cs2cs -f "%.9f" +proj=longlat +datum=WGS84 +no_defs +type=crs +to
//  +proj=utm +zone=37 +ellps=WGS84 +datum=WGS84 +units=m +no_defs +type=crs` (PROJ 9.8.1).
TEST_F(ProjConverterTest, Utm37NRoundTripsToTheMetre) {
    const auto conv = factory_.byEpsg(32637); // WGS 84 / UTM zone 37N
    ASSERT_NE(conv, nullptr);

    const LonLat origin{37.5, 55.0};
    const auto xy = conv->lonLat2XY(origin);
    ASSERT_TRUE(xy.has_value());
    EXPECT_NEAR(xy->x(), 404050.944491686, 1e-6);
    EXPECT_NEAR(xy->y(), 6095820.349290952, 1e-6);

    const auto back = conv->xy2LonLat(*xy);
    ASSERT_TRUE(back.has_value());
    EXPECT_NEAR(back->lon, origin.lon, 1e-9);
    EXPECT_NEAR(back->lat, origin.lat, 1e-9);
}

// EPSG:3857 (Pseudo-Mercator) is also what MercatorSphereProjectionType computes natively for
// every tile index (ADR-0006 "Harder": two engines, same formula); projection_type_test.cpp's
// NativeAgreesWithProjWithin1e7 cross-checks the two directly. Oracle: `echo "37.5 55.0" | cs2cs
// -f "%.9f" +proj=longlat +datum=WGS84 +no_defs +type=crs +to +proj=merc +a=6378137 +b=6378137
// +lat_ts=0 +lon_0=0 +x_0=0 +y_0=0 +k=1 +units=m +nadgrids=@null +wktext +no_defs +type=crs`.
TEST_F(ProjConverterTest, Epsg3857MatchesTheBuiltIn) {
    const auto conv = factory_.byEpsg(3857);
    ASSERT_NE(conv, nullptr);
    const auto xy = conv->lonLat2XY({37.5, 55.0});
    ASSERT_TRUE(xy.has_value());
    EXPECT_NEAR(xy->x(), 4174480.904747759, 1e-6);
    EXPECT_NEAR(xy->y(), 7361866.113051188, 1e-6);
}

// Pins ADR-0006 decision 4: SK-42 is built from the legacy `+towgs84=` Helmert string, never from
// an EPSG code, so results match the Windows build rather than whatever proj.db's own Pulkovo
// 1942 pipeline would pick. Oracle: `echo "37.5 55.0" | cs2cs -f "%.9f" +proj=longlat
// +datum=WGS84 +no_defs +type=crs +to +proj=longlat +ellps=krass
// +towgs84=23.57,-140.95,-79.8,0,0.35,0.79,-0.22 +no_defs +type=crs`.
TEST_F(ProjConverterTest, Sk42MatchesTheLegacyHelmert) {
    const auto conv = factory_.byInitString(QByteArray(cg::geo::kSk42));
    ASSERT_NE(conv, nullptr);
    const auto shifted = conv->lonLat2XY({37.5, 55.0});
    ASSERT_TRUE(shifted.has_value());
    EXPECT_NEAR(shifted->x(), 37.501836095, 1e-7);
    EXPECT_NEAR(shifted->y(), 54.999971416, 1e-7);
}

// Ports Proj4ArgsByEpsg (Proj4.Defines.pas:53-96) verbatim, including the two Gauss-Kruger ranges'
// wraparound at lon0 > 180 (2491 and 2522) and the empty-string fallback for an unknown code.
// Doesn't itself touch PROJ, but stays under the same fixture so the whole file skips uniformly
// when CG_HAVE_PROJ is 0, per the ticket's acceptance criterion.
TEST_F(ProjConverterTest, Proj4ArgsByEpsgTable) {
    EXPECT_EQ(proj4ArgsByEpsg(53004), QByteArray(cg::geo::kEsri53004));
    EXPECT_EQ(proj4ArgsByEpsg(3785), QByteArray(cg::geo::kEpsg3857)); // deprecated alias
    EXPECT_EQ(proj4ArgsByEpsg(3857), QByteArray(cg::geo::kEpsg3857));
    EXPECT_EQ(proj4ArgsByEpsg(3395), QByteArray(cg::geo::kEpsg3395));
    EXPECT_EQ(proj4ArgsByEpsg(4269), QByteArray(cg::geo::kNad83));
    EXPECT_EQ(proj4ArgsByEpsg(4326), QByteArray(cg::geo::kWgs84));

    // Pulkovo 1995: lon0 = 21 + (epsg-2463)*6; 2491 wraps 189 -> -171.
    EXPECT_EQ(proj4ArgsByEpsg(2463), "+proj=tmerc +lat_0=0 +lon_0=21 +k=1 +x_0=500000 +y_0=0 "
                                     "+ellps=krass +units=m +no_defs");
    EXPECT_EQ(proj4ArgsByEpsg(2491), "+proj=tmerc +lat_0=0 +lon_0=-171 +k=1 +x_0=500000 +y_0=0 "
                                     "+ellps=krass +units=m +no_defs");
    // Pulkovo 1942: lon0 = 9 + (epsg-2492)*6; 2522 wraps 189 -> -171.
    EXPECT_EQ(proj4ArgsByEpsg(2492), "+proj=tmerc +lat_0=0 +lon_0=9 +k=1 +x_0=500000 +y_0=0 "
                                     "+ellps=krass +units=m +no_defs");
    EXPECT_EQ(proj4ArgsByEpsg(2522), "+proj=tmerc +lat_0=0 +lon_0=-171 +k=1 +x_0=500000 +y_0=0 "
                                     "+ellps=krass +units=m +no_defs");

    EXPECT_EQ(proj4ArgsByEpsg(32601), "+proj=utm +zone=1 +ellps=WGS84 +datum=WGS84 +units=m +no_defs");
    EXPECT_EQ(proj4ArgsByEpsg(32660), "+proj=utm +zone=60 +ellps=WGS84 +datum=WGS84 +units=m +no_defs");
    EXPECT_EQ(proj4ArgsByEpsg(32701), "+proj=utm +zone=1 +south +ellps=WGS84 +datum=WGS84 +units=m +no_defs");
    EXPECT_EQ(proj4ArgsByEpsg(32760),
              "+proj=utm +zone=60 +south +ellps=WGS84 +datum=WGS84 +units=m +no_defs");

    EXPECT_TRUE(proj4ArgsByEpsg(99999).isEmpty()); // unknown to the legacy table
}

// ADR-0006 decision 5: an EPSG code the legacy table doesn't know falls through to proj.db as
// "EPSG:<code>" (proj_normalize_for_visualization applied, rule 3), instead of legacy's dead
// `+init=epsg:` fallback. EPSG:4284 (Pulkovo 1942 geographic) happens to land on proj.db's default
// operation using the same towgs84 parameters as the hardcoded SK-42 constant, which is a useful
// cross-check that the fallback path produces real numbers, not just "doesn't crash". Oracle
// (PROJ C API, matching production exactly): proj_create_crs_to_crs(ctx,
// "+proj=longlat +datum=WGS84 +no_defs +type=crs", "EPSG:4284", NULL) +
// proj_normalize_for_visualization(), point (37.5, 55.0) -> (37.501836095, 54.999971416).
TEST_F(ProjConverterTest, UnknownEpsgFallsBackToProjDb) {
    ASSERT_TRUE(proj4ArgsByEpsg(4284).isEmpty()); // not in the legacy table -> must fall back
    const auto conv = factory_.byEpsg(4284);
    ASSERT_NE(conv, nullptr);
    const auto shifted = conv->lonLat2XY({37.5, 55.0});
    ASSERT_TRUE(shifted.has_value());
    EXPECT_NEAR(shifted->x(), 37.501836095, 1e-7);
    EXPECT_NEAR(shifted->y(), 54.999971416, 1e-7);
}

// D-10 / ADR-0006 decision 9: the factory interns one ProjConverter per init string behind a
// weak_ptr cache instead of legacy's re-parse-on-every-call.
TEST_F(ProjConverterTest, FactoryInternsByInitString) {
    const auto a = factory_.byInitString(QByteArray(cg::geo::kEpsg3857));
    const auto b = factory_.byInitString(QByteArray(cg::geo::kEpsg3857));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a.get(), b.get());
}

// ADR-0006 rule 6: a failed CRS build surfaces as nullptr, never a crash or a silent identity
// transform.
TEST_F(ProjConverterTest, InvalidInitStringReturnsNullptr) {
    EXPECT_EQ(factory_.byInitString("+proj=totally_not_a_real_projection"), nullptr);
}

// Regression for the T-0020 review's MAJOR finding: ThreadProjState::pjByKey (proj_converter.cpp)
// used to be keyed on the raw ProjConverterImpl* address. ProjConverterFactory only weak_ptr-caches
// converters, so once every ProjConverterPtr for a given init string goes out of scope the
// converter is freed and a later, unrelated converter for a *different* init string can legally
// land at the same address; a thread's stale per-thread PJ* entry for the old key would then
// silently answer lookups for the new converter, transforming through the wrong CRS. Keying the
// cache on the init string instead of the address (this fix) makes that impossible by
// construction. This test does not try to force the allocator to actually reuse converter A's
// address for converter B -- that's an implementation detail this test must not depend on to pass
// deterministically -- it only proves that destroying A and then building an unrelated B still
// transforms through B's own CRS.
TEST_F(ProjConverterTest, DestroyedConverterDoesNotPoisonAnUnrelatedConverter) {
    {
        // Converter A: SK-42, the legacy-Helmert datum shift.
        const auto a = factory_.byInitString(QByteArray(cg::geo::kSk42));
        ASSERT_NE(a, nullptr);
        const auto shifted = a->lonLat2XY({37.5, 55.0});
        ASSERT_TRUE(shifted.has_value());
        EXPECT_NEAR(shifted->x(), 37.501836095, 1e-7);
        EXPECT_NEAR(shifted->y(), 54.999971416, 1e-7);
        // `a` is the only ProjConverterPtr for this key; the factory's own cache only holds a
        // weak_ptr (proj_converter.h). Falling out of scope here frees the ProjConverterImpl.
    }

    // Converter B: a different init string (EPSG:3857). If the per-thread cache were still keyed
    // on the (now-dangling, possibly-reused) address of A, this would risk transforming through
    // A's SK-42 PJ* instead of B's own.
    const auto b = factory_.byEpsg(3857);
    ASSERT_NE(b, nullptr);
    const auto xy = b->lonLat2XY({37.5, 55.0});
    ASSERT_TRUE(xy.has_value());
    EXPECT_NEAR(xy->x(), 4174480.904747759, 1e-6);
    EXPECT_NEAR(xy->y(), 7361866.113051188, 1e-6);
}
