// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of the SML geometry blob (t_GeometryPointSML.pas, u_GeometryFromStreamSML.pas,
// u_GeometryToStreamSML.pas). Every blob is a literal byte vector: record = X (10 bytes) + Y (10 bytes) +
// Reserved (4 bytes), the values come from an independent script (see docs/handoffs/T-0109.md).
#include "cg/core/platform/qt_compat.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/marks/sml/geometry_from_stream_sml.h"
#include "cg/marks/sml/geometry_point_sml.h"
#include "cg/marks/sml/geometry_to_stream_sml.h"

#include <QByteArray>
#include <QDateTime>

#include <gtest/gtest.h>

#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace m = cg::marks;
namespace g = cg::geo;

namespace {

// ---- extended values (10 bytes each) ----
constexpr const char* kV10 = "00000000000000a00240";
constexpr const char* kV20 = "00000000000000a00340";
constexpr const char* kV30 = "00000000000000f00340";
constexpr const char* kV40 = "00000000000000a00440";
constexpr const char* kV50 = "00000000000000c80440";
constexpr const char* kV60 = "00000000000000f00440";
constexpr const char* kV2p5 = "00000000000000a00040";
constexpr const char* kVm3p5 = "00000000000000e000c0";
constexpr const char* kV12p5 = "00000000000000c80240";
constexpr const char* kVm45p25 = "00000000000000b504c0";
constexpr const char* kV4 = "00000000000000800140";
constexpr const char* kV180 = "00000000000000b40640";
constexpr const char* kV360 = "00000000000000b40740";
constexpr const char* kVm360 = "00000000000000b407c0";
constexpr const char* kV400 = "00000000000000c80740";
constexpr const char* kV0 = "00000000000000000000";
constexpr const char* kVm0 = "00000000000000000080";
constexpr const char* kV37p6173 = "008048bf7d1d78960440";
constexpr const char* kVm122p4194 = "0080e2c798bbd6f405c0";
constexpr const char* kV359p99999999 = "007086eaffffffb30740";
constexpr const char* kV5em324 = "0000000000000080cd3b";
constexpr const char* kNaN = "00000000000000c0ffff";
constexpr const char* kMinus1 = "0000000000000080ffbf";
constexpr const char* kPlusInf = "0000000000000080ff7f";
constexpr const char* kMinusInf = "0000000000000080ffff";
constexpr const char* kUnnormal = "0000000000000040ff3f"; // exponent 0x3FFF, integer bit clear
constexpr const char* kPseudoNaN = "0000000000000040ff7f";
constexpr const char* kPseudoDenormal = "00000000000000800000";
constexpr const char* kJustBelow360 =
    "ffffffffffffffb30740"; // 360 - 2^-55: below 360 as Extended, 360.0 as double
constexpr const char* kReserved0 = "00000000";

// one record
std::string rec(const char* x, const char* y, const char* reserved = kReserved0) {
    return std::string(x) + y + reserved;
}

struct Pt {
    const char* x;
    const char* y;
};
constexpr Pt kA{kV10, kV20};
constexpr Pt kB{kV30, kV40};
constexpr Pt kC{kV50, kV60};
constexpr Pt kD{kV2p5, kVm3p5};
constexpr Pt kE{kV12p5, kVm45p25};
constexpr Pt kF{kV4, kV180};
constexpr Pt kSep{kNaN, kNaN};
constexpr Pt kHole{kNaN, kMinus1};

std::string recs(std::initializer_list<Pt> pts) {
    std::string s;
    for (const Pt& p : pts) {
        s += rec(p.x, p.y);
    }
    return s;
}

QByteArray blob(const std::string& hex) {
    const QByteArray b = QByteArray::fromHex(QByteArray::fromStdString(hex));
    EXPECT_EQ(static_cast<std::size_t>(b.size()) * 2, hex.size()) << "odd or non-hex literal";
    return b;
}

// ---- describing a geometry as text, independent of the code under test ----
std::string num(double v) {
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, r.ptr);
}

std::string pts(std::span<const g::LonLat> p) {
    std::string s;
    for (const g::LonLat& v : p) {
        if (!s.empty()) {
            s += ';';
        }
        s += num(v.lon) + "," + num(v.lat);
    }
    return s;
}

std::string describeSingleLine(const g::IGeometryLonLatSingleLine& l) {
    return "L(" + pts(l.points()) + ")";
}

std::string describeSinglePolygon(const g::IGeometryLonLatSinglePolygon& p) {
    std::string s = "PG[O(" + pts(p.outerBorder()->points()) + ")";
    for (std::size_t i = 0; i < p.holeCount(); ++i) {
        s += "H(" + pts(p.holeBorder(i)->points()) + ")";
    }
    return s + "]";
}

std::string describe(const g::IGeometryLonLat* geometry) {
    if (geometry == nullptr) {
        return "null";
    }
    if (const auto* pt = dynamic_cast<const g::IGeometryLonLatPoint*>(geometry)) {
        return "P(" + num(pt->point().lon) + "," + num(pt->point().lat) + ")";
    }
    if (const auto* l = dynamic_cast<const g::IGeometryLonLatSingleLine*>(geometry)) {
        return describeSingleLine(*l);
    }
    if (const auto* ml = dynamic_cast<const g::IGeometryLonLatMultiLine*>(geometry)) {
        std::string s = "ML[";
        for (std::size_t i = 0; i < ml->count(); ++i) {
            if (i) {
                s += '|';
            }
            s += describeSingleLine(*ml->item(i));
        }
        return s + "]";
    }
    if (const auto* p = dynamic_cast<const g::IGeometryLonLatSinglePolygon*>(geometry)) {
        return describeSinglePolygon(*p);
    }
    if (const auto* mp = dynamic_cast<const g::IGeometryLonLatMultiPolygon*>(geometry)) {
        std::string s = "MP[";
        for (std::size_t i = 0; i < mp->count(); ++i) {
            if (i) {
                s += '|';
            }
            s += describeSinglePolygon(*mp->item(i));
        }
        return s + "]";
    }
    return "other";
}

g::GeometryLonLatPtr parse(const QByteArray& b, const g::PointsMeta* meta = nullptr) {
    const g::GeometryLonLatFactory factory;
    return m::parseGeometrySml(b, factory, meta);
}

using V = std::vector<g::LonLat>;
g::LonLatPointsPtr ptr(V v) {
    return std::make_shared<const V>(std::move(v));
}
const g::LonLat gA{10, 20}, gB{30, 40}, gC{50, 60}, gD{2.5, -3.5}, gE{12.5, -45.25}, gF{4, 180};

} // namespace

// u_GeometryFromStreamSML.pas:75-352 -- one row per reader rule. `canonical` rows are the shape the writer
// produces, so writing the parsed geometry must give the same bytes back.
TEST(GeometrySmlParse, BlobTable) {
    struct Row {
        const char* name;
        std::string hex;
        const char* expect;
        bool canonical;
    };
    const std::string tail23(46, 'f'); // 23 garbage bytes
    const std::vector<Row> rows = {
        // :337-351 sizes
        {"empty blob", "", "null", false},
        {"22 bytes", rec(kV10, kV20).substr(0, 44), "null", false},
        {"23 bytes", rec(kV10, kV20).substr(0, 46), "null", false},
        // single record: :339-343 and SMLPointToDoublePoint :75-90
        {"point", rec(kV37p6173, kVm122p4194), "P(37.6173,-122.4194)", true},
        {"point + 23 trailing bytes ignored", rec(kV10, kV20) + tail23, "P(10,20)", false},
        {"point, Reserved ignored", rec(kV10, kV20, "deadbeef"), "P(10,20)", false},
        {"point, X NaN", rec(kNaN, kV20), "null", false},
        {"point, Y NaN", rec(kV10, kNaN), "null", false},
        {"point, X = 360", rec(kV360, kV20), "null", false},
        {"point, X = -360", rec(kVm360, kV20), "null", false},
        {"point, Y = 360", rec(kV10, kV360), "null", false},
        {"point, Y = -360", rec(kV10, kVm360), "null", false},
        {"point, Y = 359.99999999", rec(kV10, kV359p99999999), "P(10,359.99999999)", true},
        {"point, X +inf", rec(kPlusInf, kV20), "null", false},
        {"point, Y -inf", rec(kV10, kMinusInf), "null", false},
        {"point, X unnormal", rec(kUnnormal, kV20), "null", false},
        {"point, subnormal double", rec(kV5em324, kV0), "P(5e-324,0)", true},
        {"point, pseudo-denormal X reads as 0", rec(kPseudoDenormal, kV20), "P(0,20)", false},
        {"point, -0 reads as -0", rec(kVm0, kV20), "P(-0,20)", true},
        {"point, range is tested on the 80-bit value", rec(kJustBelow360, kV20), "P(360,20)", false},
        // :187-195 and ParseLine :209-276
        {"line + terminator", recs({kA, kB, kC, kSep}), "L(10,20;30,40;50,60)", true},
        {"line without terminator keeps the last record", recs({kA, kB}), "L(10,20;30,40)", false},
        {"line of one vertex", recs({kA, kSep}), "L(10,20)", true},
        {"line + terminator + 5 trailing bytes", recs({kA, kB, kSep}) + "ffffffffff", "L(10,20;30,40)",
         false},
        {"multi-line", recs({kA, kB, kSep, kC, kD, kSep}), "ML[L(10,20;30,40)|L(50,60;2.5,-3.5)]", true},
        {"two separators in a row", recs({kA, kSep, kSep, kB, kC, kSep}), "ML[L(10,20)|L(30,40;50,60)]",
         false},
        {"leading separator", recs({kSep, kA, kB}), "L(10,20;30,40)", false},
        {"hole marker inside a line is a separator", recs({kA, kHole, kB, kSep}), "ML[L(10,20)|L(30,40)]",
         false},
        {"X valid, Y NaN inside a line is a separator",
         rec(kV10, kV20) + rec(kV30, kNaN) + rec(kV50, kV60) + rec(kNaN, kNaN), "ML[L(10,20)|L(50,60)]",
         false},
        {"out-of-range record inside a line is a separator",
         rec(kV10, kV20) + rec(kV360, kV10) + rec(kV30, kV40) + rec(kNaN, kNaN), "ML[L(10,20)|L(30,40)]",
         false},
        {"unnormal inside a line is a separator",
         rec(kV10, kV20) + rec(kUnnormal, kV10) + rec(kV30, kV40) + rec(kNaN, kNaN), "ML[L(10,20)|L(30,40)]",
         false},
        // D row: legacy raises out of Parse at :157 when the FIRST or LAST record is an unsupported encoding;
        // the port folds it into "empty"
        {"unnormal first, real last: line, first skipped", rec(kUnnormal, kV10) + recs({kB, kA}),
         "L(30,40;10,20)", false},
        {"unnormal last, real first: line, last dropped as empty", recs({kA, kB}) + rec(kUnnormal, kV10),
         "L(10,20;30,40)", false},
        {"unnormal first and last: both empty, polygon",
         rec(kUnnormal, kV10) + recs({kA, kB}) + rec(kV10, kPlusInf) + rec(kPseudoNaN, kV10),
         "PG[O(10,20;30,40)]", false},
        // IsPolygonInStream :164-196 and ParsePolygon :278-324
        {"polygon, writer shape (contour closed + closing record)", recs({kA, kB, kC, kA, kA}),
         "PG[O(10,20;30,40;50,60)]", true},
        {"polygon, one closing vertex only", recs({kA, kB, kC, kA}), "PG[O(10,20;30,40;50,60)]", false},
        {"polygon with a hole", recs({kA, kB, kC, kA, kHole, kD, kE, kF, kD, kA}),
         "PG[O(10,20;30,40;50,60)H(2.5,-3.5;12.5,-45.25;4,180)]", true},
        {"multi-polygon", recs({kA, kB, kC, kA, kSep, kD, kE, kF, kD, kA}),
         "MP[PG[O(10,20;30,40;50,60)]|PG[O(2.5,-3.5;12.5,-45.25;4,180)]]", true},
        {"hole marker, then a separator with an empty ring: the next ring is still a hole (:302-309)",
         recs({kA, kB, kC, kA, kHole, kSep, kD, kE, kF, kD, kA}),
         "PG[O(10,20;30,40;50,60)H(2.5,-3.5;12.5,-45.25;4,180)]", false},
        {"out-of-range record starts a new outer",
         recs({kA, kB, kC}) + rec(kV400, kV10) + recs({kD, kE, kF, kA}),
         "MP[PG[O(10,20;30,40;50,60)]|PG[O(2.5,-3.5;12.5,-45.25;4,180)]]", false},
        {"both ends empty count as equal: polygon", recs({kSep, kA, kB, kSep}), "PG[O(10,20;30,40)]", false},
        {"two records, first = last: a ring of one vertex", recs({kA, kA}), "PG[O(10,20)]", true},
        {"+0 equals -0 in the closing test", rec(kV0, kV10) + rec(kV30, kV40) + rec(kVm0, kV10),
         "PG[O(0,10;30,40)]", false},
        {"first and last differ in Y only: line", recs({kA, kB, {kV10, kV60}}), "L(10,20;30,40;10,60)",
         false},
        {"all records empty: polygon without rings", recs({kSep, kSep, kSep}), "null", false},
        {"only invalid records and a different valid last: line", recs({kSep, kHole, kA}), "L(10,20)", false},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        const QByteArray b = blob(r.hex);
        const g::GeometryLonLatPtr geometry = parse(b);
        EXPECT_EQ(describe(geometry.get()), r.expect);
        if (r.canonical) {
            EXPECT_EQ(m::saveGeometrySml(geometry.get()), b) << "blob -> geometry -> blob";
        }
    }
}

// :228-232, :249-261: the meta entry of RAW record index I goes to the vertex of record I; the arrays have
// a slot for every separator record. Used only when the entry count equals the record count after the
// terminator was dropped.
TEST(GeometrySmlParse, MetaIndexedByRawRecord) {
    // records: A B sep C terminator -> 4 records after the terminator is dropped
    const QByteArray b = blob(recs({kA, kB, kSep, kC, kSep}));
    g::PointsMeta meta;
    meta.elevation = {1.5, 2.5, -9.0, 4.5};
    meta.timestamp = {
        cg::core::platform::utcFromSecsSinceEpoch(100), cg::core::platform::utcFromSecsSinceEpoch(200),
        cg::core::platform::utcFromSecsSinceEpoch(300), cg::core::platform::utcFromSecsSinceEpoch(400)};
    const g::GeometryLonLatPtr geometry = parse(b, &meta);
    const auto* multi = dynamic_cast<const g::IGeometryLonLatMultiLine*>(geometry.get());
    ASSERT_NE(multi, nullptr);
    ASSERT_EQ(multi->count(), 2u);
    const g::PointsMeta& first = multi->item(0)->meta();
    EXPECT_EQ(first.elevation, (std::vector<double>{1.5, 2.5}));
    ASSERT_EQ(first.timestamp.size(), 2u);
    EXPECT_EQ(first.timestamp[1].toSecsSinceEpoch(), 200);
    const g::PointsMeta& second = multi->item(1)->meta();
    EXPECT_EQ(second.elevation, (std::vector<double>{4.5})); // raw index 3, not 2
    ASSERT_EQ(second.timestamp.size(), 1u);
    EXPECT_EQ(second.timestamp[0].toSecsSinceEpoch(), 400);

    // elevation only
    g::PointsMeta onlyElevation;
    onlyElevation.elevation = {1.5, 2.5, -9.0, 4.5};
    const g::GeometryLonLatPtr keep = parse(b, &onlyElevation);
    const auto* m3 = dynamic_cast<const g::IGeometryLonLatMultiLine*>(keep.get());
    ASSERT_NE(m3, nullptr);
    EXPECT_TRUE(m3->item(1)->meta().hasElevation());
    EXPECT_FALSE(m3->item(1)->meta().hasTimestamp());
}

TEST(GeometrySmlParse, MetaIgnoredWhenCountDiffers) {
    const QByteArray b = blob(recs({kA, kB, kSep, kC, kSep}));
    for (const std::size_t n : {std::size_t{3}, std::size_t{5}}) { // 5 = counting the terminator record
        SCOPED_TRACE(n);
        g::PointsMeta meta;
        meta.elevation.assign(n, 7.0);
        const g::GeometryLonLatPtr geometry = parse(b, &meta);
        const auto* multi = dynamic_cast<const g::IGeometryLonLatMultiLine*>(geometry.get());
        ASSERT_NE(multi, nullptr);
        EXPECT_FALSE(multi->item(0)->meta().hasElevation());
        EXPECT_FALSE(multi->item(1)->meta().hasElevation());
    }
    // a polygon never takes meta
    g::PointsMeta meta;
    meta.elevation.assign(3, 7.0);
    const g::GeometryLonLatPtr polygon = parse(blob(recs({kA, kB, kC, kA})), &meta);
    EXPECT_EQ(describe(polygon.get()), "PG[O(10,20;30,40;50,60)]");
}

namespace {

struct WriteRow {
    const char* name;
    g::GeometryLonLatPtr geometry;
    std::string hex;
};

g::GeometryLonLatPtr line(std::initializer_list<V> parts) {
    const g::GeometryLonLatFactory f;
    g::GeometryLonLatLineBuilder b = f.makeLineBuilder();
    for (const V& p : parts) {
        b.addLine(ptr(p));
    }
    return b.makeStaticAndClear();
}

struct PolyDef {
    V outer;
    std::vector<V> holes;
};

g::GeometryLonLatPtr polygon(std::initializer_list<PolyDef> defs) {
    const g::GeometryLonLatFactory f;
    g::GeometryLonLatPolygonBuilder b = f.makePolygonBuilder();
    for (const PolyDef& d : defs) {
        b.addOuter(ptr(d.outer));
        for (const V& h : d.holes) {
            b.addHole(ptr(h));
        }
    }
    return b.makeStaticAndClear();
}

} // namespace

// u_GeometryToStreamSML.pas:103-292 -- the writer, one row per geometry kind, and the round trip back.
TEST(GeometrySmlSave, GeometryTable) {
    const g::GeometryLonLatFactory f;
    const std::vector<WriteRow> rows = {
        {"point", f.createPoint({37.6173, -122.4194}), rec(kV37p6173, kVm122p4194)},
        {"single line: vertices + one terminator", line({{gA, gB, gC}}), recs({kA, kB, kC, kSep})},
        {"multi-line: separator between parts, one terminator", line({{gA, gB}, {gC, gD}}),
         recs({kA, kB, kSep, kC, kD, kSep})},
        {"polygon: contour closed by its own first vertex, then the closing record",
         polygon({{{gA, gB, gC}, {}}}), recs({kA, kB, kC, kA, kA})},
        {"polygon with a hole", polygon({{{gA, gB, gC}, {{gD, gE, gF}}}}),
         recs({kA, kB, kC, kA, kHole, kD, kE, kF, kD, kA})},
        {"multi-polygon with a hole in the second: closing record is the first vertex of polygon 0",
         polygon({{{gA, gB, gC}, {}}, {{gD, gE, gF}, {{gE, gD, gF}}}}),
         recs({kA, kB, kC, kA, kSep, kD, kE, kF, kD, kHole, kE, kD, kF, kE, kA})},
        {"polygon with a one-vertex contour: no repeat for count 1", polygon({{{gA}, {}}}), recs({kA, kA})},
        {"polygon with two holes", polygon({{{gA, gB, gC}, {{gD, gE}, {gF, gD, gE}}}}),
         recs({kA, kB, kC, kA, kHole, kD, kE, kD, kHole, kF, kD, kE, kF, kA})},
    };
    for (const WriteRow& r : rows) {
        SCOPED_TRACE(r.name);
        ASSERT_NE(r.geometry, nullptr);
        const QByteArray written = m::saveGeometrySml(r.geometry.get());
        EXPECT_EQ(written, blob(r.hex));
        // geometry -> blob -> geometry
        EXPECT_EQ(describe(parse(written).get()), describe(r.geometry.get()));
    }
}

// :114-122 and the Assert(False) branches (assertions are off in the shipped build): nothing is written.
TEST(GeometrySmlSave, NothingWrittenForUnsupportedOrEmpty) {
    const g::GeometryLonLatFactory f;
    EXPECT_TRUE(m::saveGeometrySml(nullptr).isEmpty());
    const std::vector<g::LonLat> two = {gA, gB};
    EXPECT_TRUE(
        m::saveGeometrySml(f.createMultiPoint(two).get()).isEmpty()); // neither point, line nor polygon
    // a bare contour is a polygon in the interface tree but neither single nor multi (:203-209)
    const g::GeometryLonLatContour contour(0, g::LonLatRect{}, ptr({gA, gB, gC}));
    EXPECT_TRUE(m::saveGeometrySml(&contour).isEmpty());
}

// A coordinate that is NaN in a point geometry is widened, not replaced by the fixed pattern: the sign and
// payload of the double are kept (u_GeometryToStreamSML.pas:96-97 assigns a Double to an Extended).
TEST(GeometrySmlSave, NaNCoordinateIsWidenedNotReplaced) {
    const g::GeometryLonLatFactory f;
    const auto pointNaN = f.createPoint({std::numeric_limits<double>::quiet_NaN(), 20.0});
    EXPECT_EQ(m::saveGeometrySml(pointNaN.get()), blob(rec("00000000000000c0ff7f", kV20)));
}

namespace {

// Test stubs: the geo interfaces are abstract, so states the landed constructors only Q_ASSERT against can
// be reached (debug and release alike).
class StubContour final : public g::IGeometryLonLatContour {
public:
    explicit StubContour(std::vector<g::LonLat> pts) : pts_(std::move(pts)) {}
    cg::core::HashValue hash() const noexcept override { return 0; }
    g::LonLatRect bounds() const noexcept override { return {}; }
    g::LonLat goToPoint() const override { return {}; }
    bool isSameGeometry(const g::IGeometryLonLat&) const override { return false; }
    std::span<const g::LonLat> points() const noexcept override { return pts_; }

private:
    std::vector<g::LonLat> pts_;
};

class StubMultiLine final : public g::IGeometryLonLatMultiLine {
public:
    cg::core::HashValue hash() const noexcept override { return 0; }
    g::LonLatRect bounds() const noexcept override { return {}; }
    g::LonLat goToPoint() const override { return {}; }
    bool isSameGeometry(const g::IGeometryLonLat&) const override { return false; }
    std::size_t count() const noexcept override { return 0; }
    const g::GeometryLonLatSingleLinePtr& item(std::size_t) const override { return none_; }

private:
    g::GeometryLonLatSingleLinePtr none_;
};

class StubMultiPolygon final : public g::IGeometryLonLatMultiPolygon {
public:
    cg::core::HashValue hash() const noexcept override { return 0; }
    g::LonLatRect bounds() const noexcept override { return {}; }
    g::LonLat goToPoint() const override { return {}; }
    bool isSameGeometry(const g::IGeometryLonLat&) const override { return false; }
    std::size_t count() const noexcept override { return 0; }
    const g::GeometryLonLatSinglePolygonPtr& item(std::size_t) const override { return none_; }

private:
    g::GeometryLonLatSinglePolygonPtr none_;
};

class StubSinglePolygon final : public g::IGeometryLonLatSinglePolygon {
public:
    StubSinglePolygon(g::GeometryLonLatContourPtr outer, std::vector<g::GeometryLonLatContourPtr> holes)
        : outer_(std::move(outer)), holes_(std::move(holes)) {}
    cg::core::HashValue hash() const noexcept override { return 0; }
    g::LonLatRect bounds() const noexcept override { return {}; }
    g::LonLat goToPoint() const override { return {}; }
    bool isSameGeometry(const g::IGeometryLonLat&) const override { return false; }
    const g::GeometryLonLatContourPtr& outerBorder() const override { return outer_; }
    std::size_t holeCount() const noexcept override { return holes_.size(); }
    const g::GeometryLonLatContourPtr& holeBorder(std::size_t i) const override { return holes_[i]; }

private:
    g::GeometryLonLatContourPtr outer_;
    std::vector<g::GeometryLonLatContourPtr> holes_;
};

} // namespace

// D row regression (guards in geometry_to_stream_sml.cpp): empty multi-geometries and a contour without
// vertices. Legacy: Item[0] of an empty TList (exception, RTL not in the tree) and a closing record built
// from an uninitialised variable. Port: nothing written / no closing record.
TEST(GeometrySmlSave, DegenerateGeometryStubs) {
    const StubMultiLine noLines;
    const StubMultiPolygon noPolygons;
    EXPECT_TRUE(m::saveGeometrySml(&noLines).isEmpty());
    EXPECT_TRUE(m::saveGeometrySml(&noPolygons).isEmpty());
    const auto emptyOuter = std::make_shared<const StubContour>(std::vector<g::LonLat>{});
    const auto hole = std::make_shared<const StubContour>(std::vector<g::LonLat>{gD, gE, gF});
    // no vertex at all: nothing
    const StubSinglePolygon nothing(emptyOuter, {});
    EXPECT_TRUE(m::saveGeometrySml(&nothing).isEmpty());
    // empty outer + a hole: marker and hole only, no closing record
    const StubSinglePolygon holeOnly(emptyOuter, {hole});
    EXPECT_EQ(m::saveGeometrySml(&holeOnly), blob(recs({kHole, kD, kE, kF, kD})));
    // empty hole after a real outer: the marker is written, the closing record is the outer's first vertex
    const auto outer = std::make_shared<const StubContour>(std::vector<g::LonLat>{gA, gB, gC});
    const StubSinglePolygon emptyHole(outer, {emptyOuter});
    EXPECT_EQ(m::saveGeometrySml(&emptyHole), blob(recs({kA, kB, kC, kA, kHole, kA})));
}
