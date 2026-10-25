// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/wkb.h"

#include "cg/core/platform/qt_compat.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace cg::geo {
namespace {

// u_GeometryFromWKB.pas / u_GeometryToWKB.pas type codes. Rule 1: 4 (MultiPoint) and 7
// (GeometryCollection) are neither read nor written.
constexpr quint32 kWkbPoint = 1;
constexpr quint32 kWkbLine = 2;
constexpr quint32 kWkbPolygon = 3;
constexpr quint32 kWkbMultiLine = 5;
constexpr quint32 kWkbMultiPolygon = 6;

// Internal control-flow signal only -- never escapes parseWkb/parseMetaJson. Legacy signals a
// malformed blob with a silent Delphi `Abort`; this is the C++ analog, caught once at the public
// entry point and turned into a core::Result (docs/specs/geo.md §3.7 "Errors").
struct WkbParseError {};

// A bounds-checked little-endian cursor over the WKB blob.
class WkbReader {
public:
    explicit WkbReader(QByteArrayView data) noexcept : data_(data) {}

    template <class T> [[nodiscard]] T read() {
        static_assert(std::is_trivially_copyable_v<T>);
        if (pos_ + sizeof(T) > static_cast<std::size_t>(data_.size())) {
            throw WkbParseError{};
        }
        T value{};
        std::memcpy(&value, data_.constData() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return value;
    }

    // Rule 2: little-endian only, a non-1 byte-order byte aborts the parse.
    void checkByteOrder() {
        if (read<quint8>() != 1) {
            throw WkbParseError{};
        }
    }

    // Rule 4, vertex counts: count < INT_MAX / 2 / sizeof(double).
    [[nodiscard]] quint32 readVertexCount() {
        const quint32 count = read<quint32>();
        constexpr quint32 kMax = static_cast<quint32>(std::numeric_limits<int>::max()) / 2 / sizeof(double);
        if (count >= kMax) {
            throw WkbParseError{};
        }
        return count;
    }

    // Rule 4, element counts: count < INT_MAX / (1 + 4 + 4 + 2 * sizeof(double)) -- each element
    // carries its own byte-order + type header (rule 7).
    [[nodiscard]] quint32 readElementCount() {
        const quint32 count = read<quint32>();
        constexpr quint32 kMax =
            static_cast<quint32>(std::numeric_limits<int>::max()) / (1 + 4 + 4 + 2 * sizeof(double));
        if (count >= kMax) {
            throw WkbParseError{};
        }
        return count;
    }

    // Rule 3: XY only, a whole ring/line is one read(count * 16). BLOCKER fix (review F1): bounds-
    // check the declared count against the remaining bytes BEFORE allocating -- `count` is only
    // bounded by the rule-4 guard (~134M), so a corrupt blob declaring a huge count must not be
    // able to force a multi-GB allocation / std::bad_alloc out of parseWkb (wkb.h:24-27 "a
    // corrupt blob must be skippable, not fatal").
    [[nodiscard]] LonLatPointsPtr readPoints(quint32 count) {
        if (count == 0) {
            return std::make_shared<std::vector<LonLat>>();
        }
        const auto bytes = static_cast<std::size_t>(count) * sizeof(LonLat);
        if (pos_ + bytes > static_cast<std::size_t>(data_.size())) {
            throw WkbParseError{};
        }
        auto points = std::make_shared<std::vector<LonLat>>(count);
        std::memcpy(points->data(), data_.constData() + pos_, bytes);
        pos_ += bytes;
        return points;
    }

private:
    QByteArrayView data_;
    std::size_t pos_ = 0;
};

[[nodiscard]] LonLatPointsPtr loadSingleLinePoints(WkbReader& r) {
    // u_GeometryFromWKB.pas LoadSingleLine / LoadSinglePolygon -- identical shape, neither ring
    // nor line carries its own byte-order/type header (only MultiLine/MultiPolygon elements do).
    return r.readPoints(r.readVertexCount());
}

// Fits `meta` (the whole flattened-stream sidecar) to one line's [startIndex, startIndex+count)
// window, silently dropping it if it does not fit -- u_GeometryFromWKB.pas:141-146's
// `Assert(False)` branch, which is a no-op in a release build (assertions compiled out), so
// legacy's actual behavior there is "drop the meta for this segment", not "crash".
[[nodiscard]] PointsMeta sliceMetaIfFits(const PointsMeta* meta, std::size_t startIndex, std::size_t count) {
    if (meta == nullptr) {
        return {};
    }
    const std::size_t metaLen = meta->hasElevation()   ? meta->elevation.size()
                                : meta->hasTimestamp() ? meta->timestamp.size()
                                                       : 0;
    if (metaLen == 0 || metaLen < startIndex + count) {
        return {};
    }
    return meta->slice(startIndex, count);
}

[[nodiscard]] GeometryLonLatPtr loadLine(const GeometryLonLatFactory& factory, WkbReader& r,
                                         const PointsMeta* meta) {
    auto builder = factory.makeLineBuilder();
    LonLatPointsPtr points = loadSingleLinePoints(r);
    builder.addLine(points, sliceMetaIfFits(meta, 0, points->size()));
    return builder.makeStaticAndClear();
}

[[nodiscard]] GeometryLonLatPtr loadMultiLine(const GeometryLonLatFactory& factory, WkbReader& r,
                                              const PointsMeta* meta) {
    auto builder = factory.makeLineBuilder();
    const quint32 count = r.readElementCount();
    std::size_t metaIndex = 0;
    for (quint32 i = 0; i < count; ++i) {
        r.checkByteOrder();
        if (r.read<quint32>() != kWkbLine) {
            throw WkbParseError{};
        }
        LonLatPointsPtr points = loadSingleLinePoints(r);
        builder.addLine(points, sliceMetaIfFits(meta, metaIndex, points->size()));
        metaIndex += points->size() + 1; // rule 9: skip the separation point
    }
    return builder.makeStaticAndClear();
}

void loadPolygonRings(GeometryLonLatPolygonBuilder& builder, WkbReader& r) {
    // Rule 6: first ring is the outer, the rest are holes, positionally.
    const quint32 count = r.readElementCount();
    for (quint32 i = 0; i < count; ++i) {
        LonLatPointsPtr ring = loadSingleLinePoints(r);
        if (i == 0) {
            builder.addOuter(ring);
        } else {
            builder.addHole(ring);
        }
    }
}

[[nodiscard]] GeometryLonLatPtr loadPolygon(const GeometryLonLatFactory& factory, WkbReader& r) {
    auto builder = factory.makePolygonBuilder();
    loadPolygonRings(builder, r);
    return builder.makeStaticAndClear();
}

[[nodiscard]] GeometryLonLatPtr loadMultiPolygon(const GeometryLonLatFactory& factory, WkbReader& r) {
    auto builder = factory.makePolygonBuilder();
    const quint32 count = r.readElementCount();
    for (quint32 i = 0; i < count; ++i) {
        r.checkByteOrder();
        if (r.read<quint32>() != kWkbPolygon) {
            throw WkbParseError{};
        }
        loadPolygonRings(builder, r);
    }
    return builder.makeStaticAndClear();
}

[[nodiscard]] GeometryLonLatPtr parseWkbBody(const GeometryLonLatFactory& factory, WkbReader& r,
                                             const PointsMeta* meta) {
    r.checkByteOrder();
    const quint32 type = r.read<quint32>();
    switch (type) {
    case kWkbPoint:
        return factory.createPoint(r.read<LonLat>());
    case kWkbLine:
        return loadLine(factory, r, meta);
    case kWkbPolygon:
        return loadPolygon(factory, r);
    case kWkbMultiLine:
        return loadMultiLine(factory, r, meta);
    case kWkbMultiPolygon:
        return loadMultiPolygon(factory, r);
    default:
        throw WkbParseError{}; // reserved/unsupported type code (rule 1) or garbage
    }
}

// --- writer ------------------------------------------------------------------------------------

void appendU32(QByteArray& out, quint32 v) {
    out.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

void appendPoints(QByteArray& out, std::span<const LonLat> points) {
    out.append(reinterpret_cast<const char*>(points.data()),
               static_cast<qsizetype>(points.size()) * static_cast<qsizetype>(sizeof(LonLat)));
}

void writePoint(QByteArray& out, const IGeometryLonLatPoint& g) {
    appendU32(out, kWkbPoint);
    const LonLat p = g.point();
    out.append(reinterpret_cast<const char*>(&p), sizeof(p));
}

void writeLine(QByteArray& out, const IGeometryLonLatSingleLine& g) {
    appendU32(out, kWkbLine);
    appendU32(out, static_cast<quint32>(g.points().size()));
    appendPoints(out, g.points());
}

void writeContour(QByteArray& out, const IGeometryLonLatContour& g) {
    // Rule 5: SaveContour writes count+1 and re-emits points[0] -- the ring is written CLOSED
    // even though it is stored open. A write->read round-trip therefore grows every ring by one.
    // u_GeometryToWKB.pas:195-206 (SaveContour) writes unconditionally, with no empty-ring guard;
    // it can get away with that because TGeometryLonLatBase.Create asserts Count > 0
    // (u_GeometryLonLat.pas:176), exactly like GeometryLonLatContour's own constructor
    // (geometry_lonlat.cpp) -- so an empty ring here is unreachable, not a case to special-case
    // into a malformed (count-claimed-but-unwritten) blob. Match legacy: no branch.
    const auto pts = g.points();
    Q_ASSERT(!pts.empty());
    appendU32(out, static_cast<quint32>(pts.size() + 1));
    appendPoints(out, pts);
    appendPoints(out, pts.subspan(0, 1));
}

void writePolygon(QByteArray& out, const IGeometryLonLatSinglePolygon& g) {
    appendU32(out, kWkbPolygon);
    appendU32(out, static_cast<quint32>(1 + g.holeCount()));
    writeContour(out, *g.outerBorder());
    for (std::size_t i = 0; i < g.holeCount(); ++i) {
        writeContour(out, *g.holeBorder(i));
    }
}

void writeMultiLine(QByteArray& out, const IGeometryLonLatMultiLine& g) {
    appendU32(out, kWkbMultiLine);
    appendU32(out, static_cast<quint32>(g.count()));
    for (std::size_t i = 0; i < g.count(); ++i) {
        out.append(char(1)); // nested byte-order header (rule 7)
        writeLine(out, *g.item(i));
    }
}

void writeMultiPolygon(QByteArray& out, const IGeometryLonLatMultiPolygon& g) {
    appendU32(out, kWkbMultiPolygon);
    appendU32(out, static_cast<quint32>(g.count()));
    for (std::size_t i = 0; i < g.count(); ++i) {
        out.append(char(1));
        writePolygon(out, *g.item(i));
    }
}

// --- JSON meta sidecar ---------------------------------------------------------------------

// TDateTime <-> QDateTime. Legacy stores GPX timestamps as a Delphi TDateTime double (whole part
// = days since 1899-12-30, i.e. Unix epoch == 25569.0) inside the base64 "d" payload; reproduced
// so a marks database written by legacy round-trips through this port unchanged.
constexpr double kDelphiEpochOffsetDays = 25569.0;
constexpr double kMsPerDay = 86400000.0;

[[nodiscard]] double toDelphiDateTime(const QDateTime& dt) noexcept {
    if (!dt.isValid()) {
        return 0.0;
    }
    return static_cast<double>(dt.toUTC().toMSecsSinceEpoch()) / kMsPerDay + kDelphiEpochOffsetDays;
}

[[nodiscard]] QDateTime fromDelphiDateTime(double days) noexcept {
    const double msecs = (days - kDelphiEpochOffsetDays) * kMsPerDay;
    return core::platform::utcFromMSecsSinceEpoch(static_cast<qint64>(std::llround(msecs)));
}

[[nodiscard]] QString toBase64(std::span<const double> values) {
    const auto bytes = static_cast<qsizetype>(values.size()) * static_cast<qsizetype>(sizeof(double));
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(values.data()), bytes).toBase64());
}

[[nodiscard]] std::vector<const IGeometryLonLatSingleLine*> flattenLines(const IGeometryLonLat& geometry) {
    // GeometryLonLatLineToArray (u_GeometryFunc.pas:1153-1177).
    std::vector<const IGeometryLonLatSingleLine*> lines;
    if (const auto* single = dynamic_cast<const IGeometryLonLatSingleLine*>(&geometry)) {
        lines.push_back(single);
    } else if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiLine*>(&geometry)) {
        lines.reserve(multi->count());
        for (std::size_t i = 0; i < multi->count(); ++i) {
            lines.push_back(multi->item(i).get());
        }
    }
    return lines;
}

// Appends one WriteItem (u_GeometryMetaToStreamJson.pas:91-98): `{"t":1,"n":"<tag>","d":"<base64>"}`.
void appendMetaItem(QByteArray& out, QLatin1String tag, const QString& base64) {
    out += "{\"t\":1,\"n\":\"";
    out += tag;
    out += "\",\"d\":\"";
    out += base64.toLatin1();
    out += "\"}";
}

} // namespace

core::Result<GeometryLonLatPtr> parseWkb(QByteArrayView wkb, const GeometryLonLatFactory& factory,
                                         const PointsMeta* meta) {
    try {
        WkbReader r(wkb);
        GeometryLonLatPtr result = parseWkbBody(factory, r, meta);
        if (!result) {
            return core::fail(core::ErrorCode::ParseError, QStringLiteral("wkb: empty geometry"));
        }
        return result;
    } catch (const WkbParseError&) {
        return core::fail(core::ErrorCode::ParseError, QStringLiteral("wkb: malformed blob"));
    }
}

QByteArray writeWkb(const IGeometryLonLat& geometry) {
    QByteArray out;
    out.append(char(1)); // byte-order, always little-endian (rule 2)
    if (const auto* point = dynamic_cast<const IGeometryLonLatPoint*>(&geometry)) {
        writePoint(out, *point);
    } else if (const auto* line = dynamic_cast<const IGeometryLonLatSingleLine*>(&geometry)) {
        writeLine(out, *line);
    } else if (const auto* polygon = dynamic_cast<const IGeometryLonLatSinglePolygon*>(&geometry)) {
        writePolygon(out, *polygon);
    } else if (const auto* multiLine = dynamic_cast<const IGeometryLonLatMultiLine*>(&geometry)) {
        Q_ASSERT(multiLine->count() > 0);
        if (multiLine->count() == 1) { // rule 8: a count==1 multi collapses to the plain type
            writeLine(out, *multiLine->item(0));
        } else {
            writeMultiLine(out, *multiLine);
        }
    } else if (const auto* multiPolygon = dynamic_cast<const IGeometryLonLatMultiPolygon*>(&geometry)) {
        Q_ASSERT(multiPolygon->count() > 0);
        if (multiPolygon->count() == 1) {
            writePolygon(out, *multiPolygon->item(0));
        } else {
            writeMultiPolygon(out, *multiPolygon);
        }
    } else {
        Q_ASSERT(false); // u_GeometryToWKB.pas:107 -- unreachable IGeometryLonLat concrete type
    }
    return out;
}

QByteArray writeMetaJson(const IGeometryLonLat& geometry) {
    // Points and polygons never carry meta (u_GeometryMetaToStreamJson.pas Save).
    const std::vector<const IGeometryLonLatSingleLine*> lines = flattenLines(geometry);
    const bool hasAny = std::any_of(lines.begin(), lines.end(), [](const auto* l) {
        return l->meta().hasElevation() || l->meta().hasTimestamp();
    });
    if (!hasAny) {
        return {};
    }

    // Built by hand rather than QJsonDocument::toJson(): QJsonObject always serializes keys in
    // sorted order, which would reorder root {"v","t","g"} to {"g","t","v"} (only "c"/"m" and
    // "t"/"n"/"d" survive alphabetization unscathed). Tech Lead ruling (review F4): reproduce
    // TGeometryMetaToStreamJson.DoSaveLines (u_GeometryMetaToStreamJson.pas:100-174) byte-for-byte,
    // including its exact key order, not merely a JSON-equivalent reordering.
    QByteArray out("JSON");
    out += "{\"v\":1,\"t\":1,\"g\":[";
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        const IGeometryLonLatSingleLine& line = *lines[i];
        const PointsMeta& m = line.meta();
        out += "{\"c\":";
        out += QByteArray::number(static_cast<qint64>(line.points().size()));
        out += ",\"m\":[";
        if (m.hasElevation()) {
            appendMetaItem(out, QLatin1String("ele"), toBase64(m.elevation));
        }
        if (m.hasTimestamp()) {
            if (m.hasElevation()) {
                out += ',';
            }
            std::vector<double> days(m.timestamp.size());
            std::transform(m.timestamp.begin(), m.timestamp.end(), days.begin(), toDelphiDateTime);
            appendMetaItem(out, QLatin1String("time"), toBase64(days));
        }
        out += "]}";
    }
    out += "]}";
    return out;
}

core::Result<PointsMeta> parseMetaJson(QByteArrayView data) {
    if (data.isEmpty()) {
        return PointsMeta{}; // "" -> "no meta", not an error
    }
    if (data.size() <= 4 || data.first(4) != QByteArrayView("JSON", 4)) {
        return PointsMeta{}; // IsMagicOk false in legacy -> Parse returns nil, not an error either
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data.sliced(4).toByteArray(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::fail(core::ErrorCode::ParseError, QStringLiteral("meta-json: invalid JSON"));
    }
    const QJsonObject root = doc.object();
    // Tech Lead ruling (review F5): an unknown "v"/"t" is legacy's Assert(False); Exit --
    // Result stays nil, i.e. "no meta", not a parse error (u_GeometryMetaFromStreamJson.pas:
    // 103-112). Only a "JSON"-prefixed payload that fails to parse as JSON at all, or omits "g",
    // is a real error (spec §11.2 MalformedPayloadReturnsError).
    if (root.value(QLatin1String("v")).toInt(-1) != 1) {
        return PointsMeta{};
    }
    if (root.value(QLatin1String("t")).toInt(-1) != 1) { // jgLine -- the only type this port writes
        return PointsMeta{};
    }
    const QJsonValue geomsValue = root.value(QLatin1String("g"));
    if (!geomsValue.isArray()) {
        return core::fail(core::ErrorCode::ParseError, QStringLiteral("meta-json: missing geometry array"));
    }
    const QJsonArray geoms = geomsValue.toArray();

    struct Segment {
        std::size_t count = 0;
        std::vector<double> ele;
        std::vector<double> days;
    };
    std::vector<Segment> segments;
    for (qsizetype i = 0; i < geoms.size(); ++i) {
        if (i > 0) {
            segments.push_back(Segment{1, {}, {}}); // the separation point between sub-lines
        }
        const QJsonObject item = geoms.at(i).toObject();
        const int count = item.value(QLatin1String("c")).toInt(-1);
        if (count <= 0) {
            continue;
        }
        Segment seg;
        seg.count = static_cast<std::size_t>(count);
        const auto metaArr = item.value(QLatin1String("m")).toArray();
        for (const auto& metaValue : metaArr) {
            const QJsonObject metaItem = metaValue.toObject();
            if (metaItem.value(QLatin1String("t")).toInt(-1) != 1) { // jdDouble only
                continue;
            }
            const QString tag = metaItem.value(QLatin1String("n")).toString().toLower();
            const QByteArray raw =
                QByteArray::fromBase64(metaItem.value(QLatin1String("d")).toString().toLatin1());
            if (raw.size() != static_cast<qsizetype>(seg.count) * static_cast<qsizetype>(sizeof(double))) {
                continue; // rule: a size mismatch drops that tag, matching legacy's Assert(False) fallback
            }
            if (tag == QLatin1String("ele")) {
                seg.ele.resize(seg.count);
                std::memcpy(seg.ele.data(), raw.constData(), static_cast<std::size_t>(raw.size()));
            } else if (tag == QLatin1String("time")) {
                seg.days.resize(seg.count);
                std::memcpy(seg.days.data(), raw.constData(), static_cast<std::size_t>(raw.size()));
            }
        }
        segments.push_back(std::move(seg));
    }

    const bool anyEle =
        std::any_of(segments.begin(), segments.end(), [](const Segment& s) { return !s.ele.empty(); });
    const bool anyTime =
        std::any_of(segments.begin(), segments.end(), [](const Segment& s) { return !s.days.empty(); });

    // Tech Lead ruling (review F3): a sub-line with no meta of its own is gap-filled with legacy's
    // literal FillChar(..., 0) -- ZERO, not this port's own NaN/invalid-QDateTime convention --
    // matching TDoublePointsMetaAggregator.AddItems/MoveRight (u_DoublePointsMetaAggregator.pas:
    // 126, 196) and EraseMetaItems (u_DoublePointsMetaFunc.pas:138-142). A raw double 0.0
    // reinterpreted as a Delphi TDateTime is the "zero date" 1899-12-30, not "no timestamp"; the
    // port has no null TDateTime, so fromDelphiDateTime(0.0) (a *valid* QDateTime on that date) is
    // the byte-exact analog -- an invalid QDateTime would still round-trip back out as 0.0 (see
    // toDelphiDateTime above) but would misrepresent what's actually stored here.
    PointsMeta result;
    if (anyEle) {
        for (const Segment& s : segments) {
            if (!s.ele.empty()) {
                result.elevation.insert(result.elevation.end(), s.ele.begin(), s.ele.end());
            } else {
                result.elevation.insert(result.elevation.end(), s.count, 0.0);
            }
        }
    }
    if (anyTime) {
        const QDateTime kDelphiZeroDate = fromDelphiDateTime(0.0);
        for (const Segment& s : segments) {
            if (!s.days.empty()) {
                for (const double d : s.days) {
                    result.timestamp.push_back(fromDelphiDateTime(d));
                }
            } else {
                result.timestamp.insert(result.timestamp.end(), s.count, kDelphiZeroDate);
            }
        }
    }
    return result;
}

} // namespace cg::geo
