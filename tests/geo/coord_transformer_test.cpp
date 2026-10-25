// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-005, FEAT-GEO-006, FEAT-GEO-007; docs/specs/geo.md:2178-2238 (§6.4), test plan
// §11.2 ("coord_transformer_test.cpp (14)"). No legacy DUnit test exists for u_CoordTransformer.pas
// or Proj4.UTM.pas/Proj4.GaussKruger.pas (Test/ has none), so every case here is derived by hand
// from the ported zone/band/digraph arithmetic, or from `cs2cs` (PROJ 9.8.1) run with the exact
// legacy init strings under test (oracle command recorded per case). This file is exactly the
// Architect's §11.2 list (14 named cases) plus one extra case (WiringRoundTripThrough...) proving
// the actual T-0019B2 deliverable -- the wiring into coord_to_string.cpp/coord_from_string.cpp --
// works end to end, per the ticket's explicit round-trip requirement.
//
// "The first six" (spec §11.2) are pure zone/band arithmetic and run without PROJ -- see
// coord_transformer.cpp's wgs84ToUtm/wgs84ToUps: Zone/Band/IsNorth are written before the PROJ leg
// is attempted, mirroring legacy's own mutate-the-out-param-in-place semantics
// (geodetic_wgs84_to_utm, Proj4.UTM.pas:145-155). Every other case needs a real PROJ transform and
// is skipped via CoordTransformerProjTest::SetUp when PROJ is unavailable.
#include "cg/geo/coord_from_string.h"
#include "cg/geo/coord_to_string.h"
#include "cg/geo/coord_transformer.h"
#include "cg/geo/proj_converter.h"

#include <gtest/gtest.h>

#include <vector>

using cg::geo::CoordFromStringParser;
using cg::geo::CoordPart;
using cg::geo::CoordRepresentationSettings;
using cg::geo::CoordSysType;
using cg::geo::CoordToStringConverter;
using cg::geo::CoordToStringOption;
using cg::geo::CoordTransformer;
using cg::geo::GaussKrugerCoord;
using cg::geo::GeogCoordShowFormat;
using cg::geo::LonLat;
using cg::geo::MgrsCoord;
using cg::geo::MgrsCoordShowFormat;
using cg::geo::ProjConverterFactory;
using cg::geo::ProjCoordShowFormat;
using cg::geo::UtmCoord;

namespace {

class CoordTransformerProjTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!ProjConverterFactory().isAvailable()) {
            GTEST_SKIP() << "PROJ not available (not built with PROJ, or proj.db not found)";
        }
    }
};

} // namespace

// --- pure zone/band arithmetic -- no PROJ needed (spec §11.2 "the first six") -------------------

TEST(CoordTransformer, UtmZoneFromLongitude) {
    // wgs84_long_to_utm_zone, Proj4.UTM.pas:75-81. lat=10 keeps the band well clear of the
    // Norway/Svalbard exception bands (V/X), so this pins the base zone formula alone.
    struct Row {
        double lon;
        int zone;
    };
    const std::vector<Row> rows = {
        {-180.0, 1}, {-177.0, 1}, {-174.0, 2},      {-3.0, 30},  {0.0, 31},
        {3.0, 31},   {177.0, 60}, {179.999999, 60}, {180.0, 60},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.lon);
        UtmCoord out;
        const bool ok = CoordTransformer::wgs84ToUtm(LonLat{row.lon, 10.0}, out);
        (void)ok; // PROJ may be absent here; only Zone is pinned (out.zone is set regardless).
        EXPECT_EQ(out.zone, row.zone);
    }
}

TEST(CoordTransformer, UtmNorwayException) {
    // wgs84_longlat_to_utm_zone, Proj4.UTM.pas:96-98: band V (lat in [56,64)), zone 31 bumped to
    // 32 only when lon >= 3.
    UtmCoord out;
    bool ok = CoordTransformer::wgs84ToUtm(LonLat{2.9, 61.0}, out);
    (void)ok;
    EXPECT_EQ(out.zone, 31);
    EXPECT_EQ(out.band, QChar(u'V'));

    ok = CoordTransformer::wgs84ToUtm(LonLat{3.0, 61.0}, out);
    (void)ok;
    EXPECT_EQ(out.zone, 32);
    EXPECT_EQ(out.band, QChar(u'V'));
}

TEST(CoordTransformer, UtmSvalbardExceptions) {
    // Proj4.UTM.pas:100-108, all six adjustments; band X is lat in [72,84).
    struct Row {
        double lon;
        int zone;
    };
    const std::vector<Row> rows = {
        {7.0, 31},  // base 32, lon < 9   -> 31
        {10.0, 33}, // base 32, lon >= 9  -> 33
        {19.0, 33}, // base 34, lon < 21  -> 33
        {22.0, 35}, // base 34, lon >= 21 -> 35
        {31.0, 35}, // base 36, lon < 33  -> 35
        {34.0, 37}, // base 36, lon >= 33 -> 37
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.lon);
        UtmCoord out;
        const bool ok = CoordTransformer::wgs84ToUtm(LonLat{row.lon, 76.0}, out);
        (void)ok;
        EXPECT_EQ(out.zone, row.zone);
        EXPECT_EQ(out.band, QChar(u'X'));
    }
}

TEST(CoordTransformer, UtmBandTableBoundaries) {
    // CMgrsLatBands skips 'I'/'O'; the band flips exactly every 8 degrees (Floor(lat/8+10)).
    UtmCoord out;
    bool ok = CoordTransformer::wgs84ToUtm(LonLat{10.0, 7.999999}, out);
    (void)ok;
    EXPECT_EQ(out.band, QChar(u'N'));
    ok = CoordTransformer::wgs84ToUtm(LonLat{10.0, 8.0}, out);
    (void)ok;
    EXPECT_EQ(out.band, QChar(u'P')); // 'O' is skipped

    ok = CoordTransformer::wgs84ToUtm(LonLat{10.0, -8.000001}, out);
    (void)ok;
    EXPECT_EQ(out.band, QChar(u'L'));
    ok = CoordTransformer::wgs84ToUtm(LonLat{10.0, -8.0}, out);
    (void)ok;
    EXPECT_EQ(out.band, QChar(u'M'));
}

TEST(CoordTransformer, UtmOutsideRangeFails) {
    // wgs84_longlat_to_utm_zone: Result := (ALat < 84) and (ALat > -80).
    UtmCoord out;
    EXPECT_FALSE(CoordTransformer::wgs84ToUtm(LonLat{10.0, 84.0}, out));
    EXPECT_FALSE(CoordTransformer::wgs84ToUtm(LonLat{10.0, -80.0}, out));
}

TEST(CoordTransformer, UpsNorthAbove84) {
    // geodetic_wgs84_to_ups, Proj4.UTM.pas:178-206: lat >= 84 -> north; band by sign of lon. Band
    // is computed ahead of the PROJ call (coord_transformer.cpp), so this needs no PROJ either.
    UtmCoord out;
    bool ok = CoordTransformer::wgs84ToUps(LonLat{10.0, 84.0}, out);
    (void)ok;
    EXPECT_EQ(out.zone, 0);
    EXPECT_TRUE(out.isNorth);
    EXPECT_EQ(out.band, QChar(u'Z'));

    ok = CoordTransformer::wgs84ToUps(LonLat{-10.0, 90.0}, out);
    (void)ok;
    EXPECT_TRUE(out.isNorth);
    EXPECT_EQ(out.band, QChar(u'Y'));
}

// --- everything below needs a real PROJ transform -------------------------------------------------

TEST_F(CoordTransformerProjTest, UpsSouthBelowMinus80) {
    // D-9 anchor (spec §6.4 "legacy bug, fixed here"): lat = -80.5 -> south, lat = 85 -> north --
    // both branches agree on these inputs whether the switch constant is legacy's `80` or the
    // port's `-80` (only unreachable inputs would ever distinguish them), so this pins the
    // reachable behavior, not the bug itself.
    // Oracle: `echo "10.0 -80.5" | cs2cs -f "%.9f" +proj=longlat +datum=WGS84 +no_defs +type=crs
    //  +to +proj=stere +lat_0=-90 +lon_0=0 +k=0.994 +x_0=2000000 +y_0=2000000 +datum=WGS84
    //  +units=m +no_defs +type=crs` (PROJ 9.8.1).
    UtmCoord south;
    ASSERT_TRUE(CoordTransformer::wgs84ToUps(LonLat{10.0, -80.5}, south));
    EXPECT_FALSE(south.isNorth);
    EXPECT_EQ(south.band, QChar(u'B'));
    EXPECT_NEAR(south.x, 2183555.078318784, 1e-6);
    EXPECT_NEAR(south.y, 3040992.578567822, 1e-6);

    // Oracle: `echo "10.0 85.0" | cs2cs -f "%.9f" +proj=longlat +datum=WGS84 +no_defs +type=crs
    //  +to +proj=stere +lat_0=90 +lon_0=0 +k=0.994 +x_0=2000000 +y_0=2000000 +datum=WGS84
    //  +units=m +no_defs +type=crs`.
    UtmCoord north;
    ASSERT_TRUE(CoordTransformer::wgs84ToUps(LonLat{10.0, 85.0}, north));
    EXPECT_TRUE(north.isNorth);
    EXPECT_EQ(north.band, QChar(u'Z'));
    EXPECT_NEAR(north.x, 2096454.163785229, 1e-6);
    EXPECT_NEAR(north.y, 1452981.254498403, 1e-6);
}

TEST_F(CoordTransformerProjTest, MgrsDigraphTables) {
    // UTM-based (zone 37N) and UPS-based (north polar) digraph selection, Proj4.UTM.pas:238-259.
    // UTM oracle (also proj_converter_test.cpp's Utm37NRoundTripsToTheMetre): `echo "37.5 55.0" |
    // cs2cs -f "%.9f" +proj=longlat +datum=WGS84 +no_defs +type=crs +to +proj=utm +zone=37
    // +ellps=WGS84 +datum=WGS84 +units=m +no_defs +type=crs` -> (404050.944491686,
    // 6095820.349290952). col=4, row=60 -> CMgrsUtmCols[36%3=0][3]='D', CMgrsUtmRows[0][60%20=0]='A'.
    MgrsCoord utmBased;
    ASSERT_TRUE(CoordTransformer::wgs84ToMgrs(LonLat{37.5, 55.0}, utmBased));
    EXPECT_EQ(utmBased.zone, 37);
    EXPECT_EQ(utmBased.band, QChar(u'U'));
    EXPECT_EQ(utmBased.digraph[0], QChar(u'D'));
    EXPECT_EQ(utmBased.digraph[1], QChar(u'A'));

    // UPS oracle (lon=10, lat=85, see UpsSouthBelowMinus80's north half): (2096454.163785229,
    // 1452981.254498403). col=20, row=14, band 'Z' -> i=3, CMgrsUpsCols[3][20-20=0]='A',
    // CMgrsUpsRows[3][14-13=1]='B'.
    MgrsCoord upsBased;
    ASSERT_TRUE(CoordTransformer::wgs84ToMgrs(LonLat{10.0, 85.0}, upsBased));
    EXPECT_EQ(upsBased.zone, 0);
    EXPECT_EQ(upsBased.band, QChar(u'Z'));
    EXPECT_EQ(upsBased.digraph[0], QChar(u'A'));
    EXPECT_EQ(upsBased.digraph[1], QChar(u'B'));
}

TEST_F(CoordTransformerProjTest, MgrsTruncatesNotRounds) {
    // Same UTM-37N point as MgrsDigraphTables: easting 404050.944491686 -- Trunc gives 404050
    // (-> mod-100000 4050); a naive round would give 404051 (-> 4051). Trunc, not rounding, is the
    // legacy behavior (spec §6.4: "trunc(utm.x) % 100000 -- truncation, not rounding").
    MgrsCoord mgrs;
    ASSERT_TRUE(CoordTransformer::wgs84ToMgrs(LonLat{37.5, 55.0}, mgrs));
    EXPECT_EQ(mgrs.x, 4050);
}

TEST_F(CoordTransformerProjTest, MgrsRoundTrip) {
    struct Row {
        const char* name;
        LonLat lonLat;
    };
    const std::vector<Row> rows = {
        {"UtmBased", LonLat{37.5, 55.0}},
        {"UpsBased", LonLat{10.0, 85.0}},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        MgrsCoord mgrs;
        ASSERT_TRUE(CoordTransformer::wgs84ToMgrs(row.lonLat, mgrs));
        LonLat back{};
        ASSERT_TRUE(CoordTransformer::mgrsToWgs84(mgrs, back));
        // 1 m MGRS truncation resolution is well under 1e-4 degrees at any latitude.
        EXPECT_NEAR(back.lon, row.lonLat.lon, 1e-4);
        EXPECT_NEAR(back.lat, row.lonLat.lat, 1e-4);
    }
}

TEST_F(CoordTransformerProjTest, GaussKrugerZoneFromLongitude) {
    // geog_long_to_zone, Proj4.GaussKruger.pas:65-72, applied to the (tiny) SK-42-shifted
    // longitude -- both test points sit comfortably clear of their zone boundaries, so the sub-
    // 0.01-degree Helmert shift cannot move them across one.
    const CoordTransformer transformer;
    GaussKrugerCoord z7;
    ASSERT_TRUE(transformer.wgs84ToGaussKruger(CoordSysType::Sk42, LonLat{37.5, 55.0}, z7));
    EXPECT_EQ(z7.zone, 7);

    GaussKrugerCoord z54;
    ASSERT_TRUE(transformer.wgs84ToGaussKruger(CoordSysType::Sk42, LonLat{-37.5, 55.0}, z54));
    EXPECT_EQ(z54.zone, 54);
}

TEST_F(CoordTransformerProjTest, GaussKrugerFalseEastingIsZonePrefixed) {
    // x_0 = zone * 1e6 + 500000 (the Russian convention), GetProjInit, Proj4.GaussKruger.pas:96.
    // Oracle: the already-shifted SK-42 point fed through the pure tmerc leg, cross-checked
    // against the WGS84-anchored ballpark pipeline this port actually builds (both give the same
    // number -- see coord_transformer.h's file header): `echo "37.501836095 54.999971416" | cs2cs
    // -f "%.9f" +proj=longlat +datum=WGS84 +no_defs +type=crs +to +proj=tmerc +lat_0=0 +lon_0=39
    // +k=1 +x_0=7500000 +y_0=0 +ellps=krass +units=m +no_defs +type=crs` ->
    // (7404128.374529145, 6098360.848358408).
    const CoordTransformer transformer;
    GaussKrugerCoord coord;
    ASSERT_TRUE(transformer.wgs84ToGaussKruger(CoordSysType::Sk42, LonLat{37.5, 55.0}, coord));
    EXPECT_EQ(coord.zone, 7);
    EXPECT_NEAR(coord.x, 7404128.374529145, 1e-3);
    EXPECT_NEAR(coord.y, 6098360.848358408, 1e-3);
    EXPECT_EQ(static_cast<int>(coord.x / 1000000.0), coord.zone); // zone-prefixed
}

TEST_F(CoordTransformerProjTest, Sk42UsesTheLegacyTowgs84) {
    // Same Helmert oracle as proj_converter_test.cpp's Sk42MatchesTheLegacyHelmert: `echo "37.5
    // 55.0" | cs2cs -f "%.9f" +proj=longlat +datum=WGS84 +no_defs +type=crs +to +proj=longlat
    // +ellps=krass +towgs84=23.57,-140.95,-79.8,0,0.35,0.79,-0.22 +no_defs +type=crs`.
    const CoordTransformer transformer;
    LonLat geog{37.5, 55.0};
    ASSERT_TRUE(transformer.wgs84ToGeographic(CoordSysType::Sk42, geog));
    EXPECT_NEAR(geog.lon, 37.501836095, 1e-7);
    EXPECT_NEAR(geog.lat, 54.999971416, 1e-7);

    LonLat back = geog;
    ASSERT_TRUE(transformer.geographicToWgs84(CoordSysType::Sk42, back));
    EXPECT_NEAR(back.lon, 37.5, 1e-7);
    EXPECT_NEAR(back.lat, 55.0, 1e-7);
}

TEST_F(CoordTransformerProjTest, Gsk2011UsesTheLegacyTowgs84) {
    // Oracle: `echo "37.5 55.0" | cs2cs -f "%.9f" +proj=longlat +datum=WGS84 +no_defs +type=crs
    // +to +proj=longlat +a=6378136.5 +rf=298.2564151
    // +towgs84=0.013,-0.092,-0.03,-0.001738,0.003559,-0.004263,0.0074 +no_defs +type=crs`.
    const CoordTransformer transformer;
    LonLat geog{37.5, 55.0};
    ASSERT_TRUE(transformer.wgs84ToGeographic(CoordSysType::Gsk2011, geog));
    EXPECT_NEAR(geog.lon, 37.500002759, 1e-7);
    EXPECT_NEAR(geog.lat, 55.000001371, 1e-7);
}

// --- T-0019B2's actual deliverable: the wiring through both consumer classes, end to end (not in
// spec §11.2's list, which tests CoordTransformer in isolation; ticket-required coverage) --------

TEST_F(CoordTransformerProjTest, WiringRoundTripThroughCoordToStringAndCoordFromString) {
    struct Row {
        const char* name;
        CoordSysType type;
    };
    const std::vector<Row> rows = {
        {"Utm", CoordSysType::Utm},         {"Mgrs", CoordSysType::Mgrs},
        {"Sk42", CoordSysType::Sk42},       {"Sk42Gk", CoordSysType::Sk42Gk},
        {"Gsk2011", CoordSysType::Gsk2011}, {"Gsk2011Gk", CoordSysType::Gsk2011Gk},
    };
    const LonLat origin{37.5, 55.0};

    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        CoordRepresentationSettings settings;
        settings.coordSysType = row.type;
        settings.geogFormat = GeogCoordShowFormat::SignDegr2; // plain signed decimal, round-trips
        settings.projFormat = ProjCoordShowFormat::RoundedToThousandths;
        settings.mgrsFormat = MgrsCoordShowFormat::Joined; // one self-contained Zone string

        const CoordToStringConverter toString(settings);
        const CoordFromStringParser fromString(settings);

        const auto parts = toString.lonLatConvertExt(origin, {CoordToStringOption::IncludeZone});
        const QString& zone = parts[static_cast<int>(CoordPart::Zone)];
        const QString& lon = parts[static_cast<int>(CoordPart::Lon)];
        const QString& lat = parts[static_cast<int>(CoordPart::Lat)];
        ASSERT_NE(lon, QStringLiteral("NaN")) << "formatting itself failed -- nothing to round-trip";

        LonLat back{};
        bool ok = false;
        double tolerance = 1e-6;
        switch (row.type) {
        case CoordSysType::Utm:
        case CoordSysType::Sk42Gk:
        case CoordSysType::Gsk2011Gk:
            // The "Lon"/"Lat" display slots are exactly what a UI's Lon/Lat text boxes would hold
            // (including Gauss-Kruger's own lon/lat swap, u_CoordToStringConverter.pas:444) -- feed
            // them back the same way.
            ok = fromString.tryStrToCoord(lon, lat, zone, back);
            break;
        case CoordSysType::Mgrs:
            ok = fromString.tryStrToCoord(zone, back); // Joined format: one self-contained string
            tolerance = 1e-4;                          // 1 m MGRS resolution
            break;
        default: // Sk42, Gsk2011 (geographic)
            ok = fromString.tryStrToCoord(lon, lat, back);
            break;
        }
        ASSERT_TRUE(ok);
        EXPECT_NEAR(back.lon, origin.lon, tolerance);
        EXPECT_NEAR(back.lat, origin.lat, tolerance);
    }
}
