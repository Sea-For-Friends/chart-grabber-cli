// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-003 (Format/FormatEx/RoundEx surface, T-0014C1). docs/specs/script.md:611-627,
// :1088-1131 (F20/F21/F25, the "Format subset to implement" table).
#include "cg/core/delphi_format.h"

#include <gtest/gtest.h>

#include <limits>

using namespace cg::core;

namespace {

struct DelphiFormatCase {
    const char* name;
    QByteArray fmt;
    std::vector<FormatArg> args;
    bool expectError = false;
    const char* expected = "";
};

class DelphiFormat_Cases : public ::testing::TestWithParam<DelphiFormatCase> {};

TEST_P(DelphiFormat_Cases, MatchesTheSpecTable) {
    const auto& c = GetParam();
    auto r = delphiFormat(c.fmt, c.args);
    if (c.expectError) {
        ASSERT_FALSE(r.has_value());
    } else {
        ASSERT_TRUE(r.has_value()) << r.error().what().toStdString();
        EXPECT_EQ(*r, QByteArray(c.expected));
    }
}

INSTANTIATE_TEST_SUITE_P(
    Specifiers, DelphiFormat_Cases,
    ::testing::Values(
        DelphiFormatCase{"Decimal", "%d", {qint64{42}}, false, "42"},
        DelphiFormatCase{"Unsigned", "%u", {qint64{-1}}, false, "4294967295"},
        DelphiFormatCase{"Hex", "%x", {qint64{255}}, false, "FF"},
        DelphiFormatCase{"Str", "%s", {QByteArray(QByteArrayLiteral("hi"))}, false, "hi"},
        // T-0014C1 rework (finding 3): %e's precision is the *total* significant digit count,
        // default 15 - Delphi docs' own worked example, `Format('%e',[12345.6789])` =
        // `1.23456789000000E+004` (15 sig digits: "123456789000000", exponent sign + >=3 digits).
        // The old expectation ("1.23E+04") encoded 2-fractional-digit-default/2-digit-exponent,
        // which was the bug.
        DelphiFormatCase{"Scientific", "%e", {12345.6789L}, false, "1.23456789000000E+004"},
        // finding 3: an explicit precision on %e is *total* significant digits, and ties round
        // half-away-from-zero via the shared digit-string helper (not `ad/pow`, which loses ties).
        // 12345 is an exact double; rounding it to 4 significant digits cuts exactly at "12345"'s
        // trailing '5' - a genuine tie - so half-away rounds "1234" up to "1235".
        DelphiFormatCase{
            "ScientificPrecisionIsTotalSigDigitsAndTiesHalfAway", "%.4e", {12345.0L}, false, "1.235E+004"},
        DelphiFormatCase{"Fixed", "%f", {3.14159L}, false, "3.14"},
        DelphiFormatCase{"General", "%g", {123.456L}, false, "123.456"},
        DelphiFormatCase{"Thousands", "%n", {1234567.891L}, false, "1,234,567.89"},
        DelphiFormatCase{"Currency", "%m", {1234567.891L}, false, "1,234,567.89"},
        DelphiFormatCase{"ExplicitIndexIsZeroBased", "%1:d %0:d", {qint64{10}, qint64{20}}, false, "20 10"},
        DelphiFormatCase{"LeftAlignWidth", "%-5d|", {qint64{3}}, false, "3    |"},
        DelphiFormatCase{"Precision", "%.3f", {1.0L / 3.0L}, false, "0.333"},
        DelphiFormatCase{"StarWidthThenImplicitValue", "%*d", {qint64{5}, qint64{3}}, false, "    3"},
        DelphiFormatCase{"LiteralPercent", "100%%", {}, false, "100%"},
        // finding 7: one tie row per suite is enough (the negative-sign variant added nothing a
        // sign-flip doesn't already cover).
        DelphiFormatCase{"RoundingTieIsHalfAwayFromZero", "%.2f", {1.125L}, false, "1.13"},
        DelphiFormatCase{"UnsupportedSpecifierIsARuntimeError", "%p", {qint64{1}}, true, ""},
        // finding 2: an explicit `%<n>:` index moves the implicit argument counter to index+1,
        // it does not just borrow an argument - documented Delphi:
        // `Format('%d %d %d %0:d %d',[1,2,3,4]) = '1 2 3 1 2'`.
        DelphiFormatCase{"ExplicitIndexMovesTheImplicitCounter",
                         "%d %d %d %0:d %d",
                         {qint64{1}, qint64{2}, qint64{3}, qint64{4}},
                         false,
                         "1 2 3 1 2"},
        // finding 5: %g ties round half-away-from-zero, not glibc's round-half-to-even. 0.125 at
        // 2 significant digits is an exact tie ("12|5" cut); glibc's `%.1Le` gives "1.2e-01"
        // (half-to-even), half-away gives "0.13".
        DelphiFormatCase{"GeneralTieIsHalfAwayFromZero", "%.2g", {0.125L}, false, "0.13"},
        // finding 4: NaN/Inf on the float specifiers print the string, they do not raise.
        DelphiFormatCase{"FloatSpecifierPrintsInfAsString",
                         "%f",
                         {std::numeric_limits<long double>::infinity()},
                         false,
                         "INF"},
        // finding 6: Delphi's Format is argument-type-strict - %f wants a real, not an integer
        // (the port used to widen Int through Value::toReal()).
        DelphiFormatCase{"FloatSpecifierRejectsIntegerArgument", "%f", {qint64{5}}, true, ""}),
    [](const ::testing::TestParamInfo<DelphiFormatCase>& paramInfo) { return paramInfo.param.name; });

TEST(DelphiFormat, FormatExUsesTheSuppliedDecimalSeparator) {
    auto r = delphiFormat("%.2f", std::array<FormatArg, 1>{3.5L}, ',');
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, QByteArrayLiteral("3,50"));
}

TEST(DelphiFormat, FormatExThousandsSeparatorIsTheNulByte) {
    // F21: a fresh, default-initialised TFormatSettings has ThousandSeparator == #0 - reproduced,
    // not fixed up (spec docs/specs/script.md:1094-1095).
    auto r = delphiFormat("%n", std::array<FormatArg, 1>{1234.0L}, '.', '\0');
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, QByteArray("1"
                             "\0"
                             "234.00",
                             8));
}

struct RoundExCase {
    const char* name;
    double v;
    int precision;
    const char* expected;
};

class RoundEx_Cases : public ::testing::TestWithParam<RoundExCase> {};

TEST_P(RoundEx_Cases, MatchesF25) {
    EXPECT_EQ(roundEx(GetParam().v, GetParam().precision), QByteArray(GetParam().expected));
}

INSTANTIATE_TEST_SUITE_P(
    Goldens, RoundEx_Cases,
    // finding 7: one Inf row and one tie row per suite is enough.
    ::testing::Values(
        RoundExCase{"NaNIsDash", std::numeric_limits<double>::quiet_NaN(), 3, "-"},
        RoundExCase{"PosInf", std::numeric_limits<double>::infinity(), 3, "INF"},
        RoundExCase{"BasicZeroPadded", 1.0, 3, "1.000"},
        RoundExCase{"TieIsHalfAwayFromZero", 1.125, 2, "1.13"},
        RoundExCase{"PrecisionClampedHighTo18", 1.0, 25, "1.000000000000000000"},
        RoundExCase{"PrecisionClampedLowTo0", 1.0, -5, "1"},
        // finding 1 (major): FloatToStrF first rounds to 15 *significant* digits, then lays out
        // exactly `Precision` decimals, zero-filling past the 15th - it does not print the exact
        // binary expansion to `Precision` decimals. This is the review's own corpus-shape vector
        // (Mercator metres, `RoundEx(GetLMetr,10)`, 13 corpus uses):
        // exact double value: 2226389.8158654714934527873992919921875
        // 15 sig digits (7 int + 8 frac), half-away-from-zero: 2226389.81586547
        // Digits->10 requested->8 available: zero-fill 2 more: 2226389.8158654700
        RoundExCase{"FifteenSignificantDigitCapMetreShape", 2226389.8158654715, 10, "2226389.8158654700"},
        // Precision 13, typical value: exact double 123.4567890123456805895330035127699375...
        // 15 sig digits (3 int + 12 frac), half-away: 123.456789012346
        // Digits->13 requested->12 available: zero-fill 1 more: 123.4567890123460
        RoundExCase{"FifteenSignificantDigitCapPrecision13", 123.45678901234568, 13, "123.4567890123460"},
        // Precision 15, typical longitude value: exact double 37.6173456789123434873545193113...
        // 15 sig digits (2 int + 13 frac), half-away: 37.6173456789123
        // Digits->15 requested->13 available: zero-fill 2 more: 37.617345678912300
        RoundExCase{"FifteenSignificantDigitCapPrecision15Longitude", 37.617345678912343, 15,
                    "37.617345678912300"}),
    [](const ::testing::TestParamInfo<RoundExCase>& paramInfo) { return paramInfo.param.name; });

} // namespace
