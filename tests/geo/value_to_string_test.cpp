// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-016; docs/specs/geo.md:2227-2311 (§6.5, §6.5.1).
//
// ValToNum's one primitive (`QLocale().toString(value, 'f', digits)`) makes every string here
// depend on the *process-wide default* QLocale, exactly like legacy's global DecimalSeparator/
// ThousandSeparator -- unlike coord_to_string.cpp, which pins '.' unconditionally
// (u_CoordToStringConverter.pas:126). A fixture forces QLocale::c() (dot decimal, no thousands
// grouping -- verified empirically, see below) around every test but
// `LocaleAffectsGroupingAndSeparator`, which deliberately switches to Russian and restores after.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/geo/value_to_string.h"

#include <QLocale>
#include <QString>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

using cg::core::ConfigProviderIni;
using cg::core::IniFile;
using cg::geo::AreaStrFormat;
using cg::geo::DistStrFormat;
using cg::geo::ValueFormatSettings;
using cg::geo::ValueToStringConverter;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

ValueToStringConverter makeConverter(DistStrFormat dist, AreaStrFormat area = AreaStrFormat::Auto) {
    return ValueToStringConverter(ValueFormatSettings{dist, area});
}

class ValueToStringTest : public ::testing::Test {
protected:
    void SetUp() override {
        savedDefault_ = QLocale();
        // QLocale::c() gives '.' decimal, and -- empirically verified while porting (no thousands
        // separator is ever emitted, unlike a bare default-constructed QLocale, which picks up
        // whatever the process/environment locale is) -- deterministic across machines. It also
        // rounds an exact decimal tie AWAY FROM ZERO, matching the Delphi FormatFloat convention
        // established in docs/handoffs/T-0019A1-review.md finding 1 (verified: QLocale::c()
        // .toString(0.125, 'f', 2) == "0.13", not the round-half-to-even "0.12" glibc's printf
        // gives for the same input) -- so, unlike coord_to_string.cpp's ddToStr, no separate
        // formatFixedRoundHalfAwayFromZero-style helper is needed here: QLocale's own primitive
        // already reproduces legacy's tie rule for every decimals count this module uses.
        QLocale::setDefault(QLocale::c());
    }
    void TearDown() override { QLocale::setDefault(savedDefault_); }

private:
    QLocale savedDefault_;
};

} // namespace

// --- dataSizeConvert (u_ValueToStringConverter.pas:214-224) --------------------------------------

TEST_F(ValueToStringTest, DataSizeConvertThresholds) {
    const ValueToStringConverter conv = makeConverter(DistStrFormat::KmAndM);
    struct Row {
        double kb;
        const char* expected;
    };
    const Row rows[] = {
        {512.0, "512.0 KB"},   // <= 1024 -> KB
        {2048.0, "2.0 MB"},    // > 1024, <= 1048576 -> MB
        {2097152.0, "2.0 GB"}, // > 1048576 -> GB
        // Rounding tie, derived by hand: 1 decimal -> a tie needs the value to be an odd multiple
        // of 1/4 (10^1 has one fewer factor of 2 than the dyadic denominator: 0.25 * 10 = 2.5).
        // 0.25 <= 1024 -> KB branch; legacy FormatFloat / QLocale::c() both round the tie away
        // from zero -> "0.3", not "0.2".
        {0.25, "0.3 KB"},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.kb);
        EXPECT_EQ(conv.dataSizeConvert(r.kb), QString::fromLatin1(r.expected));
    }
}

TEST_F(ValueToStringTest, DataSizeConvertHasNoNaNGuard) {
    // Spec §6.5 closing note: NaN guards exist on distConvert/areaConvert/speedConvert/
    // altitudeConvert but NOT on dataSizeConvert -- reproduce, add none. NaN > 1048576 and
    // NaN > 1024 are both false in IEEE 754, so this falls to the KB branch and formats NaN
    // itself, never returning the "NaN" sentinel.
    const ValueToStringConverter conv = makeConverter(DistStrFormat::KmAndM);
    EXPECT_NE(conv.dataSizeConvert(kNaN), QStringLiteral("NaN"));
}

// --- distConvert (u_ValueToStringConverter.pas:226-283) ------------------------------------------

TEST_F(ValueToStringTest, DistConvertThresholdsAcrossEveryDistStrFormat) {
    struct Row {
        DistStrFormat format;
        double metres;
        const char* expected;
    };
    const Row rows[] = {
        {DistStrFormat::KmAndM, 1500.0, "1 km 500.00 m"}, // |m| > 1000
        {DistStrFormat::KmAndM, 500.0, "500.00 m"},       // |m| <= 1000
        {DistStrFormat::KmOrM, 5000.0, "5000.00 m"},      // |m| < 10000
        {DistStrFormat::KmOrM, 15000.0, "15.00 km"},      // |m| >= 10000
        {DistStrFormat::SimpleKm, 2500.0, "2.50 km"},
        {DistStrFormat::SimpleMeter, 2500.0, "2500.00 m"},
        {DistStrFormat::SimpleCentimeter, 2.5, "250.00 cm"},
        {DistStrFormat::SimpleMile, 1609.344, "1.00 mi"},
        {DistStrFormat::SimpleYard, 0.9144, "1.00 yd"},
        {DistStrFormat::SimpleFoot, 0.3048, "1.00 ft"},
        {DistStrFormat::SimpleInch, 0.0254, "1.00 in"},
        {DistStrFormat::SimpleNauticalMile, 1852.0, "1.00 nmi"},
        // Rounding tie, derived by hand (2 decimals -> odd multiple of 1/8; see the fixture
        // comment): 0.125 m formats to "0.13 m" (away from zero), not "0.12 m".
        {DistStrFormat::SimpleMeter, 0.125, "0.13 m"},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.expected);
        const ValueToStringConverter conv = makeConverter(r.format);
        EXPECT_EQ(conv.distConvert(r.metres), QString::fromLatin1(r.expected));
    }
}

TEST_F(ValueToStringTest, DistNegativePrefixesTheWholeString) {
    // §6.5.1 point 1: Abs-then-format, single '-' on the WHOLE result.
    const ValueToStringConverter conv = makeConverter(DistStrFormat::KmAndM);
    EXPECT_EQ(conv.distConvert(-1500.0), QStringLiteral("-1 km 500.00 m"));
    EXPECT_NE(conv.distConvert(-1500.0), QStringLiteral("-1 km -500.00 m"));
}

TEST_F(ValueToStringTest, DistConvertNaNGuard) {
    const ValueToStringConverter conv = makeConverter(DistStrFormat::KmAndM);
    EXPECT_EQ(conv.distConvert(kNaN), QStringLiteral("NaN"));
}

TEST_F(ValueToStringTest, DistPerPixelHasNoSpaceBeforeSlash) {
    const ValueToStringConverter conv = makeConverter(DistStrFormat::KmAndM);
    EXPECT_EQ(conv.distPerPixelConvert(500.0), QStringLiteral("500.00 m/pixel"));
}

// --- areaConvert (u_ValueToStringConverter.pas:128-212) --------------------------------------

TEST_F(ValueToStringTest, AreaConvertThresholdsAcrossEveryAreaStrFormat) {
    struct Row {
        AreaStrFormat format;
        double sqm;
        const char* expected;
    };
    const Row rows[] = {
        {AreaStrFormat::Auto, 999.0, "999.00 m2"},       // <= 1e6
        {AreaStrFormat::Auto, 2000000.0, "2.00 km2"},    // > 1e6
        {AreaStrFormat::SqM, 50.0, "50.00 m2"},          // <= 100, 2 decimals
        {AreaStrFormat::SqM, 250.0, "250 m2"},           // > 100, 0 decimals
        {AreaStrFormat::SqKm, 500000.0, "0.500000 km2"}, // <= 1e6, 6 decimals
        {AreaStrFormat::SqKm, 2000000.0, "2.00 km2"},    // > 1e6, 2 decimals
        {AreaStrFormat::Are, 5000.0, "50.00 a"},         // <= 10000, 2 decimals
        {AreaStrFormat::Are, 20000.0, "200 a"},          // > 10000, 0 decimals
        {AreaStrFormat::Ha, 50000.0, "5.0000 ha"},       // <= 1e6, 4 decimals
        {AreaStrFormat::Ha, 2000000.0, "200.00 ha"},     // > 1e6, 2 decimals
        {AreaStrFormat::SqFoot, 0.09290304, "1.00 ft2"},
        {AreaStrFormat::SqYard, 0.83612736, "1.00 yd2"},
        {AreaStrFormat::SqMile, 2589988.110336, "1.00 mi2"},
        {AreaStrFormat::SqNauticalMile, 3429904.0, "1.00 nmi2"},
        {AreaStrFormat::SqAcre, 4046.8564224, "1.00 acr"},
        // Rounding tie, derived by hand (4-decimal Ha branch -> odd multiple of 1/32; 312.5 sqm /
        // 10000 = 0.03125 exactly, an exact double since both operands are dyadic and the quotient
        // is itself dyadic): "0.0313 ha" (away from zero), not "0.0312 ha".
        {AreaStrFormat::Ha, 312.5, "0.0313 ha"},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.expected);
        const ValueToStringConverter conv = makeConverter(DistStrFormat::KmAndM, r.format);
        EXPECT_EQ(conv.areaConvert(r.sqm), QString::fromLatin1(r.expected));
    }
}

TEST_F(ValueToStringTest, AreaComparesSignedInput) {
    // §6.5.1 point 2: unlike distConvert, no Abs() -- a negative area takes the "small" branch of
    // every threshold (asfAuto's <= 1000000 branch here), not the "large" one a magnitude
    // comparison would pick for -2000000.
    const ValueToStringConverter conv = makeConverter(DistStrFormat::KmAndM, AreaStrFormat::Auto);
    EXPECT_EQ(conv.areaConvert(-2000000.0), QStringLiteral("-2000000.00 m2"));
}

TEST_F(ValueToStringTest, AreaConvertNaNGuard) {
    const ValueToStringConverter conv = makeConverter(DistStrFormat::KmAndM);
    EXPECT_EQ(conv.areaConvert(kNaN), QStringLiteral("NaN"));
}

// --- speedConvert (u_ValueToStringConverter.pas:318-357) -----------------------------------------

TEST_F(ValueToStringTest, SpeedUnitDerivedFromDistFormat) {
    // §6.5.1 point 3: the speed unit is derived from `dist`, never stored on its own.
    struct Row {
        DistStrFormat dist;
        double kmph;
        const char* expected;
    };
    const Row rows[] = {
        {DistStrFormat::KmAndM, 100.0, "100.0 km/h"},          // metric family -> ssfKmph
        {DistStrFormat::SimpleMile, 1.609344, "1.0 mi/h"},     // imperial family -> ssfMph
        {DistStrFormat::SimpleNauticalMile, 1.8520, "1.0 kn"}, // nautical -> ssfKnot
        // Rounding tie, derived by hand (1 decimal -> odd multiple of 1/4): 0.25 km/h -> "0.3
        // km/h" (away from zero).
        {DistStrFormat::KmAndM, 0.25, "0.3 km/h"},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.expected);
        const ValueToStringConverter conv = makeConverter(r.dist);
        EXPECT_EQ(conv.speedConvert(r.kmph), QString::fromLatin1(r.expected));
    }
}

TEST_F(ValueToStringTest, SpeedConvertNaNGuard) {
    const ValueToStringConverter conv = makeConverter(DistStrFormat::KmAndM);
    EXPECT_EQ(conv.speedConvert(kNaN), QStringLiteral("NaN"));
}

// --- altitudeConvert (u_ValueToStringConverter.pas:150-157) --------------------------------------

TEST_F(ValueToStringTest, AltitudeIsAlwaysMetres) {
    // Hard-wired to metres, 1 decimal, regardless of `dist` (unlike speedConvert).
    const ValueToStringConverter conv = makeConverter(DistStrFormat::SimpleMile);
    EXPECT_EQ(conv.altitudeConvert(100.5), QStringLiteral("100.5 m"));
    // Rounding tie, derived by hand (1 decimal -> odd multiple of 1/4): away from zero.
    EXPECT_EQ(conv.altitudeConvert(0.25), QStringLiteral("0.3 m"));
}

TEST_F(ValueToStringTest, AltitudeConvertNaNGuard) {
    const ValueToStringConverter conv = makeConverter(DistStrFormat::KmAndM);
    EXPECT_EQ(conv.altitudeConvert(kNaN), QStringLiteral("NaN"));
}

// --- locale sensitivity (the deliberate opposite of coord_to_string.cpp) -------------------------

TEST(ValueToStringLocale, LocaleAffectsGroupingAndSeparator) {
    const QLocale saved = QLocale();
    QLocale::setDefault(QLocale(QLocale::Russian));
    const ValueToStringConverter conv(ValueFormatSettings{DistStrFormat::SimpleMeter, AreaStrFormat::Auto});
    const QString result = conv.distConvert(1500.0);
    QLocale::setDefault(saved);
    // Grouped thousands (Russian's group separator, a narrow no-break space) and a comma decimal
    // -- the deliberate opposite of coord_to_string.cpp's hard-pinned '.' (u_CoordToStringConverter
    // .pas:126). Assert on the pieces rather than the exact separator glyph (encoding-fragile):
    // a comma decimal point, and definitely not the '.'-decimal / no-grouping C-locale form.
    EXPECT_TRUE(result.contains(QLatin1Char(',')));
    EXPECT_FALSE(result.contains(QLatin1Char('.')));
    EXPECT_NE(result, QStringLiteral("1500.00 m"));
}

// --- ValueFormatSettings::read/write (u_ValueToStringConverterConfig.pas:90-101) ------------------

namespace {
std::shared_ptr<cg::core::IConfigWriter> makeSection(std::shared_ptr<IniFile>& file) {
    file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    return root->orCreateSubItem(u"ValueFormats");
}
} // namespace

TEST(ValueFormatSettingsTest, DefaultsMatchLegacyConstructor) {
    // TValueToStringConverterConfig.Create (u_ValueToStringConverterConfig.pas:73-77).
    const ValueFormatSettings s;
    EXPECT_EQ(s.dist, DistStrFormat::KmAndM);
    EXPECT_EQ(s.area, AreaStrFormat::Auto);
}

TEST(ValueFormatSettingsTest, ReadOnAbsentSectionKeepsDefaults) {
    std::shared_ptr<IniFile> file;
    const auto section = makeSection(file);
    EXPECT_EQ(ValueFormatSettings::read(*section), ValueFormatSettings{});
}

TEST(ValueFormatSettingsTest, ReadWriteRoundTrip) {
    std::shared_ptr<IniFile> file;
    const auto section = makeSection(file);
    section->writeInt(u"DistFormat", static_cast<int>(DistStrFormat::SimpleNauticalMile));
    section->writeInt(u"AreaShowFormat", static_cast<int>(AreaStrFormat::SqAcre));

    const ValueFormatSettings s = ValueFormatSettings::read(*section);
    EXPECT_EQ(s.dist, DistStrFormat::SimpleNauticalMile);
    EXPECT_EQ(s.area, AreaStrFormat::SqAcre);

    auto outFile = std::make_shared<IniFile>();
    const auto outRoot = ConfigProviderIni::wrap(outFile);
    const auto outSection = outRoot->orCreateSubItem(u"ValueFormats");
    s.write(*outSection);

    EXPECT_EQ(outSection->readInt(u"DistFormat", -1), static_cast<int>(DistStrFormat::SimpleNauticalMile));
    EXPECT_EQ(outSection->readInt(u"AreaShowFormat", -1), static_cast<int>(AreaStrFormat::SqAcre));

    EXPECT_EQ(ValueFormatSettings::read(*outSection), s);
}

TEST(ValueFormatSettingsTest, ReadKeepsRawOutOfRangeOrdinalAndRoundTripsLikeLegacy) {
    // Legacy casts these with an *unchecked* `TFoo(Integer)` cast
    // (u_ValueToStringConverterConfig.pas:88-89) and writes the stored ordinal straight back
    // (`DoWriteConfig`) -- strict 1:1 (Tech Lead ruling, T-0019A2 rework): the port keeps the raw
    // ordinal rather than validating and falling back to the default.
    std::shared_ptr<IniFile> file;
    const auto section = makeSection(file);
    section->writeInt(u"DistFormat", 99);
    section->writeInt(u"AreaShowFormat", -3);

    const ValueFormatSettings s = ValueFormatSettings::read(*section);
    EXPECT_EQ(s.dist, static_cast<DistStrFormat>(99));
    EXPECT_EQ(s.area, static_cast<AreaStrFormat>(-3));

    // distConvert/areaConvert fall through every case unmatched -- the existing unreachable
    // fallbacks (empty `result` in distConvert, `QString()` in areaConvert) already reproduce
    // legacy's release-build behaviour (Assert(False) is a no-op, DCC_AssertionsAtRuntime=False):
    // an out-of-range DistFormat yields "" for a non-negative value, "-" for a negative one
    // (distConvert's abs-then-prefix still runs), and an out-of-range AreaShowFormat always
    // yields "".
    const ValueToStringConverter converter(s);
    EXPECT_EQ(converter.distConvert(1500.0), QString());
    EXPECT_EQ(converter.distConvert(-1500.0), QStringLiteral("-"));
    EXPECT_EQ(converter.areaConvert(1500.0), QString());
    EXPECT_EQ(converter.areaConvert(-1500.0), QString());

    // DoWriteConfig writes Ord(FDistFormat)/Ord(FAreaShowFormat) straight back -- round-trips the
    // stored (possibly out-of-range) ordinal unchanged.
    auto outFile = std::make_shared<IniFile>();
    const auto outRoot = ConfigProviderIni::wrap(outFile);
    const auto outSection = outRoot->orCreateSubItem(u"ValueFormats");
    s.write(*outSection);

    EXPECT_EQ(ValueFormatSettings::read(*outSection), s);
}
