// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: t_GeoTypes.pas, u_GeoFunc.pas (arithmetic half)
//
// The value vocabulary of the whole geo/render/region stack. Legacy used one type,
// TDoublePoint, for lon/lat, relative, map-pixel and tile coordinates alike; a caller that
// mixed them up got silent nonsense. The port keeps TDoublePoint for everything *projected*
// (a plain QPointF) but gives geographic coordinates their own type, because that is exactly
// the split the IGeometryLonLat / IGeometryProjected hierarchies already make (docs/specs/geo.md
// §2.1).

#include <QPoint>
#include <QPointF>

#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>

namespace cg::geo {

// --- points ------------------------------------------------------------------
// Projected / relative / map-pixel / tile coordinates. Identical layout to TDoublePoint.
using DoublePoint = QPointF;
using PointI = QPoint;

// Geographic coordinates, degrees, WGS84 unless a datum says otherwise.
// x == lon, y == lat in every legacy TDoublePoint that holds a geographic coordinate
// (t_GeoTypes.pas has no separate type; u_GeoFunc.pas:DoublePoint(ALon, ALat) fixes the order).
struct LonLat {
    double lon = 0.0;
    double lat = 0.0;

    [[nodiscard]] bool operator==(const LonLat&) const = default;
    [[nodiscard]] DoublePoint toPoint() const noexcept { return {lon, lat}; }
    [[nodiscard]] static LonLat fromPoint(DoublePoint p) noexcept { return {p.x(), p.y()}; }
};

// --- rects -------------------------------------------------------------------
// QRect/QRectF are deliberately NOT used: QRect::right() == left + width - 1 and
// QRectF::bottom() differ from the Delphi TRect convention this module is full of.
// All four rects below are half-open [left, right) x [top, bottom) in *pixel and tile*
// space (u_ProjectionBasic256x256.pas:1193 GetTileRect = Rect(0,0,FTiles,FTiles)), and
// closed in lon/lat space, where Top is the *larger* latitude (north-up).
struct RectI {
    int left = 0, top = 0, right = 0, bottom = 0;

    [[nodiscard]] bool operator==(const RectI&) const = default;
    [[nodiscard]] int width() const noexcept { return right - left; }
    [[nodiscard]] int height() const noexcept { return bottom - top; }
    [[nodiscard]] bool isEmpty() const noexcept { return right <= left || bottom <= top; }
    [[nodiscard]] PointI topLeft() const noexcept { return {left, top}; }
    [[nodiscard]] PointI bottomRight() const noexcept { return {right, bottom}; }
};

struct DoubleRect {
    double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;

    [[nodiscard]] bool operator==(const DoubleRect&) const = default;
    [[nodiscard]] DoublePoint topLeft() const noexcept { return {left, top}; }
    [[nodiscard]] DoublePoint bottomRight() const noexcept { return {right, bottom}; }
};

// Geographic bounding box. Top >= Bottom (north-up), Left <= Right except when the box
// crosses the anti-meridian, which legacy represents exactly this way (Left > Right).
struct LonLatRect {
    double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;

    [[nodiscard]] bool operator==(const LonLatRect&) const = default;
    [[nodiscard]] LonLat topLeft() const noexcept { return {left, top}; }
    [[nodiscard]] LonLat bottomRight() const noexcept { return {right, bottom}; }
    [[nodiscard]] LonLat center() const noexcept;              // u_LonLatRect.pas:145
    [[nodiscard]] bool isPointInRect(LonLat p) const noexcept; // u_LonLatRect.pas:117
    [[nodiscard]] bool isIntersecWithRect(const LonLatRect& r) const noexcept;
    [[nodiscard]] static LonLatRect united(const LonLatRect& a, const LonLatRect& b) noexcept;
    [[nodiscard]] static LonLatRect fromPoints(const LonLat* pts, std::size_t count) noexcept;
};

// --- rounding modes (t_GeoTypes.pas:30-32, ported name-for-name) ---------------
enum class PointRounding : std::uint8_t { Closest, ToTopLeft, ToBottomRight };
enum class RectRounding : std::uint8_t { Closest, Outside, Inside, ToTopLeft };
enum class RectWithPolygonIntersection : std::uint8_t {
    NoIntersect,
    IntersectPartial,
    RectInPolygon,
    PolygonInRect
};

// --- helpers (u_GeoFunc.pas) ---------------------------------------------------
[[nodiscard]] PointI roundPoint(DoublePoint p, PointRounding mode) noexcept;    // u_GeoFunc.pas:86-111
[[nodiscard]] RectI roundRect(const DoubleRect& r, RectRounding mode) noexcept; // u_GeoFunc.pas:113-162
[[nodiscard]] DoubleRect toDoubleRect(const RectI& r) noexcept;
[[nodiscard]] bool isPointInRect(DoublePoint p, const DoubleRect& r) noexcept;
[[nodiscard]] bool pointIsEmpty(DoublePoint p) noexcept; // u_GeoFunc.pas: NaN sentinel
[[nodiscard]] DoublePoint emptyDoublePoint() noexcept;   // {NaN, NaN}

} // namespace cg::geo
