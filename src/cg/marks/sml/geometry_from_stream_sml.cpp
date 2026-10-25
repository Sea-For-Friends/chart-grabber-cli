// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/sml/geometry_from_stream_sml.h"

#include "cg/marks/sml/geometry_point_sml.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace cg::marks {

namespace {

using geo::LonLat;

const std::uint8_t* recordAt(QByteArrayView blob, std::size_t index) noexcept {
    return reinterpret_cast<const std::uint8_t*>(blob.constData()) + index * kGeometryPointSmlSize;
}

// |value| < 360 on the 80-bit value itself, as the Win32 build compares the Extended before narrowing
// (u_GeometryFromStreamSML.pas:82, :109). On Win64 the compare is on the rounded Double, so [360 - 2^-55,
// 360) would be out of range there (open question 1). Only called for values that are not NaN.
bool isInsideLimit(const std::uint8_t* ext) noexcept {
    const Extended80Parts p = unpackExtended80(ext);
    if (p.exponent == 0) {
        return true;
    }
    if (p.exponent == 0x7FFF) {
        return false; // infinity
    }
    constexpr std::uint16_t k360Exp = 0x4007; // 360 = 0xB4.. * 2^(0x4007 - 16383 - 63)
    constexpr std::uint64_t k360Sig = 0xB400000000000000ULL;
    return p.exponent < k360Exp || (p.exponent == k360Exp && p.significand < k360Sig);
}

struct Decoded {
    LonLat point{};
    bool xValid = false;
    bool yValid = false;
};

// SMLPointToDoublePoint (:75-90): nullopt is CEmptyDoublePoint.
std::optional<LonLat> toPoint(const std::uint8_t* rec) noexcept {
    const std::uint8_t* x = rec;
    const std::uint8_t* y = rec + kExtended80Size;
    if (extended80IsNaN(x) || extended80IsNaN(y)) {
        return std::nullopt;
    }
    if (!isInsideLimit(x) || !isInsideLimit(y)) {
        return std::nullopt;
    }
    return LonLat{extended80ToDouble(x), extended80ToDouble(y)};
}

// SMLPointToDoublePointEx (:92-123). X NaN with a non-NaN Y is the hole marker (YIsValid true).
// X in range with Y NaN: ASSUMPTION (open question 2). On Win32 an ordered x87 compare of a NaN raises when
// the invalid-operation exception is unmasked (Delphi default) and the try/except returns "empty". On Win64
// the compare runs on a Double after the conversion and whether it raises depends on generated code that is
// not in the tree; if it does not, the legacy record is VALID with Y = NaN. The port returns "empty".
Decoded toPointEx(const std::uint8_t* rec) noexcept {
    const std::uint8_t* x = rec;
    const std::uint8_t* y = rec + kExtended80Size;
    Decoded d;
    if (extended80IsNaN(x)) {
        if (!extended80IsNaN(y)) {
            d.yValid = true;
        }
        return d;
    }
    if (extended80IsNaN(y) || !isInsideLimit(x) || !isInsideLimit(y)) {
        return d;
    }
    d.point = LonLat{extended80ToDouble(x), extended80ToDouble(y)};
    d.xValid = true;
    d.yValid = true;
    return d;
}

bool isEmptyRecord(const std::uint8_t* rec) noexcept {
    return extended80IsNaN(rec) || extended80IsNaN(rec + kExtended80Size);
}

bool extendedEqual(const std::uint8_t* a, const std::uint8_t* b) noexcept {
    const Extended80Parts pa = unpackExtended80(a);
    const Extended80Parts pb = unpackExtended80(b);
    if (pa.significand == 0 && pb.significand == 0) {
        return true; // +0 == -0
    }
    const auto eff = [](const Extended80Parts& p) { return p.exponent == 0 ? 1 : p.exponent; };
    return pa.negative == pb.negative && eff(pa) == eff(pb) && pa.significand == pb.significand;
}

// SmlPointsEqual (:146-162).
bool pointsEqual(const std::uint8_t* p1, const std::uint8_t* p2) noexcept {
    const bool e1 = isEmptyRecord(p1);
    const bool e2 = isEmptyRecord(p2);
    if (e1 && e2) {
        return true;
    }
    if (!e1 && !e2) {
        return extendedEqual(p1, p2) && extendedEqual(p1 + kExtended80Size, p2 + kExtended80Size);
    }
    return false;
}

geo::GeometryLonLatPtr parseLine(QByteArrayView blob, const geo::GeometryLonLatFactory& factory,
                                 const geo::PointsMeta* meta, std::size_t count) {
    // :228-232. The port's PointsMeta carries no separate Count: it is the length of the arrays present.
    bool useMeta = false;
    if (meta != nullptr && !meta->isEmpty()) {
        const bool elevOk = !meta->hasElevation() || meta->elevation.size() == count;
        const bool timeOk = !meta->hasTimestamp() || meta->timestamp.size() == count;
        useMeta = elevOk && timeOk;
    }

    geo::GeometryLonLatLineBuilder builder = factory.makeLineBuilder();
    std::vector<LonLat> points;
    geo::PointsMeta partMeta;
    const auto flush = [&] {
        if (!points.empty()) {
            builder.addLine(std::make_shared<const std::vector<LonLat>>(std::move(points)),
                            std::move(partMeta));
            points = {};
            partMeta = {};
        }
    };
    for (std::size_t i = 0; i < count; ++i) {
        const Decoded d = toPointEx(recordAt(blob, i));
        if (d.xValid && d.yValid) {
            points.push_back(d.point);
            if (useMeta) {
                if (meta->hasElevation()) {
                    partMeta.elevation.push_back(meta->elevation[i]);
                }
                if (meta->hasTimestamp()) {
                    partMeta.timestamp.push_back(meta->timestamp[i]);
                }
            }
        } else {
            flush();
        }
    }
    flush();
    return builder.makeStaticAndClear();
}

geo::GeometryLonLatPtr parsePolygon(QByteArrayView blob, const geo::GeometryLonLatFactory& factory,
                                    std::size_t count) {
    geo::GeometryLonLatPolygonBuilder builder = factory.makePolygonBuilder();
    std::vector<LonLat> ring;
    bool isOuter = true;
    const auto flush = [&] {
        auto pts = std::make_shared<const std::vector<LonLat>>(std::move(ring));
        ring = {};
        if (isOuter) {
            builder.addOuter(std::move(pts));
        } else {
            builder.addHole(std::move(pts));
        }
    };
    for (std::size_t i = 0; i < count; ++i) {
        const Decoded d = toPointEx(recordAt(blob, i));
        if (d.xValid && d.yValid) {
            ring.push_back(d.point);
        } else if (!ring.empty()) {
            flush();
            isOuter = !d.yValid; // :308 -- inside the "ring is not empty" branch, as in legacy
        }
    }
    if (!ring.empty()) {
        flush();
    }
    return builder.makeStaticAndClear();
}

} // namespace

geo::GeometryLonLatPtr parseGeometrySml(QByteArrayView blob, const geo::GeometryLonLatFactory& factory,
                                        const geo::PointsMeta* meta) {
    static_assert(kGeometryPointSmlSize == 24); // :130
    std::size_t count = static_cast<std::size_t>(blob.size()) / kGeometryPointSmlSize;
    if (count == 0) {
        return nullptr;
    }
    if (count == 1) {
        const std::optional<LonLat> p = toPoint(recordAt(blob, 0));
        return p ? factory.createPoint(*p) : nullptr;
    }
    // IsPolygonInStream (:164-196)
    const std::uint8_t* first = recordAt(blob, 0);
    const std::uint8_t* last = recordAt(blob, count - 1);
    if (pointsEqual(first, last)) {
        --count;
        return parsePolygon(blob, factory, count);
    }
    if (isEmptyRecord(last)) {
        --count;
    }
    return parseLine(blob, factory, meta, count);
}

} // namespace cg::marks
