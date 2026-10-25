// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-004, FEAT-GEO-005, FEAT-GEO-006, FEAT-GEO-007; docs/specs/geo.md:1969-2086
// (§6.2), test plan §11.2 (item 13, u_CoordToStringConverter_Test.pas -> ported below).
//
// Every expected string in the table below was hand-derived by replaying
// u_CoordToStringConverter.pas's DegrToStr algorithm (Trunc-based quantisation, then the
// FormatFloat mask) in Python during porting; non-obvious rows carry an inline note. See
// docs/handoffs/T-0019A1.md for the derivation script and the DMS CutZero finding.
#include "cg/geo/coord_to_string.h"
#include "cg/geo/coord_transformer.h"
#include "cg/geo/proj_converter.h"

#include <QLocale>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using cg::geo::CoordPart;
using cg::geo::CoordRepresentationSettings;
using cg::geo::CoordSysInfoType;
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

CoordRepresentationSettings settingsFor(GeogCoordShowFormat format, bool latitudeFirst = false,
                                        CoordSysInfoType infoType = CoordSysInfoType::DontShow) {
    CoordRepresentationSettings s;
    s.isLatitudeFirst = latitudeFirst;
    s.geogFormat = format;
    s.coordSysType = CoordSysType::Wgs84;
    s.coordSysInfoType = infoType;
    return s;
}

} // namespace

// --- u_CoordToStringConverter_Test.pas -> ported 1:1 (spec §11.2 item 13) -----------------------

TEST(CoordToString, LegacyPortedTestCoordToString) {
    CoordRepresentationSettings s;
    s.isLatitudeFirst = true;
    s.geogFormat = GeogCoordShowFormat::SignDegr2;
    s.projFormat = ProjCoordShowFormat::RoundedToTenth;
    s.mgrsFormat = MgrsCoordShowFormat::Splitted;
    s.coordSysType = CoordSysType::Wgs84;
    s.coordSysInfoType = CoordSysInfoType::DontShow;
    const CoordToStringConverter converter(s);

    // Note the one-argument overload (legacy calls `LonLatConvertExt(DoublePoint(10.0, 40.101),
    // [coCutZero])`, which dispatches on the converter's own coordSysType, set to Wgs84 above):
    // isLatitudeFirst is NOT applied (spec §6.2.4) -- that is exactly what this legacy test pins.
    const auto result = converter.lonLatConvertExt({10.0, 40.101}, {CoordToStringOption::CutZero});

    EXPECT_EQ(result[static_cast<int>(CoordPart::Lon)], QStringLiteral("10"));
    EXPECT_EQ(result[static_cast<int>(CoordPart::Lat)], QStringLiteral("40.101"));
}

// --- degrToStr, one row per GeogCoordShowFormat "shape", CutZero on/off (spec §6.2.1/§6.2.2) -----

namespace {
struct DegrCase {
    const char* name;
    GeogCoordShowFormat format;
    double lon;
    double lat;
    bool cutZero;
    QString expectedLon;
    QString expectedLat;
};
} // namespace

TEST(CoordToString, DegrToStrTableAcrossFormatsAndCutZero) {
    // clang-format off
    const std::vector<DegrCase> cases = {
        // DMS: int(deg) + deg-sign + 2-digit minutes + ' + format("00.0000", sec) + "
        {"CharDMS_NoCutZero", GeogCoordShowFormat::CharDegrMinSec, 55.7558, -33.5, false,
         QStringLiteral(u"E55°45'20.8800\""), QStringLiteral(u"S33°30'00.0000\"")},
        // DMS CutZero is a legacy no-op (verified while porting -- see DmsCutZeroIsALegacyNoOp
        // below): identical to the NoCutZero row even though -33.5 has all-zero seconds/minutes.
        {"CharDMS_CutZeroIsANoOp", GeogCoordShowFormat::CharDegrMinSec, 55.7558, -33.5, true,
         QStringLiteral(u"E55°45'20.8800\""), QStringLiteral(u"S33°30'00.0000\"")},
        {"SignDMS", GeogCoordShowFormat::SignDegrMinSec, 55.7558, -89.99999999, false,
         QStringLiteral(u"55°45'20.8800\""), QStringLiteral(u"-89°59'59.9999\"")},

        // DDM: int(deg) + deg-sign + format("00.000000", min) + '
        {"CharDDM_NoCutZero", GeogCoordShowFormat::CharDegrMin, 40.101, -33.5, false,
         QStringLiteral(u"E40°06.060000'"), QStringLiteral(u"S33°30.000000'")},
        // 12°34.50000' -> 12°34.5' style trim: -33.5's minutes are exactly 30.000000 -> the whole
        // field (including the integer part) collapses per spec's cascading DDM algorithm.
        {"CharDDM_CutZero", GeogCoordShowFormat::CharDegrMin, 40.101, -33.5, true,
         QStringLiteral(u"E40°06.06'"), QStringLiteral(u"S33°30'")},
        {"SignDDM_WholeDegreeCutZero", GeogCoordShowFormat::SignDegrMin, 12.0, -1.0, true,
         QStringLiteral(u"12°"), QStringLiteral(u"-1°")},

        // DD: format("0.00000000", deg) + deg-sign
        {"CharDD_NoCutZero", GeogCoordShowFormat::CharDegr, 55.7558, 0.0, false,
         QStringLiteral(u"E55.75580000°"), QStringLiteral(u"0.00000000°")},
        {"CharDD_CutZero", GeogCoordShowFormat::CharDegr, 55.7558, 0.0, true,
         QStringLiteral(u"E55.7558°"), QStringLiteral(u"0°")},

        // DD2: format("0.00000000", deg), no degree sign
        {"CharDD2_CutZero", GeogCoordShowFormat::CharDegr2, 40.101, 12.0, true,
         QStringLiteral(u"E40.101"), QStringLiteral(u"N12")},
        {"SignDD2_NearBoundaryNoCutZero", GeogCoordShowFormat::SignDegr2, 179.9999999, -89.99999999,
         false, QStringLiteral(u"179.99999990"), QStringLiteral(u"-89.99999999")},
        {"SignDD2_NearBoundaryCutZero", GeogCoordShowFormat::SignDegr2, 179.9999999, -89.99999999,
         true, QStringLiteral(u"179.9999999"), QStringLiteral(u"-89.99999999")},
        {"SignDD2_TinyValue", GeogCoordShowFormat::SignDegr2, 0.000001, 0.0, true,
         QStringLiteral(u"0.000001"), QStringLiteral(u"0")},
        // Regression (docs/handoffs/T-0019A1-review.md finding 1): 12.001953125 (= 12 + 1/512) is
        // an exact tie at the 9th decimal. Legacy's FormatFloat rounds half away from zero ->
        // ...13; snprintf's %.*f rounds the binary value half-to-even -> ...12 (wrong). No CutZero,
        // so the full 8-decimal mask is visible in both signs.
        {"SignDD2_HalfAwayFromZeroTie", GeogCoordShowFormat::SignDegr2, 12.001953125, -12.001953125,
         false, QStringLiteral(u"12.00195313"), QStringLiteral(u"-12.00195313")},
    };
    // clang-format on

    for (const auto& c : cases) {
        SCOPED_TRACE(c.name);
        const CoordToStringConverter converter(settingsFor(c.format));
        const auto result =
            converter.lonLatConvertExt({c.lon, c.lat}, CoordSysType::Wgs84,
                                       c.cutZero ? cg::geo::CoordToStringOptions{CoordToStringOption::CutZero}
                                                 : cg::geo::CoordToStringOptions{});
        EXPECT_EQ(result[static_cast<int>(CoordPart::Lon)], c.expectedLon);
        EXPECT_EQ(result[static_cast<int>(CoordPart::Lat)], c.expectedLat);
    }
}

TEST(CoordToString, DmsCutZeroIsALegacyNoOp) {
    // u_CoordToStringConverter.pas:174-184: the guard checks the last 4 characters for a literal
    // '.' followed by two zeros and the closing quote, but the seconds field is always rendered
    // with 4 decimal digits (mask "00.0000"), so the '.' is always 6 characters from the end --
    // never inside that window. Even the all-zero case (0°00'00.0000") is left untouched. This was
    // verified by construction and empirically (2,000,000 random degree values) while porting; see
    // docs/handoffs/T-0019A1.md and the ticket's Open Questions.
    const CoordToStringConverter converter(settingsFor(GeogCoordShowFormat::CharDegrMinSec));
    const auto noCutZero = converter.lonLatConvertExt({0.0, 0.0}, CoordSysType::Wgs84, {});
    const auto cutZero =
        converter.lonLatConvertExt({0.0, 0.0}, CoordSysType::Wgs84, {CoordToStringOption::CutZero});
    EXPECT_EQ(noCutZero[static_cast<int>(CoordPart::Lat)], cutZero[static_cast<int>(CoordPart::Lat)]);
    EXPECT_EQ(cutZero[static_cast<int>(CoordPart::Lat)], QStringLiteral(u"0°00'00.0000\""));
}

// --- hemisphere markers (spec §6.2.3) -------------------------------------------------------------

TEST(CoordToString, MarkerIsEmptyAtExactlyZeroForCharFormats) {
    const CoordToStringConverter converter(settingsFor(GeogCoordShowFormat::CharDegr2));
    const auto result =
        converter.lonLatConvertExt({0.0, -0.0}, CoordSysType::Wgs84, {CoordToStringOption::CutZero});
    // No E/W/N/S letter at all, not even a blank prefix -- exactly the bare digits.
    EXPECT_EQ(result[static_cast<int>(CoordPart::Lon)], QStringLiteral("0"));
    EXPECT_EQ(result[static_cast<int>(CoordPart::Lat)], QStringLiteral("0"));
}

TEST(CoordToString, SignFormatEmitsNoMinusForZeroOrPositive) {
    const CoordToStringConverter converter(settingsFor(GeogCoordShowFormat::SignDegr2));
    const auto result = converter.lonLatConvertExt({0.0, 12.5}, CoordSysType::Wgs84, {});
    EXPECT_EQ(result[static_cast<int>(CoordPart::Lon)], QStringLiteral("0.00000000"));
    EXPECT_EQ(result[static_cast<int>(CoordPart::Lat)], QStringLiteral("12.50000000"));
}

// --- lonLatConvert: join order, space-skipping, and the LatitudeFirst asymmetry (spec §6.2.4) ----

TEST(CoordToString, LonLatConvertJoinsWithSingleSpaceSkippingEmptyParts) {
    const CoordToStringConverter converter(
        settingsFor(GeogCoordShowFormat::SignDegr2, /*latitudeFirst=*/false));
    EXPECT_EQ(converter.lonLatConvert({10.0, 40.101}, {CoordToStringOption::CutZero}),
              QStringLiteral("10 40.101"));
}

TEST(CoordToString, LonLatConvertAppliesLatitudeFirstUnlikeLonLatConvertExt) {
    const CoordToStringConverter converter(
        settingsFor(GeogCoordShowFormat::SignDegr2, /*latitudeFirst=*/true));
    // lonLatConvert applies isLatitudeFirst (spec's asymmetry) -> lat printed before lon.
    EXPECT_EQ(converter.lonLatConvert({10.0, 40.101}, {CoordToStringOption::CutZero}),
              QStringLiteral("40.101 10"));
    // The two-arg lonLatConvertExt overload never applies it, regardless of the setting.
    const auto ext =
        converter.lonLatConvertExt({10.0, 40.101}, CoordSysType::Wgs84, {CoordToStringOption::CutZero});
    EXPECT_EQ(ext[static_cast<int>(CoordPart::Lon)], QStringLiteral("10"));
    EXPECT_EQ(ext[static_cast<int>(CoordPart::Lat)], QStringLiteral("40.101"));
}

// --- coordSysInfo (spec §6.2.4 last paragraph) ----------------------------------------------------

TEST(CoordToString, CoordSysInfoDontShowIsAlwaysEmpty) {
    CoordRepresentationSettings s = settingsFor(GeogCoordShowFormat::CharDegrMinSec);
    s.coordSysInfoType = CoordSysInfoType::DontShow;
    s.coordSysType = CoordSysType::Utm;
    const CoordToStringConverter converter(s);
    EXPECT_TRUE(converter.coordSysInfo({0.0, 0.0}).isEmpty());
}

TEST(CoordToString, CoordSysInfoShowExceptWgs84HidesOnlyWgs84) {
    CoordRepresentationSettings s = settingsFor(GeogCoordShowFormat::CharDegrMinSec);
    s.coordSysInfoType = CoordSysInfoType::ShowExceptWgs84;
    s.coordSysType = CoordSysType::Wgs84;
    const CoordToStringConverter wgs84(s);
    EXPECT_TRUE(wgs84.coordSysInfo({0.0, 0.0}).isEmpty());

    s.coordSysType = CoordSysType::Sk42;
    const CoordToStringConverter sk42(s);
    EXPECT_EQ(sk42.coordSysInfo({0.0, 0.0}), QStringLiteral("SK-42"));
}

TEST(CoordToString, CoordSysInfoUtmSwapsToUpsNearThePoles) {
    CoordRepresentationSettings s = settingsFor(GeogCoordShowFormat::CharDegrMinSec);
    s.coordSysInfoType = CoordSysInfoType::ShowForAll;
    s.coordSysType = CoordSysType::Utm;
    const CoordToStringConverter converter(s);
    EXPECT_EQ(converter.coordSysInfo({0.0, 45.0}), QStringLiteral("WGS 84 / UTM"));
    EXPECT_EQ(converter.coordSysInfo({0.0, 84.0}), QStringLiteral("WGS 84 / UPS"));
    EXPECT_EQ(converter.coordSysInfo({0.0, -80.0}), QStringLiteral("WGS 84 / UPS"));
}

// --- non-WGS84 dispatch: T-0019B2 wires CoordTransformer / the UTM-MGRS-GaussKruger CRS table ----

TEST(CoordToString, FailedTransformYieldsLiteralNaN) {
    // Legacy's own failure sentinel (spec §6.2.4: literal "NaN" in Lon/Lat, empty Zone) is only
    // observable when the transform itself fails -- with PROJ available every CoordSysType below
    // succeeds for an ordinary mid-latitude point, so this pins the sentinel for the one condition
    // that reliably fails it in this environment: PROJ unavailable (ticket: "what is returned when
    // PROJ is unavailable -> legacy's failure sentinel"). With PROJ present, WiringRoundTripThrough...
    // (coord_transformer_test.cpp) exercises the success path for every non-WGS84 system instead.
    if (ProjConverterFactory().isAvailable()) {
        GTEST_SKIP() << "PROJ available -- every CoordSysType transform succeeds, nothing to pin here";
    }
    const CoordToStringConverter converter(settingsFor(GeogCoordShowFormat::CharDegrMinSec));
    for (const auto sys : {CoordSysType::Utm, CoordSysType::Mgrs, CoordSysType::Sk42, CoordSysType::Sk42Gk,
                           CoordSysType::Gsk2011, CoordSysType::Gsk2011Gk}) {
        SCOPED_TRACE(static_cast<int>(sys));
        const auto result = converter.lonLatConvertExt({37.6, 55.75}, sys, {});
        EXPECT_EQ(result[static_cast<int>(CoordPart::Zone)], QString());
        EXPECT_EQ(result[static_cast<int>(CoordPart::Lon)], QStringLiteral("NaN"));
        EXPECT_EQ(result[static_cast<int>(CoordPart::Lat)], QStringLiteral("NaN"));
    }
}

TEST(CoordToString, OneArgLonLatConvertExtDispatchesOnTheConverterSOwnCoordSysType) {
    CoordRepresentationSettings s = settingsFor(GeogCoordShowFormat::CharDegrMinSec);
    s.coordSysType = CoordSysType::Utm;
    const CoordToStringConverter converter(s);
    // NOTE: `lonLatConvertExt(lonLat, {})` used to be genuinely ambiguous at this call site -- `{}`
    // bound equally well to the 1-arg overload's `CoordToStringOptions options` and to the 2-arg
    // overload's `CoordSysType coordSysType` (a scalar value-initialization outranks the
    // class-type default constructor in overload resolution), so it silently picked the 2-arg
    // overload with coordSysType defaulted to Wgs84 -- not "the converter's own coordSysType" at
    // all (docs/handoffs/T-0019A1-review.md finding 3). Fixed by dropping the default on the 2-arg
    // overload's `options` (coord_to_string.h); `f(ll)` now resolves unambiguously to the 1-arg
    // overload, exercising exactly the dispatch this test names -- compared directly against the
    // explicit 2-arg call with the same CoordSysType, rather than hard-coding a "NaN" outcome that
    // depends on PROJ availability.
    const auto oneArg = converter.lonLatConvertExt({37.6, 55.75});
    const auto twoArg = converter.lonLatConvertExt({37.6, 55.75}, CoordSysType::Utm, {});
    EXPECT_EQ(oneArg, twoArg);
}

// --- non-WGS84 formatting: _ProjectedCoordToStr / _MgrsCoordToStr / _ZoneInfoToStr (§6.2.4) ------
// Every test below needs PROJ (a real UtmCoord/MgrsCoord/GaussKrugerCoord to format), skipping
// cleanly when it is unavailable, same as coord_transformer_test.cpp.

namespace {

class CoordToStringProjTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!ProjConverterFactory().isAvailable()) {
            GTEST_SKIP() << "PROJ not available (not built with PROJ, or proj.db not found)";
        }
    }
};

} // namespace

TEST_F(CoordToStringProjTest, ProjMaskRoundedToWholeHasNoTrailingPoint) {
    // FormatFloat('0.', X): a mask with no digit placeholders after '.' omits the decimal point
    // entirely (u_CoordToStringConverter.pas:363's CFloatFormat table, RoundedToWhole = '0.').
    CoordRepresentationSettings s;
    s.coordSysType = CoordSysType::Utm;
    s.projFormat = ProjCoordShowFormat::RoundedToWhole;
    const CoordToStringConverter converter(s);
    const auto result = converter.lonLatConvertExt({37.5, 55.0}, {});
    const QString& lon = result[static_cast<int>(CoordPart::Lon)];
    EXPECT_FALSE(lon.contains(QLatin1Char('.')));
    EXPECT_EQ(lon, QStringLiteral("404051")); // 404050.944491686, rounded half away from zero
}

TEST_F(CoordToStringProjTest, ProjHasNoThousandsSeparatorUnderAnyLocale) {
    // The projected-coordinate formatting is manual digit arithmetic (formatFixedRoundHalfAwayFromZero
    // via projCoordToStr), never QLocale/snprintf grouping -- unlike u_CoordToStringConverter.pas's
    // own FormatFloat call, which is fed an explicit FFormatSettings and so is also locale-stable,
    // but this pins the port's independence from the *global* Qt locale specifically.
    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale(QLocale::Russian));
    struct RestoreLocale {
        QLocale saved;
        ~RestoreLocale() { QLocale::setDefault(saved); }
    } restore{previous};

    CoordRepresentationSettings s;
    s.coordSysType = CoordSysType::Utm;
    s.projFormat = ProjCoordShowFormat::RoundedToThousandths;
    const CoordToStringConverter converter(s);
    const auto result = converter.lonLatConvertExt({37.5, 55.0}, {});
    const QString& lon = result[static_cast<int>(CoordPart::Lon)];
    EXPECT_FALSE(lon.contains(QLatin1Char(' ')));
    EXPECT_FALSE(lon.contains(QLatin1Char(',')));
    EXPECT_EQ(lon, QStringLiteral("404050.944"));
}

TEST_F(CoordToStringProjTest, MgrsSplittedLayout) {
    // _MgrsCoordToStr, msfSplitted branch, u_CoordToStringConverter.pas:388-390: Zone gets
    // "<zone><band> <digraph>", Lon/Lat get the 5-digit easting/northing separately.
    CoordRepresentationSettings s;
    s.coordSysType = CoordSysType::Mgrs;
    s.mgrsFormat = MgrsCoordShowFormat::Splitted;
    const CoordToStringConverter converter(s);
    const auto result = converter.lonLatConvertExt({37.5, 55.0}, {});
    EXPECT_EQ(result[static_cast<int>(CoordPart::Zone)], QStringLiteral("37U DA"));
    EXPECT_EQ(result[static_cast<int>(CoordPart::Lon)], QStringLiteral("04050"));
    EXPECT_EQ(result[static_cast<int>(CoordPart::Lat)], QStringLiteral("95820"));
}

TEST_F(CoordToStringProjTest, MgrsJoinedPutsEverythingInZone) {
    // _MgrsCoordToStr, msfJoined branch, u_CoordToStringConverter.pas:392-396: Zone gets the whole
    // MGRS string, Lon/Lat are cleared.
    CoordRepresentationSettings s;
    s.coordSysType = CoordSysType::Mgrs;
    s.mgrsFormat = MgrsCoordShowFormat::Joined;
    const CoordToStringConverter converter(s);
    const auto result = converter.lonLatConvertExt({37.5, 55.0}, {});
    EXPECT_EQ(result[static_cast<int>(CoordPart::Zone)], QStringLiteral("37UDA0405095820"));
    EXPECT_TRUE(result[static_cast<int>(CoordPart::Lon)].isEmpty());
    EXPECT_TRUE(result[static_cast<int>(CoordPart::Lat)].isEmpty());
}

TEST_F(CoordToStringProjTest, GaussKrugerSwapsLonAndLat) {
    // _ProjectedCoordToStr(VGauss.X, VGauss.Y, {Swap=}True, ...), u_CoordToStringConverter.pas:444:
    // always swapped, regardless of hemisphere -- Lon shows the northing, Lat shows the easting.
    CoordRepresentationSettings s;
    s.coordSysType = CoordSysType::Sk42Gk;
    s.projFormat = ProjCoordShowFormat::RoundedToWhole;
    const CoordToStringConverter converter(s);

    CoordTransformer transformer;
    GaussKrugerCoord gauss{};
    ASSERT_TRUE(transformer.wgs84ToGaussKruger(CoordSysType::Sk42Gk, {37.5, 55.0}, gauss));

    const auto result = converter.lonLatConvertExt({37.5, 55.0}, {});
    EXPECT_EQ(result[static_cast<int>(CoordPart::Lon)],
              QString::number(static_cast<qint64>(std::llround(gauss.y))));
    EXPECT_EQ(result[static_cast<int>(CoordPart::Lat)],
              QString::number(static_cast<qint64>(std::llround(gauss.x))));
}

TEST_F(CoordToStringProjTest, UtmDoesNotSwapButUpsDoes) {
    // _ProjectedCoordToStr(..., {Swap=}False, ...) for plain UTM vs {Swap=}True for the UPS
    // fallback, u_CoordToStringConverter.pas:449-460.
    CoordRepresentationSettings s;
    s.coordSysType = CoordSysType::Utm;
    s.projFormat = ProjCoordShowFormat::RoundedToWhole;
    const CoordToStringConverter converter(s);

    UtmCoord utm{};
    ASSERT_TRUE(CoordTransformer::wgs84ToUtm({37.5, 55.0}, utm));
    const auto utmResult = converter.lonLatConvertExt({37.5, 55.0}, {});
    EXPECT_EQ(utmResult[static_cast<int>(CoordPart::Lon)],
              QString::number(static_cast<qint64>(std::llround(utm.x))));
    EXPECT_EQ(utmResult[static_cast<int>(CoordPart::Lat)],
              QString::number(static_cast<qint64>(std::llround(utm.y))));

    UtmCoord ups{};
    ASSERT_TRUE(CoordTransformer::wgs84ToUps({10.0, 85.0}, ups));
    const auto upsResult = converter.lonLatConvertExt({10.0, 85.0}, {});
    EXPECT_EQ(upsResult[static_cast<int>(CoordPart::Lon)],
              QString::number(static_cast<qint64>(std::llround(ups.y))));
    EXPECT_EQ(upsResult[static_cast<int>(CoordPart::Lat)],
              QString::number(static_cast<qint64>(std::llround(ups.x))));
}
