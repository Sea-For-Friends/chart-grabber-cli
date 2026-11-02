// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors.

// Tests for src/cg/core/platform/parse_double (T-0396): the contract of std::from_chars for double, on every
// platform.
#include "cg/core/platform/parse_double.h"

#include <QLocale>

#include <gtest/gtest.h>

#include <clocale>
#include <cstring>
#include <string>

using cg::core::platform::parseDouble;
using cg::core::platform::parseDoubleStrtod;

namespace {
bool parse(const std::string& text, double& value) {
    return parseDouble(text.data(), text.data() + text.size(), value);
}
} // namespace

TEST(ParseDouble, NormalLiteralsGiveTheExactValue) {
    double v = -1;
    EXPECT_TRUE(parse("1.5", v));
    EXPECT_EQ(v, 1.5);
    EXPECT_TRUE(parse("0.1", v));
    EXPECT_EQ(v, 0.1);
    EXPECT_TRUE(parse("123456.789", v));
    EXPECT_EQ(v, 123456.789);
}

TEST(ParseDouble, ExponentForms) {
    double v = 0;
    EXPECT_TRUE(parse("1e3", v));
    EXPECT_EQ(v, 1000.0);
    EXPECT_TRUE(parse("2.5E-2", v));
    EXPECT_EQ(v, 0.025);
    EXPECT_TRUE(parse("1.0e+2", v));
    EXPECT_EQ(v, 100.0);
    EXPECT_TRUE(parse("5e-324", v)); // smallest denormal: representable, not an error
    EXPECT_GT(v, 0.0);
}

TEST(ParseDouble, OverflowAndUnderflowLeaveTheValueUnchanged) {
    double v = 0.0;
    EXPECT_FALSE(parse("1e999", v));
    EXPECT_EQ(v, 0.0);
    v = 7.0;
    EXPECT_FALSE(parse("1e999", v));
    EXPECT_EQ(v, 7.0);
    EXPECT_FALSE(parse("1e-999", v));
    EXPECT_EQ(v, 7.0);
}

TEST(ParseDouble, LongMantissaIsCorrectlyRounded) {
    double v = 0;
    EXPECT_TRUE(parse("0.30000000000000004440892098500626161694526672363281250000000000001", v));
    EXPECT_EQ(v, 0.30000000000000004);
    EXPECT_TRUE(parse("3.14159265358979323846264338327950288419716939937510582097494459", v));
    EXPECT_EQ(v, 3.141592653589793);
    EXPECT_TRUE(parse("12345678901234567890123456789012345678901234567890e-30", v));
    EXPECT_EQ(v, 1.2345678901234568e19);
}

TEST(ParseDouble, OnlyTheLeadingDecimalNumberCounts) {
    double v = 0;
    EXPECT_TRUE(parse("1e", v));
    EXPECT_EQ(v, 1.0);
    EXPECT_TRUE(parse("0x10", v)); // no hex
    EXPECT_EQ(v, 0.0);
    EXPECT_TRUE(parse("-2.5", v));
    EXPECT_EQ(v, -2.5);
    EXPECT_TRUE(parse(".5", v));
    EXPECT_EQ(v, 0.5);
}

TEST(ParseDouble, EmptyOrGarbageIsRejected) {
    double v = 3.0;
    EXPECT_FALSE(parse("", v));
    EXPECT_FALSE(parse("abc", v));
    EXPECT_FALSE(parse("+1", v));
    EXPECT_FALSE(parse(" 1", v));
    EXPECT_FALSE(parse("-", v));
    EXPECT_EQ(v, 3.0);
}

TEST(ParseDouble, CommaDecimalLocaleChangesNothing) {
    const std::string old = std::setlocale(LC_NUMERIC, nullptr);
    const bool haveComma = std::setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr ||
                           std::setlocale(LC_NUMERIC, "de_DE") != nullptr;
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    double v = 0;
    EXPECT_TRUE(parse("1.5", v));
    EXPECT_EQ(v, 1.5);
    EXPECT_TRUE(parse("2.25e1", v));
    EXPECT_EQ(v, 22.5);
    EXPECT_TRUE(parse("1,5", v)); // a comma is never a decimal separator: only the leading "1" is a number
    EXPECT_EQ(v, 1.0);
    EXPECT_FALSE(parse(",5", v));
    std::setlocale(LC_NUMERIC, old.c_str());
    QLocale::setDefault(QLocale::c());
    if (!haveComma) {
        GTEST_LOG_(INFO) << "no comma-decimal C locale installed: only the QLocale default was switched";
    }
}

// The Apple implementation (strtod_l behind a hand-written scanner) against parseDouble, which is
// std::from_chars where the library has it (Linux in the gate; on Apple both are the same code), on the
// grammar the lexer passes: same verdict, same bits.
TEST(ParseDouble, StrtodImplementationAgreesWithFromCharsOnTheLexerGrammar) {
    const char* const cases[] = {"0",
                                 "1",
                                 "1.",
                                 ".5",
                                 "-.5",
                                 "007",
                                 "1.5",
                                 "0.1",
                                 "123456.789",
                                 "1e3",
                                 "1E3",
                                 "1e+3",
                                 "1e-3",
                                 "1.e2",
                                 ".5e1",
                                 "1e",
                                 "1e+",
                                 "1e-",
                                 "1.5e",
                                 "1ex",
                                 "5e-324",
                                 "4.9e-324",
                                 "2.5e-324",
                                 "2e-324",
                                 "1e-325",
                                 "1e-999",
                                 "1e308",
                                 "1.7976931348623157e308",
                                 "1.8e308",
                                 "1e309",
                                 "1e999",
                                 "2.2250738585072014e-308",
                                 "2.225073858507201e-308",
                                 "0e0",
                                 "0.0",
                                 "-0",
                                 "-0.0",
                                 "-1e999",
                                 "9007199254740993",
                                 "0.30000000000000004440892098500626161694526672363281250000000000001",
                                 "12345678901234567890123456789012345678901234567890e-30",
                                 "1,5",
                                 ",5",
                                 ".",
                                 "-",
                                 "e5",
                                 "",
                                 "abc",
                                 "1.2.3",
                                 "1e5e5",
                                 "00.5"};
    for (const char* text : cases) {
        const std::string t = text;
        double viaChars = 42.0;
        double viaStrtod = 42.0;
        const bool okChars =
            parseDouble(t.data(), t.data() + t.size(), viaChars); // std::from_chars where the library has it
        const bool okStrtod = parseDoubleStrtod(t.data(), t.data() + t.size(), viaStrtod);
        EXPECT_EQ(okStrtod, okChars) << "'" << t << "'";
        EXPECT_EQ(std::memcmp(&viaStrtod, &viaChars, sizeof viaChars), 0)
            << "'" << t << "': " << viaStrtod << " vs " << viaChars;
    }
}
