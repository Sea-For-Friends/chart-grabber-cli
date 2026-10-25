// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/region/zoom_array_func (Src/RegionProcess/u_ZoomArrayFunc.pas). FEAT-REG-004 (text form).
#include "cg/region/zoom_array_func.h"

#include <gtest/gtest.h>

#include <vector>

using cg::region::isSortedZoomArray;
using cg::region::isZoomInZoomArray;
using cg::region::sortZoomArray;
using cg::region::zoomArrayFromStr;
using cg::region::zoomArrayToStr;

using Bytes = std::vector<quint8>;

// ZoomArrayToStr :181-225. Input 0-based, text 1-based.
TEST(ZoomArrayFunc, ToStrRows) {
    struct Row {
        Bytes in;
        const char* out;
    };
    const Row rows[] = {
        {{0}, "1"},
        {{0, 1}, "1,2"}, // exactly two: "s,e" (:155-179)
        {{0, 1, 2, 4}, "1-3,5"},
        {{2, 2}, "3,3"},              // equal values: diff 0 is not one -> two single parts
        {{4, 0, 2, 1}, "1-3,5"},      // unsorted input is sorted first (:200-202)
        {{0, 1, 5, 6, 7}, "1,2,6-8"}, // a pair inside a longer list
        {{255}, "256"},               // single element: Integer arithmetic (:191-192)
        {{255, 0}, "0,1"},            // multi element: Byte assignment wraps 255+1 to 0 (:196-199)
    };
    for (const Row& r : rows) {
        EXPECT_EQ(zoomArrayToStr(r.in), QString::fromLatin1(r.out)) << r.out;
    }
}

// ZoomArrayFromStr :101-148. Output 1-based, as written, sorted.
TEST(ZoomArrayFunc, FromStrRows) {
    struct Row {
        const char16_t* in;
        Bytes out;
    };
    const Row rows[] = {
        {u"1-5,7", {1, 2, 3, 4, 5, 7}},
        {u"7, 1-3", {1, 2, 3, 7}},    // blank separates; unsorted -> sorted
        {u"\"1-3\",5", {1, 2, 3, 5}}, // quoted item
        {u"5-1,2", {2}},              // min > max dropped
        {u"abc,4", {4}},
        {u"-3,6", {6}},    // empty left part does not parse
        {u"1-2-3,8", {8}}, // right part "2-3" does not parse
        {u"3-3", {3}},
        {u"3--1,9", {9}}, // right part "-1": min 3 > max -1
        {u"5--3,9", {9}},
        {u"300", {44}},        // Byte narrowing, range checks off
        {u"9,3,9", {3, 9, 9}}, // duplicates are kept
    };
    for (const Row& r : rows) {
        Bytes out{200, 201}; // pre-filled: must be cleared
        EXPECT_TRUE(zoomArrayFromStr(QString::fromUtf16(r.in), out))
            << QString::fromUtf16(r.in).toStdString();
        EXPECT_EQ(out, r.out) << QString::fromUtf16(r.in).toStdString();
    }
}

TEST(ZoomArrayFunc, FromStrNothingParsed) {
    for (const QString& s : {QString(), QStringLiteral("abc"), QStringLiteral("5-1")}) {
        Bytes out{7, 8};
        EXPECT_FALSE(zoomArrayFromStr(s, out)) << s.toStdString();
        EXPECT_TRUE(out.empty()) << s.toStdString();
    }
}

TEST(ZoomArrayFunc, RoundTrip) {
    const Bytes inputs[] = {{0, 1, 2, 4}, {3, 5, 6, 7, 10, 11}};
    const Bytes expected[] = {{1, 2, 3, 5}, {4, 6, 7, 8, 11, 12}};
    for (size_t i = 0; i < 2; ++i) {
        Bytes out;
        ASSERT_TRUE(zoomArrayFromStr(zoomArrayToStr(inputs[i]), out));
        EXPECT_EQ(out, expected[i]);
    }
}

TEST(ZoomArrayFunc, IsSorted) {
    EXPECT_TRUE(isSortedZoomArray({}));
    EXPECT_TRUE(isSortedZoomArray({5}));
    EXPECT_TRUE(isSortedZoomArray({1, 1, 2})); // equal neighbours are sorted
    EXPECT_FALSE(isSortedZoomArray({1, 3, 2}));
    EXPECT_FALSE(isSortedZoomArray({2, 1}));
}

TEST(ZoomArrayFunc, Sort) {
    Bytes a{9, 2, 7, 2, 0, 9};
    sortZoomArray(a);
    EXPECT_EQ(a, (Bytes{0, 2, 2, 7, 9, 9}));
}

TEST(ZoomArrayFunc, IsZoomIn) {
    const Bytes a{4, 8, 15};
    EXPECT_TRUE(isZoomInZoomArray(15, a));
    EXPECT_TRUE(isZoomInZoomArray(4, a));
    EXPECT_FALSE(isZoomInZoomArray(9, a));
    EXPECT_FALSE(isZoomInZoomArray(4, {}));
}
