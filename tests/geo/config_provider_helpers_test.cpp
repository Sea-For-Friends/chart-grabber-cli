// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-024, FEAT-REG-006 (both contribute, neither closes; docs/parity/held-open.md).
// Tests ReadPolygon / WritePolygon of Src/ConfigData/u_ConfigProviderHelpers.pas:239-419 (T-0158).
// No real selection file exists on the box (docs/specs/region-download.md §8.3, OQ-7): the reader tests
// use INI text written by hand from the key rules of the Pascal (:271-297 key chain, :302-323 loop),
// NOT produced by writePolygon. Only the round-trip and defect tests go through writePolygon.

#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/geo/config_provider_helpers.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using cg::core::ConfigProviderIni;
using cg::core::IConfigProviderPtr;
using cg::core::IConfigWriterPtr;
using cg::core::IniFile;
using cg::geo::GeometryLonLatFactory;
using cg::geo::IGeometryLonLatMultiPolygon;
using cg::geo::IGeometryLonLatPolygon;
using cg::geo::IGeometryLonLatSinglePolygon;
using cg::geo::LonLat;
using cg::geo::readPolygon;
using cg::geo::writePolygon;

namespace {

using Pts = std::vector<LonLat>;
using Single = std::vector<Pts>;   // outer, then the holes
using Shape = std::vector<Single>; // one entry per single polygon (two or more: multi-polygon)

struct Pt {
    const char* x;
    const char* y;
};

// "<lonPrefix><i>=x" and "<latPrefix><i>=y" lines for consecutive indices from `first`.
QByteArray keys(const char* lonPrefix, const char* latPrefix, int first, const std::vector<Pt>& pts) {
    QByteArray out;
    int i = first;
    for (const Pt& p : pts) {
        out += QByteArray(lonPrefix) + QByteArray::number(i) + '=' + p.x + '\n';
        out += QByteArray(latPrefix) + QByteArray::number(i) + '=' + p.y + '\n';
        ++i;
    }
    return out;
}

IConfigProviderPtr sectionFromText(const QByteArray& body) {
    auto parsed = IniFile::fromBytes("[S]\n" + body);
    EXPECT_TRUE(parsed.has_value());
    auto root = ConfigProviderIni::wrap(std::make_shared<IniFile>(std::move(*parsed)));
    return root->subItem(u"S");
}

Shape shapeOf(const std::shared_ptr<const IGeometryLonLatPolygon>& polygon) {
    auto single = [](const IGeometryLonLatSinglePolygon& s) {
        Single out;
        const auto o = s.outerBorder()->points();
        out.emplace_back(o.begin(), o.end());
        for (std::size_t h = 0; h < s.holeCount(); ++h) {
            const auto p = s.holeBorder(h)->points();
            out.emplace_back(p.begin(), p.end());
        }
        return out;
    };
    Shape shape;
    if (const auto s = std::dynamic_pointer_cast<const IGeometryLonLatSinglePolygon>(polygon)) {
        shape.push_back(single(*s));
    } else if (const auto m = std::dynamic_pointer_cast<const IGeometryLonLatMultiPolygon>(polygon)) {
        for (std::size_t i = 0; i < m->count(); ++i) {
            shape.push_back(single(*m->item(i)));
        }
    }
    return shape;
}

Shape readShape(const QByteArray& body) {
    const GeometryLonLatFactory factory;
    return shapeOf(readPolygon(*sectionFromText(body), factory));
}

cg::geo::LonLatPointsPtr ptr(Pts p) {
    return std::make_shared<const Pts>(std::move(p));
}

// outer + holes built through the factory's builder.
std::shared_ptr<const IGeometryLonLatPolygon> makeSingle(const Pts& outer, const std::vector<Pts>& holes) {
    GeometryLonLatFactory factory;
    auto b = factory.makePolygonBuilder();
    b.addOuter(ptr(outer));
    for (const Pts& h : holes) {
        b.addHole(ptr(h));
    }
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatSinglePolygon>(b.makeStaticAndClear());
    EXPECT_TRUE(result != nullptr);
    return result;
}

std::shared_ptr<const IGeometryLonLatPolygon> makeMulti(const Pts& a, const Pts& b2) {
    GeometryLonLatFactory factory;
    auto b = factory.makePolygonBuilder();
    b.addOuter(ptr(a));
    b.addOuter(ptr(b2));
    auto result = std::dynamic_pointer_cast<const IGeometryLonLatMultiPolygon>(b.makeStaticAndClear());
    EXPECT_TRUE(result != nullptr);
    return result;
}

QStringList sortedKeys(const IConfigProviderPtr& s) {
    QStringList k = s->valueNames();
    k.sort();
    return k;
}

} // namespace

// Proof 1: each spelling, each starting index, is found (distinct coordinates per case).
TEST(ConfigProviderHelpers, ReaderFindsEverySpellingAtIndexZeroAndOne) {
    struct Case {
        const char* lon;
        const char* lat;
        int first;
        Pt a, b, c;
        Pts expected;
    };
    const Case cases[] = {
        {"PointLon_", "PointLat_", 0, {"1", "2"}, {"3", "4"}, {"5", "6"}, {{1, 2}, {3, 4}, {5, 6}}},
        {"PointLon_", "PointLat_", 1, {"7", "8"}, {"9", "10"}, {"11", "12"}, {{7, 8}, {9, 10}, {11, 12}}},
        {"LLPointX_",
         "LLPointY_",
         0,
         {"13", "14"},
         {"15", "16"},
         {"17", "18"},
         {{13, 14}, {15, 16}, {17, 18}}},
        {"LLPointX_",
         "LLPointY_",
         1,
         {"19", "20"},
         {"21", "22"},
         {"23", "24"},
         {{19, 20}, {21, 22}, {23, 24}}},
        {"PointX_", "PointY_", 0, {"25", "26"}, {"27", "28"}, {"29", "30"}, {{25, 26}, {27, 28}, {29, 30}}},
        {"PointX_", "PointY_", 1, {"31", "32"}, {"33", "34"}, {"35", "36"}, {{31, 32}, {33, 34}, {35, 36}}},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(std::string(c.lon) + " from " + std::to_string(c.first));
        EXPECT_EQ(readShape(keys(c.lon, c.lat, c.first, {c.a, c.b, c.c})), (Shape{{c.expected}}));
    }
}

// Proof 1: the order of the key chain (:271-297) decides when several spellings are present.
TEST(ConfigProviderHelpers, ReaderSpellingOrderPointLonThenLLPointThenPointX) {
    const std::vector<Pt> pl{{"1", "2"}, {"3", "4"}, {"5", "6"}};
    const std::vector<Pt> ll{{"11", "12"}, {"13", "14"}, {"15", "16"}};
    const std::vector<Pt> px{{"21", "22"}, {"23", "24"}, {"25", "26"}};
    const Pts plExpected{{1, 2}, {3, 4}, {5, 6}};
    const Pts llExpected{{11, 12}, {13, 14}, {15, 16}};

    // All three at index 0: PointLon_ wins.
    EXPECT_EQ(readShape(keys("PointX_", "PointY_", 0, px) + keys("LLPointX_", "LLPointY_", 0, ll) +
                        keys("PointLon_", "PointLat_", 0, pl)),
              (Shape{{plExpected}}));
    // LLPointX_ and PointX_ at index 0: LLPointX_ wins.
    EXPECT_EQ(readShape(keys("PointX_", "PointY_", 0, px) + keys("LLPointX_", "LLPointY_", 0, ll)),
              (Shape{{llExpected}}));
    // PointLon_ at index 1 is tried before LLPointX_ at index 0 (the spelling is the outer loop).
    EXPECT_EQ(readShape(keys("LLPointX_", "LLPointY_", 0, ll) + keys("PointLon_", "PointLat_", 1, pl)),
              (Shape{{plExpected}}));
}

// Proof 2: empty / NaN / out-of-range first points.
TEST(ConfigProviderHelpers, ReaderRejectsEmptySectionAndNanFirstPoint) {
    const GeometryLonLatFactory factory;
    EXPECT_EQ(readPolygon(*sectionFromText(""), factory), nullptr);
    // NaN in lon or in lat at both candidate indices; nothing else to fall back to.
    EXPECT_EQ(readPolygon(*sectionFromText(keys("PointLon_", "PointLat_", 0, {{"NAN", "5"}, {"5", "NAN"}})),
                          factory),
              nullptr);
    EXPECT_EQ(readPolygon(*sectionFromText(keys("PointLon_", "PointLat_", 0, {{"5", "NAN"}, {"NAN", "7"}})),
                          factory),
              nullptr);
}

TEST(ConfigProviderHelpers, ReaderRejectsFirstPointAtOrAbove360ThenTriesNextCandidate) {
    const GeometryLonLatFactory factory;
    // 360 exactly and 361, in x and in y, at every spelling: null.
    for (const char* bad : {"360", "361", "-360", "-361"}) {
        for (const bool inX : {true, false}) {
            const Pt p = inX ? Pt{bad, "5"} : Pt{"5", bad};
            for (const char* spelling : {"0", "1", "2"}) {
                const char* lon = spelling[0] == '0'   ? "PointLon_"
                                  : spelling[0] == '1' ? "LLPointX_"
                                                       : "PointX_";
                const char* lat = spelling[0] == '0'   ? "PointLat_"
                                  : spelling[0] == '1' ? "LLPointY_"
                                                       : "PointY_";
                SCOPED_TRACE(std::string(bad) + (inX ? " x " : " y ") + lon);
                EXPECT_EQ(readPolygon(*sectionFromText(keys(lon, lat, 0, {p, p})), factory), nullptr);
            }
        }
    }
    // Invalid at index 0 of PointLon_, valid at index 1: the read starts at index 1 (index 0 is skipped).
    EXPECT_EQ(
        readShape(keys("PointLon_", "PointLat_", 0, {{"360", "40"}, {"1", "2"}, {"3", "4"}, {"5", "6"}})),
        (Shape{{Pts{{1, 2}, {3, 4}, {5, 6}}}}));
    EXPECT_EQ(
        readShape(keys("PointLon_", "PointLat_", 0, {{"40", "361"}, {"1", "2"}, {"3", "4"}, {"5", "6"}})),
        (Shape{{Pts{{1, 2}, {3, 4}, {5, 6}}}}));
    // Both PointLon_ candidates invalid: the next spelling is tried.
    EXPECT_EQ(readShape(keys("PointLon_", "PointLat_", 0, {{"360", "1"}, {"1", "361"}}) +
                        keys("LLPointX_", "LLPointY_", 0, {{"7", "8"}, {"9", "10"}, {"11", "12"}})),
              (Shape{{Pts{{7, 8}, {9, 10}, {11, 12}}}}));
}

// Proof 3: separators, missing key, literal comparisons, flush on the finishing pass.
TEST(ConfigProviderHelpers, ReaderNanNumberPairOpensHoleAndMissingKeyEndsRead) {
    // Index 7 is absent (reads -10000, |x| > 360): the read ends and the hole is flushed on that pass.
    const QByteArray text =
        keys("PointLon_", "PointLat_", 0,
             {{"1", "2"}, {"3", "4"}, {"5", "6"}, {"NAN", "-1"}, {"11", "12"}, {"13", "14"}, {"15", "16"}});
    EXPECT_EQ(readShape(text), (Shape{{Pts{{1, 2}, {3, 4}, {5, 6}}, Pts{{11, 12}, {13, 14}, {15, 16}}}}));
}

TEST(ConfigProviderHelpers, ReaderNanNanPairOpensNewOuterContourAsMultiPolygon) {
    const QByteArray text = keys("PointLon_", "PointLat_", 0,
                                 {{"1", "2"},
                                  {"3", "4"},
                                  {"5", "6"},
                                  {"NAN", "-1"},
                                  {"11", "12"},
                                  {"13", "14"},
                                  {"15", "16"},
                                  {"NAN", "NAN"},
                                  {"21", "22"},
                                  {"23", "24"},
                                  {"25", "26"}});
    EXPECT_EQ(readShape(text), (Shape{{Pts{{1, 2}, {3, 4}, {5, 6}}, Pts{{11, 12}, {13, 14}, {15, 16}}},
                                      {Pts{{21, 22}, {23, 24}, {25, 26}}}}));
}

// :318 the isOuter update sits inside "Count > 0": a separator met with an empty contour does not touch it.
TEST(ConfigProviderHelpers, ReaderSeparatorWithEmptyContourKeepsHoleState) {
    const QByteArray text = keys("PointLon_", "PointLat_", 0,
                                 {{"1", "2"},
                                  {"3", "4"},
                                  {"5", "6"},
                                  {"NAN", "-1"},
                                  {"NAN", "NAN"}, // contour is empty here: no flush, isOuter stays false
                                  {"11", "12"},
                                  {"13", "14"},
                                  {"15", "16"}});
    EXPECT_EQ(readShape(text), (Shape{{Pts{{1, 2}, {3, 4}, {5, 6}}, Pts{{11, 12}, {13, 14}, {15, 16}}}}));
}

// :312 the finish test is "> 360"; 360 itself is appended (literal comparison, unlike the first point).
TEST(ConfigProviderHelpers, ReaderValueOf360InsideRunIsAppendedAbove360EndsRead) {
    // x == 360 appended; the next point has y == -360 (also appended); 361 ends the read and that point is
    // dropped.
    EXPECT_EQ(
        readShape(keys("PointLon_", "PointLat_", 0,
                       {{"1", "2"}, {"3", "4"}, {"360", "6"}, {"7", "-360"}, {"361", "8"}, {"9", "10"}})),
        (Shape{{Pts{{1, 2}, {3, 4}, {360, 6}, {7, -360}}}}));
    // y > 360 ends the read too; the contour collected so far is flushed on the finishing pass.
    EXPECT_EQ(readShape(keys("PointLon_", "PointLat_", 0,
                             {{"1", "2"}, {"3", "4"}, {"5", "6"}, {"7", "361"}, {"9", "10"}})),
              (Shape{{Pts{{1, 2}, {3, 4}, {5, 6}}}}));
}

// Proof 4: round trip. The inputs are values of the Pascal key rules, the expected text is a literal.
TEST(ConfigProviderHelpers, RoundTripSinglePolygonWithoutHoles) {
    const Pts outer{{10.5, 20.25}, {30.5, 40.25}, {50.5, 60.25}};
    IConfigWriterPtr section = ConfigProviderIni::wrap(std::make_shared<IniFile>())->orCreateSubItem(u"S");
    writePolygon(*section, makeSingle(outer, {}));
    EXPECT_EQ(sortedKeys(section), (QStringList{"PointLat_0", "PointLat_1", "PointLat_2", "PointLon_0",
                                                "PointLon_1", "PointLon_2"}));
    EXPECT_EQ(section->readFloat(u"PointLon_1", 0), 30.5);
    EXPECT_EQ(section->readFloat(u"PointLat_2", 0), 60.25);
    const GeometryLonLatFactory factory;
    EXPECT_EQ(shapeOf(readPolygon(*section, factory)), (Shape{{outer}}));
}

TEST(ConfigProviderHelpers, RoundTripSinglePolygonWithOneHole) {
    const Pts outer{{10.5, 20.25}, {30.5, 40.25}, {50.5, 60.25}};
    const Pts hole{{11.5, 21.25}, {12.5, 22.25}, {13.5, 23.25}};
    IConfigWriterPtr section = ConfigProviderIni::wrap(std::make_shared<IniFile>())->orCreateSubItem(u"S");
    writePolygon(*section, makeSingle(outer, {hole}));
    // Outer at 0..2, separator at 3 (NAN, -1), hole at 4..6.
    EXPECT_EQ(section->readString(u"PointLon_3"), QStringLiteral("NAN"));
    EXPECT_EQ(section->readString(u"PointLat_3"), QStringLiteral("-1"));
    EXPECT_EQ(section->readFloat(u"PointLon_4", 0), 11.5);
    EXPECT_EQ(section->readFloat(u"PointLat_6", 0), 23.25);
    EXPECT_EQ(section->valueNames().size(), 14);
    const GeometryLonLatFactory factory;
    EXPECT_EQ(shapeOf(readPolygon(*section, factory)), (Shape{{outer, hole}}));
}

// Proof 5: the fixed writer, DEVIATION D-158 (T-0272, L-260; legacy u_ConfigProviderHelpers.pas:363-365,
// 389-391 writes the separator at startIndex + h / + i). Keys are contiguous, the reader returns the full
// shape.
TEST(ConfigProviderHelpers, TwoHolesRoundTrip) {
    const Pts outer{{10.5, 20.25}, {30.5, 40.25}, {50.5, 60.25}};
    const Pts hole1{{11.5, 21.25}, {12.5, 22.25}, {13.5, 23.25}};
    const Pts hole2{{14.5, 24.25}, {15.5, 25.25}, {16.5, 26.25}};
    IConfigWriterPtr section = ConfigProviderIni::wrap(std::make_shared<IniFile>())->orCreateSubItem(u"S");
    writePolygon(*section, makeSingle(outer, {hole1, hole2}));
    // Outer 0..2; separator 3; hole1 4..6; separator 7; hole2 8..10.
    QStringList expected;
    for (int i = 0; i <= 10; ++i) {
        expected << QStringLiteral("PointLat_%1").arg(i) << QStringLiteral("PointLon_%1").arg(i);
    }
    expected.sort();
    EXPECT_EQ(sortedKeys(section), expected);
    for (const int i : {3, 7}) {
        EXPECT_EQ(section->readString(QStringLiteral("PointLon_%1").arg(i)), QStringLiteral("NAN"));
        EXPECT_EQ(section->readString(QStringLiteral("PointLat_%1").arg(i)), QStringLiteral("-1"));
    }
    EXPECT_EQ(section->readFloat(u"PointLon_8", 0), 14.5);
    const GeometryLonLatFactory factory;
    EXPECT_EQ(shapeOf(readPolygon(*section, factory)), (Shape{{outer, hole1, hole2}}));
}

TEST(ConfigProviderHelpers, TwoItemMultiPolygonRoundTrip) {
    const Pts a{{10.5, 20.25}, {30.5, 40.25}, {50.5, 60.25}};
    const Pts b{{11.5, 21.25}, {12.5, 22.25}, {13.5, 23.25}};
    IConfigWriterPtr section = ConfigProviderIni::wrap(std::make_shared<IniFile>())->orCreateSubItem(u"S");
    writePolygon(*section, makeMulti(a, b));
    // Item 0 at 0..2, separator at 3, item 1 at 4..6.
    QStringList expected;
    for (int i = 0; i <= 6; ++i) {
        expected << QStringLiteral("PointLat_%1").arg(i) << QStringLiteral("PointLon_%1").arg(i);
    }
    expected.sort();
    EXPECT_EQ(sortedKeys(section), expected);
    EXPECT_EQ(section->readString(u"PointLon_3"), QStringLiteral("NAN"));
    EXPECT_EQ(section->readString(u"PointLat_3"), QStringLiteral("NAN"));
    EXPECT_EQ(section->readFloat(u"PointLon_4", 0), 11.5);
    const GeometryLonLatFactory factory;
    EXPECT_EQ(shapeOf(readPolygon(*section, factory)), (Shape{{a}, {b}}));
}

TEST(ConfigProviderHelpers, ThreeItemMultiPolygonWithTwoHolesInSecondRoundTrip) {
    const Pts a{{10.5, 20.25}, {30.5, 40.25}, {50.5, 60.25}};
    const Pts b{{11.5, 21.25}, {12.5, 22.25}, {13.5, 23.25}};
    const Pts bh1{{11.6, 21.3}, {11.7, 21.4}, {11.8, 21.5}};
    const Pts bh2{{12.1, 22.1}, {12.2, 22.2}, {12.3, 22.3}};
    const Pts c{{70.5, 10.25}, {71.5, 11.25}, {72.5, 12.25}};
    GeometryLonLatFactory factory;
    auto builder = factory.makePolygonBuilder();
    builder.addOuter(ptr(a));
    builder.addOuter(ptr(b));
    builder.addHole(ptr(bh1));
    builder.addHole(ptr(bh2));
    builder.addOuter(ptr(c));
    const auto polygon =
        std::dynamic_pointer_cast<const IGeometryLonLatMultiPolygon>(builder.makeStaticAndClear());
    ASSERT_TRUE(polygon != nullptr);
    IConfigWriterPtr section = ConfigProviderIni::wrap(std::make_shared<IniFile>())->orCreateSubItem(u"S");
    writePolygon(*section, polygon);
    // a 0..2, sep 3, b 4..6, sep 7, bh1 8..10, sep 11, bh2 12..14, sep 15, c 16..18: 19 contiguous indices.
    EXPECT_EQ(section->valueNames().size(), 38);
    EXPECT_EQ(section->readString(u"PointLon_15"), QStringLiteral("NAN"));
    EXPECT_EQ(section->readString(u"PointLat_15"), QStringLiteral("NAN"));
    EXPECT_EQ(shapeOf(readPolygon(*section, factory)), (Shape{{a}, {b, bh1, bh2}, {c}}));
}

// The reader stays legacy: a file written by the old writer (hand-written keys, gap at the separator index)
// still reads the first part only.
TEST(ConfigProviderHelpers, LegacyWrittenTwoItemLayoutReadsFirstItemOnly) {
    // Legacy: item 0 at 0..2; the separator at 4 is overwritten by item 1 at 4..6; index 3 is missing.
    EXPECT_EQ(
        readShape(
            keys("PointLon_", "PointLat_", 0, {{"10.5", "20.25"}, {"30.5", "40.25"}, {"50.5", "60.25"}}) +
            keys("PointLon_", "PointLat_", 4, {{"11.5", "21.25"}, {"12.5", "22.25"}, {"13.5", "23.25"}})),
        (Shape{{Pts{{10.5, 20.25}, {30.5, 40.25}, {50.5, 60.25}}}}));
}

// Proof 6.
TEST(ConfigProviderHelpers, WriteNullPolygonWritesNoKey) {
    IConfigWriterPtr section = ConfigProviderIni::wrap(std::make_shared<IniFile>())->orCreateSubItem(u"S");
    writePolygon(*section, nullptr);
    EXPECT_TRUE(section->valueNames().isEmpty());
}
