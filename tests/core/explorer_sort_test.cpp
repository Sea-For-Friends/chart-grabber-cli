// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-001 (compareStringOrdinal, maptype spec 2.4.1, 7.3; L-118)
#include "cg/core/explorer_sort.h"

#include <QString>

#include <gtest/gtest.h>

using cg::core::compareStringOrdinal;
using namespace Qt::StringLiterals;

namespace {

struct Row {
    QString a;
    QString b;
    int sign; // expected sign of compareStringOrdinal(a, b)
};

int sgn(int v) {
    return (v > 0) - (v < 0);
}

} // namespace

TEST(CompareStringOrdinal, Vectors) {
    const Row rows[] = {
        {u""_s, u""_s, 0},
        {u""_s, u"a"_s, -1},
        {u"a"_s, u""_s, 1},
        {u"abc"_s, u"abc"_s, 0},
        {u"abc"_s, u"abcd"_s, -1}, // end of string < anything
        {u"ABC"_s, u"abc"_s, 0},   // case-insensitive
        {u"Map"_s, u"map2"_s, -1},
        {u"Map (3)"_s, u"Map (103)"_s, -1}, // numeric runs
        {u"Map 10"_s, u"Map 9"_s, 1},
        {u"1"_s, u"a"_s, -1}, // a digit sorts before a non-digit
        {u"a"_s, u"1"_s, 1},
        {u"a1"_s, u"ab"_s, -1},
        {u"0"_s, u"00"_s, -1}, // equal zeros: shorter run first
        {u"000"_s, u"00"_s, 1},
        {u"01"_s, u"1"_s, 0},      // equal non-zero numbers: no tie-break
        {u"a01b"_s, u"a1c"_s, -1}, // ...so the walk continues after the run
        {u"\\"_s, u"/"_s, 1},      // UTF-16 code unit order: 0x5C > 0x2F
        {u"a\\b"_s, u"a;b"_s, 1},
        {u"А"_s, u"а"_s, 0},          // Cyrillic case folding
        {u"4294967296"_s, u"0"_s, 1}, // L-118: wraps to 0, both zero -> longer run is greater
        {u"4294967296"_s, u"00000000000"_s, -1},
        {u"4294967296"_s, u"1"_s, -1}, // wraps to 0 < 1
        {u"2147483648"_s, u"1"_s, -1}, // wraps to INT_MIN (signed compare)
        {u"2147483647"_s, u"2147483648"_s, 1},
    };
    for (const Row& row : rows) {
        EXPECT_EQ(sgn(compareStringOrdinal(row.a, row.b)), row.sign)
            << row.a.toStdString() << " vs " << row.b.toStdString();
    }
}

TEST(CompareStringOrdinal, EmbeddedNulEndsTheString) {
    EXPECT_EQ(compareStringOrdinal(QStringView(u"ab\0cd", 5), u"ab"), 0);
}
