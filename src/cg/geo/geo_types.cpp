// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/geo_types.h"

#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace cg::geo {
namespace {

constexpr double kRoundEpsilon = 0.001; // u_GeoFunc.pas:97,101,127-146 (Floor/Ceil bias)
constexpr int kMaxInt = std::numeric_limits<int>::max();

// u_GeoFunc.pas:105-110 / :150-161: a coordinate that overflowed a signed 32-bit int during the
// Floor/Ceil/Round above (and therefore came back negative) is patched back to MaxInt when the
// original double was in fact huge and positive. This is what keeps zoom-23 pixel coordinates
// (up to 2147483647) from wrapping to a negative TRect edge. Reproduced exactly, not "fixed" to a
// wider integer type, because callers (projection.cpp) compare against this same MaxInt.
[[nodiscard]] int patchOverflow(int rounded, double original) noexcept {
    if (rounded < 0 && original > static_cast<double>(kMaxInt) - 2.0) {
        return kMaxInt;
    }
    return rounded;
}

// Delphi Round/Trunc/Floor->Integer on x86, with range checking off (the shipping default), traps
// an out-of-range value to the FPU's "integer indefinite" pattern, 0x80000000 (== INT_MIN) --
// which is exactly what lets the legacy "if Result < 0 then Result := MaxInt" checks
// (u_GeoFunc.pas:105-110/150-161, u_ProjectionBasic256x256.pas:774-781/1570-1577) catch a value
// that overflowed while computing something huge and positive. Reproduced explicitly so the port
// never performs an out-of-range double-to-int cast, which is undefined behavior in C++ (unlike
// the guaranteed-defined wraparound this module relies on elsewhere for already-in-range values).
[[nodiscard]] int toInt32WithOverflowTrap(double v) noexcept {
    if (v >= 2147483648.0 || v < -2147483648.0) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(v);
}

[[nodiscard]] int roundClosest(double v) noexcept {
    // Delphi Round() is banker's rounding (half-to-even); std::nearbyint under the default
    // FE_TONEAREST mode is the same rule (docs/specs/geo.md §11.0).
    return toInt32WithOverflowTrap(std::nearbyint(v));
}

[[nodiscard]] int roundToTopLeft(double v) noexcept {
    return toInt32WithOverflowTrap(std::floor(v + kRoundEpsilon));
}
[[nodiscard]] int roundToBottomRight(double v) noexcept {
    return toInt32WithOverflowTrap(std::ceil(v + kRoundEpsilon));
}

} // namespace

PointI roundPoint(DoublePoint p, PointRounding mode) noexcept {
    int x = 0;
    int y = 0;
    switch (mode) {
    case PointRounding::Closest:
        x = roundClosest(p.x());
        y = roundClosest(p.y());
        break;
    case PointRounding::ToTopLeft:
        x = roundToTopLeft(p.x());
        y = roundToTopLeft(p.y());
        break;
    case PointRounding::ToBottomRight:
        x = roundToBottomRight(p.x());
        y = roundToBottomRight(p.y());
        break;
    }
    return {patchOverflow(x, p.x()), patchOverflow(y, p.y())};
}

RectI roundRect(const DoubleRect& r, RectRounding mode) noexcept {
    int left = 0, top = 0, right = 0, bottom = 0;
    switch (mode) {
    case RectRounding::Closest:
        left = roundClosest(r.left);
        top = roundClosest(r.top);
        right = roundClosest(r.right);
        bottom = roundClosest(r.bottom);
        break;
    case RectRounding::Inside:
        left = toInt32WithOverflowTrap(std::ceil(r.left - kRoundEpsilon));
        top = toInt32WithOverflowTrap(std::ceil(r.top - kRoundEpsilon));
        right = toInt32WithOverflowTrap(std::floor(r.right + kRoundEpsilon));
        bottom = toInt32WithOverflowTrap(std::floor(r.bottom + kRoundEpsilon));
        if (right < left)
            right = left;
        if (bottom < top)
            bottom = top;
        break;
    case RectRounding::Outside:
        // u_GeoFunc.pas:135-139 (rrOutside): Left/Top use the SAME plus-epsilon Floor as point
        // rounding's ToTopLeft, but Right/Bottom use Ceil(v - epsilon) -- the OPPOSITE sign from
        // roundToBottomRight()'s point-rounding formula (Ceil(v + epsilon), u_GeoFunc.pas:100).
        // Reusing roundToBottomRight() here would push an exactly-integer Right/Bottom (e.g. a
        // tile-aligned polygon bounds) one tile too far out; legacy's minus-epsilon keeps an exact
        // boundary tight, only compensating for float noise just below it.
        left = roundToTopLeft(r.left);
        top = roundToTopLeft(r.top);
        right = toInt32WithOverflowTrap(std::ceil(r.right - kRoundEpsilon));
        bottom = toInt32WithOverflowTrap(std::ceil(r.bottom - kRoundEpsilon));
        break;
    case RectRounding::ToTopLeft:
        left = roundToTopLeft(r.left);
        top = roundToTopLeft(r.top);
        right = roundToTopLeft(r.right);
        bottom = roundToTopLeft(r.bottom);
        break;
    }
    return {patchOverflow(left, r.left), patchOverflow(top, r.top), patchOverflow(right, r.right),
            patchOverflow(bottom, r.bottom)};
}

DoubleRect toDoubleRect(const RectI& r) noexcept {
    return {static_cast<double>(r.left), static_cast<double>(r.top), static_cast<double>(r.right),
            static_cast<double>(r.bottom)};
}

bool isPointInRect(DoublePoint p, const DoubleRect& r) noexcept {
    return p.x() >= r.left && p.x() <= r.right && p.y() >= r.top && p.y() <= r.bottom;
}

// u_GeoFunc.pas:550-552 PointIsEmpty: IsNan(X) or IsNan(Y). A point with one NaN coordinate is empty.
bool pointIsEmpty(DoublePoint p) noexcept {
    return std::isnan(p.x()) || std::isnan(p.y());
}

DoublePoint emptyDoublePoint() noexcept {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    return {nan, nan};
}

LonLat LonLatRect::center() const noexcept {
    return {(left + right) / 2.0, (top + bottom) / 2.0}; // u_GeoFunc.pas RectCenter(TDoubleRect)
}

bool LonLatRect::isPointInRect(LonLat p) const noexcept {
    // u_LonLatRect.pas:181-187: a plain AABB test, no anti-meridian unwrap.
    return p.lon <= right && p.lon >= left && p.lat <= top && p.lat >= bottom;
}

bool LonLatRect::isIntersecWithRect(const LonLatRect& r) const noexcept {
    // u_LonLatRect.pas:161-166
    return left <= r.right && right >= r.left && top >= r.bottom && bottom <= r.top;
}

LonLatRect LonLatRect::united(const LonLatRect& a, const LonLatRect& b) noexcept {
    // u_GeoFunc.pas UnionLonLatRects
    LonLatRect result = a;
    if (result.left > b.left)
        result.left = b.left;
    if (result.right < b.right)
        result.right = b.right;
    if (result.top < b.top)
        result.top = b.top;
    if (result.bottom > b.bottom)
        result.bottom = b.bottom;
    return result;
}

LonLatRect LonLatRect::fromPoints(const LonLat* pts, std::size_t count) noexcept {
    // u_GeoFunc.pas LonLatMBRByPoints / UpdateLonLatMBRByPoint
    Q_ASSERT(pts != nullptr);
    Q_ASSERT(count > 0);
    LonLatRect result{pts[0].lon, pts[0].lat, pts[0].lon, pts[0].lat};
    for (std::size_t i = 1; i < count; ++i) {
        const LonLat& p = pts[i];
        result.left = std::min(result.left, p.lon);
        result.right = std::max(result.right, p.lon);
        result.top = std::max(result.top, p.lat);
        result.bottom = std::min(result.bottom, p.lat);
    }
    return result;
}

} // namespace cg::geo
