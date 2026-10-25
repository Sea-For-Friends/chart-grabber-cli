// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-012. Ports Test/u_GeometryMetaSaveLoadJson_Test.pas (5 published cases --
// TestEmptyMeta/TestSingleLineWithMeta/TestSingleLineWithoutMeta/TestMultiLine/TestPerformance;
// the ticket's spec extract names 4, but the legacy source has 5, see docs/handoffs/T-0017A.md)
// and docs/specs/geo.md §11.2 geometry_meta_json_test.cpp (2 new), plus 2 more added in Rework 1
// (docs/handoffs/T-0017A-review.md F4/F5) -- 9 cases total.

#include "cg/core/platform/qt_compat.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/wkb.h"

#include <QByteArrayView>
#include <QDate>
#include <QDateTime>
#include <QTime>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using cg::geo::GeometryLonLatFactory;
using cg::geo::IGeometryLonLatSingleLine;
using cg::geo::LonLat;
using cg::geo::parseMetaJson;
using cg::geo::PointsMeta;
using cg::geo::writeMetaJson;

namespace {

// Ports TestGeometryMetaSaveLoadJson.CreateTestSingleLine: `count` points, all at (0,0),
// elevation[i] = i*0.5, timestamp[i] = base + i minutes.
std::shared_ptr<const IGeometryLonLatSingleLine>
makeTestSingleLine(const GeometryLonLatFactory& factory, int count, bool withElevation, bool withTimestamp) {
    const std::vector<LonLat> pts(static_cast<std::size_t>(count), LonLat{0, 0});
    PointsMeta meta;
    const QDateTime base = cg::core::platform::utcFromMSecsSinceEpoch(1'700'000'000'000);
    if (withElevation) {
        meta.elevation.resize(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            meta.elevation[static_cast<std::size_t>(i)] = i * 0.5;
        }
    }
    if (withTimestamp) {
        meta.timestamp.resize(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            meta.timestamp[static_cast<std::size_t>(i)] = base.addSecs(i * 60);
        }
    }
    auto line = factory.createLine(pts, (withElevation || withTimestamp) ? &meta : nullptr);
    return std::dynamic_pointer_cast<const IGeometryLonLatSingleLine>(line);
}

// Ports CreateTestMultilineLine: 5 sub-lines of (10,none) (20,time) (30,elevation) (40,none)
// (50,both), joined via the line builder's addLine(const IGeometryLonLatLine&) re-add path.
cg::geo::GeometryLonLatPtr makeTestMultiLine(const GeometryLonLatFactory& factory) {
    auto builder = factory.makeLineBuilder();
    builder.addLine(*makeTestSingleLine(factory, 10, false, false));
    builder.addLine(*makeTestSingleLine(factory, 20, false, true));
    builder.addLine(*makeTestSingleLine(factory, 30, true, false));
    builder.addLine(*makeTestSingleLine(factory, 40, false, false));
    builder.addLine(*makeTestSingleLine(factory, 50, true, true));
    return builder.makeStaticAndClear();
}

} // namespace

// --- 14. u_GeometryMetaSaveLoadJson_Test.pas -> ported -----------------------------------------

TEST(GeometryMetaJson, TestSingleLineWithMeta) {
    GeometryLonLatFactory factory;
    auto line = makeTestSingleLine(factory, 1000, true, true);
    const QByteArray blob = writeMetaJson(*line);
    EXPECT_FALSE(blob.isEmpty());

    auto parsed = parseMetaJson(blob);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->elevation.size(), line->meta().elevation.size());
    ASSERT_EQ(parsed->timestamp.size(), line->meta().timestamp.size());
    for (std::size_t i = 0; i < parsed->elevation.size(); ++i) {
        EXPECT_NEAR(parsed->elevation[i], line->meta().elevation[i], 0.0001);
        EXPECT_NEAR(static_cast<double>(parsed->timestamp[i].toMSecsSinceEpoch()),
                    static_cast<double>(line->meta().timestamp[i].toMSecsSinceEpoch()),
                    0.000001 * 86400000.0); // 1e-6 days
    }
}

TEST(GeometryMetaJson, TestSingleLineWithoutMeta) {
    GeometryLonLatFactory factory;
    auto line = makeTestSingleLine(factory, 10, false, false);
    const QByteArray blob = writeMetaJson(*line);
    EXPECT_TRUE(blob.isEmpty());

    auto parsed = parseMetaJson(blob);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->isEmpty());
}

TEST(GeometryMetaJson, TestMultiLine) {
    GeometryLonLatFactory factory;
    auto multi = makeTestMultiLine(factory);
    const QByteArray blob = writeMetaJson(*multi);
    EXPECT_FALSE(blob.isEmpty());

    auto parsed = parseMetaJson(blob);
    ASSERT_TRUE(parsed.has_value());
    // sum(c) + (sub-lines - 1) separators = (10+20+30+40+50) + 4 = 154.
    ASSERT_EQ(parsed->elevation.size(), 154u);
    ASSERT_EQ(parsed->timestamp.size(), 154u);

    // Sub-line 3 (0-based index 2, "elevation only", 30 points) starts at flattened offset
    // 10 + 1 + 20 + 1 = 32: real elevation, but no timestamp (globally present via sub-lines 2/5).
    // Tech Lead ruling (review F3): the gap is legacy's literal FillChar(...,0)
    // (u_DoublePointsMetaAggregator.pas:126,196), i.e. the Delphi TDateTime "zero date"
    // 1899-12-30 -- a *valid* QDateTime, not an invalid/null one.
    const QDateTime delphiZeroDate = cg::core::platform::utcDateTime(QDate(1899, 12, 30), QTime(0, 0));
    for (int i = 0; i < 30; ++i) {
        EXPECT_NEAR(parsed->elevation[32 + static_cast<std::size_t>(i)], i * 0.5, 0.0001);
        EXPECT_TRUE(parsed->timestamp[32 + static_cast<std::size_t>(i)].isValid());
        EXPECT_EQ(parsed->timestamp[32 + static_cast<std::size_t>(i)], delphiZeroDate);
    }
    // Sub-line 4 ("no meta", 40 points) starts right after at 32 + 30 + 1 = 63: elevation gap is
    // zero, matching legacy's FillChar(...,0), not NaN.
    for (int i = 0; i < 40; ++i) {
        EXPECT_DOUBLE_EQ(parsed->elevation[63 + static_cast<std::size_t>(i)], 0.0);
    }
}

TEST(GeometryMetaJson, TestEmptyMeta) {
    // The legacy table's 2nd/3rd cases expect a non-null meta with a specific `.Count` even
    // though no sub-line actually carries elevation/timestamp data -- that bare-count concept has
    // no equivalent in PointsMeta (deliberately: "do not carry a per-item ok flag",
    // docs/specs/geo.md §3.2), so this port collapses a declared-but-dataless payload to the same
    // "no meta" (an empty PointsMeta) as a genuinely empty one. See docs/handoffs/T-0017A.md.
    auto empty1 = parseMetaJson(QByteArrayView());
    ASSERT_TRUE(empty1.has_value());
    EXPECT_TRUE(empty1->isEmpty());

    const QByteArray json2 = QByteArrayLiteral("JSON{\"v\":1,\"t\":1,\"g\":[{\"m\":[],\"c\":10}]}");
    auto empty2 = parseMetaJson(json2);
    ASSERT_TRUE(empty2.has_value());
    EXPECT_TRUE(empty2->isEmpty());

    const QByteArray json3 =
        QByteArrayLiteral("JSON{\"v\":1,\"t\":1,\"g\":[{\"m\":[],\"c\":30},{\"m\":[],\"c\":40}]}");
    auto empty3 = parseMetaJson(json3);
    ASSERT_TRUE(empty3.has_value());
    EXPECT_TRUE(empty3->isEmpty());
}

TEST(GeometryMetaJson, TestPerformance) {
    // Ported as a plain correctness case, without timing (docs/backlog/T-0017A ticket text).
    GeometryLonLatFactory factory;
    auto line = makeTestSingleLine(factory, 9000, true, true);
    const QByteArray blob = writeMetaJson(*line);
    EXPECT_FALSE(blob.isEmpty());
    auto parsed = parseMetaJson(blob);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->elevation.size(), 9000u);
}

// --- new coverage: docs/specs/geo.md §11.2 geometry_meta_json_test.cpp (2 cases) ---------------

TEST(GeometryMetaJson, PrefixIsLiteralJson) {
    GeometryLonLatFactory factory;
    auto line = makeTestSingleLine(factory, 5, true, false);
    const QByteArray blob = writeMetaJson(*line);
    ASSERT_GE(blob.size(), 4);
    EXPECT_EQ(blob.first(4), QByteArrayView("JSON", 4));
}

TEST(GeometryMetaJson, MalformedPayloadReturnsError) {
    // The "JSON" prefix is present (so the magic check passes) but the body is not valid JSON.
    auto result = parseMetaJson(QByteArrayLiteral("JSON{not valid json"));
    EXPECT_FALSE(result.has_value());
}

// --- Rework 1: docs/handoffs/T-0017A-review.md F4/F5 --------------------------------------------

TEST(GeometryMetaJson, LiteralKeyOrderMatchesLegacy) {
    // Tech Lead ruling (review F4): writeMetaJson must emit keys in TGeometryMetaToStreamJson's
    // exact order (v,t,g / c,m / t,n,d), not QJsonObject's alphabetically-sorted order (which
    // would reorder the root object to g,t,v). Pin the literal output string.
    GeometryLonLatFactory factory;
    auto line = makeTestSingleLine(factory, 2, true, false); // elevation = {0.0, 0.5}
    const QByteArray blob = writeMetaJson(*line);

    const std::vector<double> ele = {0.0, 0.5};
    const QByteArray base64 = QByteArray(reinterpret_cast<const char*>(ele.data()),
                                         static_cast<qsizetype>(ele.size() * sizeof(double)))
                                  .toBase64();
    const QByteArray expected =
        QByteArrayLiteral("JSON{\"v\":1,\"t\":1,\"g\":[{\"c\":2,\"m\":[{\"t\":1,\"n\":"
                          "\"ele\",\"d\":\"") +
        base64 + QByteArrayLiteral("\"}]}]}");
    EXPECT_EQ(blob, expected);
}

TEST(GeometryMetaJson, UnknownVersionOrTypeReturnsNoMetaNotError) {
    // Tech Lead ruling (review F5): legacy's Parse hits Assert(False); Exit for an unrecognised
    // "v" or "t" and returns nil ("no meta"), not a parse failure
    // (u_GeometryMetaFromStreamJson.pas:103-112).
    auto badVersion = parseMetaJson(QByteArrayLiteral("JSON{\"v\":2,\"t\":1,\"g\":[]}"));
    ASSERT_TRUE(badVersion.has_value());
    EXPECT_TRUE(badVersion->isEmpty());

    auto badType = parseMetaJson(QByteArrayLiteral("JSON{\"v\":1,\"t\":0,\"g\":[]}"));
    ASSERT_TRUE(badType.has_value());
    EXPECT_TRUE(badType->isEmpty());
}
