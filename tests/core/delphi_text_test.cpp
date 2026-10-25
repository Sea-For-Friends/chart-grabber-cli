// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-019, FEAT-STO-020, FEAT-NET-002 (Delphi text semantics; moved from
// sqlite_file_detect_test, T-0045)
#include "cg/core/delphi_text.h"

#include <QString>

#include <gtest/gtest.h>

#include <cstdint>

using namespace cg::core;

TEST(DelphiText, StrToIntAndFloatVectors) {
    struct IntCase {
        const char16_t* text;
        bool ok;
        int value;
    };
    const IntCase ints[] = {
        {u"42", true, 42},         {u"  42", true, 42},
        {u"42 ", false, 0},        {u"-7", true, -7},
        {u"+7", true, 7},          {u"$1F", true, 31},
        {u"0x1f", true, 31},       {u"X1F", true, 31},
        {u"-$10", true, -16},      {u"$FFFFFFFF", true, -1},
        {u"$100000000", false, 0}, {u"2147483647", true, 2147483647},
        {u"2147483648", false, 0}, {u"-2147483648", true, INT32_MIN},
        {u"", false, 0},           {u"-", false, 0},
        {u"$", false, 0},          {u"4 2", false, 0},
        {u"3857\t", false, 0},
    };
    for (const IntCase& c : ints) {
        int value = -12345;
        EXPECT_EQ(delphiTryStrToInt(QString::fromUtf16(c.text), value), c.ok)
            << QString::fromUtf16(c.text).toStdString();
        if (c.ok) {
            EXPECT_EQ(value, c.value) << QString::fromUtf16(c.text).toStdString();
        }
    }
    struct FloatCase {
        const char16_t* text;
        bool ok;
        double value;
    };
    const FloatCase floats[] = {{u"1.5", true, 1.5},    {u" -.5 ", true, -0.5}, {u"2.", true, 2.0},
                                {u"1e3", true, 1000.0}, {u"1,5", false, 0},     {u".", false, 0},
                                {u"1e", false, 0},      {u"0x10", false, 0},    {u"", false, 0}};
    for (const FloatCase& c : floats) {
        double value = 0;
        EXPECT_EQ(delphiTryStrToFloat(QString::fromUtf16(c.text), value), c.ok)
            << QString::fromUtf16(c.text).toStdString();
        if (c.ok) {
            EXPECT_DOUBLE_EQ(value, c.value);
        }
    }
    EXPECT_EQ(delphiLowerCase(QStringLiteral("ABCxyzÄ")), QStringLiteral("abcxyzÄ"));
    EXPECT_EQ(delphiUpperCase(QStringLiteral("ABCxyzä1")), QStringLiteral("ABCXYZä1"));
    EXPECT_EQ(delphiUpperCase(QString()), QString());
    // SameText folds ASCII only; Trim removes every char <= ' ' (control chars too) but not NBSP.
    EXPECT_TRUE(delphiSameText(QStringLiteral("REL-1"), QStringLiteral("rel-1")));
    EXPECT_FALSE(delphiSameText(QStringLiteral("Ä"), QStringLiteral("ä")));
    EXPECT_EQ(delphiTrim(QStringLiteral("\x01 \t12 \n")), QStringLiteral("12"));
    EXPECT_EQ(delphiTrim(QStringLiteral("\u00A012")), QStringLiteral("\u00A012"));
}

// delphiTryStrToInt64: the 64-bit hex / blank rules are unverified (RTL not in the legacy tree; by analogy
// with the Integer rule).
TEST(DelphiText, StrToInt64Vectors) {
    struct Case {
        const char16_t* text;
        bool ok;
        qint64 value;
    };
    const Case cases[] = {
        {u"42", true, 42},
        {u"  42", true, 42}, // blanks: unverified, by analogy
        {u"42 ", false, 0},
        {u"-7", true, -7},
        {u"+7", true, 7},
        {u"9223372036854775807", true, INT64_MAX},
        {u"9223372036854775808", false, 0},
        {u"-9223372036854775808", true, INT64_MIN},
        {u"-9223372036854775809", false, 0},
        {u"18446744073709551616", false, 0},
        {u"$1F", true, 31}, // hex: unverified, by analogy
        {u"0x1f", true, 31},
        {u"X1F", true, 31},
        {u"-$10", true, -16},
        {u"$FFFFFFFFFFFFFFFF", true, -1},  // 64-bit wrap: unverified, by analogy
        {u"$10000000000000000", false, 0}, // hex overflow
        {u"", false, 0},
        {u"-", false, 0},
        {u"$", false, 0},
        {u"4 2", false, 0},
        {u"2x", false, 0},
    };
    for (const Case& c : cases) {
        qint64 value = -12345;
        EXPECT_EQ(delphiTryStrToInt64(QString::fromUtf16(c.text), value), c.ok)
            << QString::fromUtf16(c.text).toStdString();
        if (c.ok) {
            EXPECT_EQ(value, c.value) << QString::fromUtf16(c.text).toStdString();
        }
    }
}
