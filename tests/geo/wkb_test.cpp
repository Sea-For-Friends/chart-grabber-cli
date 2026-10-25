// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-012. Ports Test/u_GeometrySaveLoadWKB_Test.pas (8 cases) and
// docs/specs/geo.md §11.2 wkb_test.cpp (4 new), plus 2 more added in Rework 1
// (docs/handoffs/T-0017A-review.md F1/F7) -- 14 cases total.

#include "cg/geo/geometry_factory.h"
#include "cg/geo/wkb.h"

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <memory>
#include <vector>

using cg::geo::emptyDoublePoint;
using cg::geo::GeometryLonLatFactory;
using cg::geo::IGeometryLonLatMultiLine;
using cg::geo::LonLat;
using cg::geo::parseWkb;
using cg::geo::PointsMeta;
using cg::geo::writeWkb;

namespace {
QByteArray hexToBin(const char* hex) {
    return QByteArray::fromHex(QByteArray(hex));
}
} // namespace

// --- 11. u_GeometrySaveLoadWKB_Test.pas -> ported (8 cases) -------------------------------------

TEST(Wkb, TestPoint) {
    GeometryLonLatFactory factory;
    auto geom = factory.createPoint({10, 15});
    const QByteArray blob = writeWkb(*geom);
    auto parsed = parseWkb(blob, factory);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_NE(*parsed, nullptr);
    EXPECT_TRUE(geom->isSameGeometry(**parsed));
    EXPECT_TRUE((*parsed)->isSameGeometry(*geom));
}

TEST(Wkb, TestSingleLine) {
    GeometryLonLatFactory factory;
    const std::vector<LonLat> pts = {{0, 1}, {1, 1}, {1, 0}};
    auto geom = factory.createLine(pts, nullptr);
    const QByteArray blob = writeWkb(*geom);
    auto parsed = parseWkb(blob, factory);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(geom->isSameGeometry(**parsed));
    EXPECT_TRUE((*parsed)->isSameGeometry(*geom));
}

TEST(Wkb, TestSinglePolygon) {
    GeometryLonLatFactory factory;
    const std::vector<LonLat> pts = {{0, 1}, {1, 1}, {1, 0}, {0, 1}};
    auto geom = factory.createPolygon(pts);
    const QByteArray blob = writeWkb(*geom);
    auto parsed = parseWkb(blob, factory);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(geom->isSameGeometry(**parsed));
    EXPECT_TRUE((*parsed)->isSameGeometry(*geom));
}

TEST(Wkb, TestMultiLine) {
    GeometryLonLatFactory factory;
    const std::vector<LonLat> pts = {{0, 1}, {1, 1}, {1, 0}, LonLat::fromPoint(emptyDoublePoint()),
                                     {2, 2}, {3, 3}, {4, 4}};
    auto geom = factory.createLine(pts, nullptr);
    const QByteArray blob = writeWkb(*geom);
    auto parsed = parseWkb(blob, factory);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(geom->isSameGeometry(**parsed));
    EXPECT_TRUE((*parsed)->isSameGeometry(*geom));
}

TEST(Wkb, TestMultiPolygon) {
    GeometryLonLatFactory factory;
    const std::vector<LonLat> pts = {{0, 1}, {1, 1}, {1, 0}, {0, 1}, LonLat::fromPoint(emptyDoublePoint()),
                                     {2, 2}, {3, 3}, {4, 2}, {2, 2}};
    auto geom = factory.createPolygon(pts);
    const QByteArray blob = writeWkb(*geom);
    auto parsed = parseWkb(blob, factory);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(geom->isSameGeometry(**parsed));
    EXPECT_TRUE((*parsed)->isSameGeometry(*geom));
}

TEST(Wkb, TestLoadMySqlPointBinary) {
    // WKT: POINT(1 4)
    GeometryLonLatFactory factory;
    const QByteArray blob = hexToBin("0101000000000000000000F03F0000000000001040");
    auto parsed = parseWkb(blob, factory);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_NE(*parsed, nullptr);
    auto expected = factory.createPoint({1, 4});
    EXPECT_TRUE(expected->isSameGeometry(**parsed));
    EXPECT_TRUE((*parsed)->isSameGeometry(*expected));
}

TEST(Wkb, TestLoadMySqlSingleLineBinary) {
    // WKT: LINESTRING(0 0,10 10,20 25,50 60)
    GeometryLonLatFactory factory;
    const QByteArray blob =
        hexToBin("01020000000400000000000000000000000000000000000000000000000000244000000000000024400000"
                 "00000000344000000000000039400000000000004940000000000000"
                 "4E40");
    auto parsed = parseWkb(blob, factory);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_NE(*parsed, nullptr);
    const std::vector<LonLat> pts = {{0, 0}, {10, 10}, {20, 25}, {50, 60}};
    auto expected = factory.createLine(pts, nullptr);
    EXPECT_TRUE(expected->isSameGeometry(**parsed));
    EXPECT_TRUE((*parsed)->isSameGeometry(*expected));
}

TEST(Wkb, TestLoadMySqlSinglePolygonBinary) {
    // WKT: POLYGON((0 0,10 0,10 10,0 10,0 0))
    GeometryLonLatFactory factory;
    const QByteArray blob =
        hexToBin("010300000001000000050000000000000000000000000000000000000000000000000024400000000000"
                 "00000000000000000024400000000000002440000000000000000000000000000024400000000000000000"
                 "0000000000000000");
    auto parsed = parseWkb(blob, factory);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_NE(*parsed, nullptr);
    const std::vector<LonLat> pts = {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}};
    auto expected = factory.createPolygon(pts);
    EXPECT_TRUE(expected->isSameGeometry(**parsed));
    EXPECT_TRUE((*parsed)->isSameGeometry(*expected));
}

// --- new coverage: docs/specs/geo.md §11.2 wkb_test.cpp (4 cases) ------------------------------

TEST(Wkb, RingGrowsByOneOnRoundTrip) {
    // §3.7 rule 5: SaveContour writes count+1, re-emitting points[0], so the ring is on the wire
    // CLOSED even though IGeometryLonLatContour::points() reports it open. Uses a pre-closed
    // input (the last point literally repeats the first) exactly like legacy's own
    // u_GeometrySaveLoadWKB_Test.pas TestSinglePolygon: GeometryLonLatContour's pre-closed-input
    // trim (geometry_lonlat.h) reduces it to an open 3-point ring at construction, which is
    // exactly what a fresh read of the +1-on-the-wire blob below reconstructs too -- so hash()
    // (computed over the untrimmed buffer, like legacy's CalcHashByBuffer) matches on both sides.
    // A NOT-pre-closed input can come back from one round trip with a different hash despite
    // identical points() (the untrimmed buffers differ by exactly the appended vertex); noted as
    // an open question in docs/handoffs/T-0017A.md -- legacy's own test suite avoids it the same
    // way, by only ever round-tripping pre-closed rings.
    GeometryLonLatFactory factory;
    const std::vector<LonLat> pts = {{0, 1}, {1, 1}, {1, 0}, {0, 1}};
    auto polygon =
        std::dynamic_pointer_cast<const cg::geo::IGeometryLonLatSinglePolygon>(factory.createPolygon(pts));
    ASSERT_NE(polygon, nullptr);
    ASSERT_EQ(polygon->outerBorder()->points().size(), 3u); // trimmed at construction

    const QByteArray blob = writeWkb(*polygon);
    // byte-order(1) + type(4) + ringCount(4) + firstRingVertexCount(4 LE) at offset 9.
    quint32 vertexCountOnWire = 0;
    std::memcpy(&vertexCountOnWire, blob.constData() + 9, sizeof(vertexCountOnWire));
    EXPECT_EQ(vertexCountOnWire, 4u); // (trimmed) count 3 + 1

    auto parsed = parseWkb(blob, factory);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(polygon->isSameGeometry(**parsed));
}

TEST(Wkb, MultiPointAndGeometryCollectionAreRejected) {
    // Rule 1: type codes 4 (MultiPoint) and 7 (GeometryCollection) are neither read nor written.
    GeometryLonLatFactory factory;
    for (const quint32 type : {4u, 7u}) {
        QByteArray blob;
        blob.append(char(1));
        blob.append(reinterpret_cast<const char*>(&type), sizeof(type));
        EXPECT_FALSE(parseWkb(blob, factory).has_value());
    }
}

TEST(Wkb, BigEndianIsRejected) {
    // Rule 2: every byte-order byte is checked; a non-1 value aborts the parse.
    GeometryLonLatFactory factory;
    QByteArray blob;
    blob.append(char(0)); // big-endian marker
    const quint32 type = 1;
    blob.append(reinterpret_cast<const char*>(&type), sizeof(type));
    const LonLat p{1, 4};
    blob.append(reinterpret_cast<const char*>(&p), sizeof(p));
    EXPECT_FALSE(parseWkb(blob, factory).has_value());
}

TEST(Wkb, HugeDeclaredCountOnCorruptBlobFailsInsteadOfCrashing) {
    // Review F1 (blocker): readPoints used to allocate `count` points BEFORE checking that
    // `count * 16` bytes actually remain in the blob, so a 9-byte corrupt blob whose declared
    // count is huge could force a multi-GB allocation and let std::bad_alloc escape parseWkb
    // uncaught (wkb.h:24-27's contract: "a corrupt blob must be skippable, not fatal"). `count`
    // here is comfortably under the rule-4 vertex-count cap (~134,217,727) but nowhere near
    // satisfied by the blob's actual (zero) remaining bytes.
    GeometryLonLatFactory factory;
    QByteArray blob;
    blob.append(char(1));   // byte-order
    const quint32 type = 2; // LineString
    blob.append(reinterpret_cast<const char*>(&type), sizeof(type));
    const quint32 hugeCount = 100'000'000;
    blob.append(reinterpret_cast<const char*>(&hugeCount), sizeof(hugeCount));
    // No point data follows -- the blob is 9 bytes total, nowhere near hugeCount * 16 bytes.
    auto result = parseWkb(blob, factory);
    EXPECT_FALSE(result.has_value());
}

TEST(Wkb, SinglePointContourWritesRepeatedVertex) {
    // Review F7: writeContour used to special-case an empty ring (write count=1 but 0 points),
    // producing a malformed blob for a case legacy's own SaveContour never guards -- because
    // GeometryLonLatContour's constructor (like TGeometryLonLatBase.Create) already asserts a ring
    // is never empty. The smallest legal ring (one point after the pre-closed-input trim) is the
    // boundary case right next to the removed branch: writeContour must still write count+1=2
    // vertices, both copies of that one point, unconditionally.
    GeometryLonLatFactory factory;
    const std::vector<LonLat> pts = {{5, 6}, {5, 6}}; // pre-closed input: trims to a 1-point ring
    auto polygon =
        std::dynamic_pointer_cast<const cg::geo::IGeometryLonLatSinglePolygon>(factory.createPolygon(pts));
    ASSERT_NE(polygon, nullptr);
    ASSERT_EQ(polygon->outerBorder()->points().size(), 1u);

    const QByteArray blob = writeWkb(*polygon);
    quint32 vertexCountOnWire = 0;
    std::memcpy(&vertexCountOnWire, blob.constData() + 9, sizeof(vertexCountOnWire));
    EXPECT_EQ(vertexCountOnWire, 2u);
    LonLat wirePoints[2];
    std::memcpy(wirePoints, blob.constData() + 13, sizeof(wirePoints));
    EXPECT_EQ(wirePoints[0], (LonLat{5, 6}));
    EXPECT_EQ(wirePoints[1], (LonLat{5, 6}));

    auto parsed = parseWkb(blob, factory);
    ASSERT_TRUE(parsed.has_value());
    auto parsedPolygon = std::dynamic_pointer_cast<const cg::geo::IGeometryLonLatSinglePolygon>(*parsed);
    ASSERT_NE(parsedPolygon, nullptr);
    EXPECT_EQ(parsedPolygon->outerBorder()->points().size(), 1u);
    EXPECT_TRUE(polygon->isSameGeometry(**parsed)); // pre-closed input: hashes agree either side
}

TEST(Wkb, MetaIndexSkipsOneSeparatorPerSubLine) {
    // Rule 9: parseWkb's meta is indexed as if the geometry were the flattened NaN-separated
    // stream -- inside a multi-line the index advances by sub-line.count() PLUS ONE per sub-line.
    // Two sub-lines of 2 points: the flattened stream is [0,1, sep, 3,4], so sub-line 2's
    // elevations start at flattened index 3, not 2.
    GeometryLonLatFactory factory;
    PointsMeta meta;
    meta.elevation = {1.0, 2.0, std::numeric_limits<double>::quiet_NaN(), 3.0, 4.0};
    const std::vector<LonLat> pts = {{0, 0}, {1, 1}, LonLat::fromPoint(emptyDoublePoint()), {2, 2}, {3, 3}};

    auto original = factory.createLine(pts, &meta);
    const QByteArray blob = writeWkb(*original);

    auto parsed = parseWkb(blob, factory, &meta);
    ASSERT_TRUE(parsed.has_value());
    auto multi = std::dynamic_pointer_cast<const IGeometryLonLatMultiLine>(*parsed);
    ASSERT_NE(multi, nullptr);
    ASSERT_EQ(multi->count(), 2u);
    EXPECT_EQ(multi->item(0)->meta().elevation, (std::vector<double>{1.0, 2.0}));
    EXPECT_EQ(multi->item(1)->meta().elevation, (std::vector<double>{3.0, 4.0}));
}
