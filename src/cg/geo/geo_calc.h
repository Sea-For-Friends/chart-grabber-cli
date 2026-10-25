// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/GeoCalc/i_GeoCalc.pas, u_GeoCalc.pas (the IGeoCalc/TGeoCalc half only --
// IGeoCalcChangeable/TGeoCalcChangeable is view state and belongs to `ui`, §1.2).
// docs/specs/geo.md §5.
//
// FEAT-GEO-014, FEAT-GEO-015

#include "cg/geo/datum.h"
#include "cg/geo/geometry_lonlat.h"

#include <stop_token>

namespace cg::geo {

// The value half of IGeoCalcConfig (i_GeoCalcConfig.pas:31 TGeoCalcDatumSource). The
// ConfigElement/IChangeable wrappers around this (u_GeoCalcConfig.pas) are a `ui` ticket -- this
// module only exports the plain value so `ui` can persist it (key "[GeoCalc] DatumSource",
// confirm against u_GeoCalcConfig.pas when that ticket lands).
struct GeoCalcSettings {
    enum class DatumSource : std::uint8_t { Wgs84, Zmp };
    DatumSource datumSource = DatumSource::Wgs84;
};

// Ports IGeoCalc / TGeoCalc (u_GeoCalc.pas:124-260). The single/multi dispatch legacy exposes as
// eight separate interface methods (CalcSingleLineLength/CalcMultiLineLength/...) is kept as
// private helpers here; only the three polymorphic entry points (matching legacy's own
// CalcLineLength/CalcPolygonPerimeter/CalcPolygonArea, which already do that dispatch) are public.
class GeoCalc {
public:
    explicit GeoCalc(DatumPtr datum);

    [[nodiscard]] const DatumPtr& datum() const noexcept { return datum_; }
    [[nodiscard]] bool isSame(const GeoCalc& other) const { return datum_->isSameDatum(*other.datum_); }

    // u_GeoCalc.pas:137-161. Sum of datum()->calcDist over consecutive points, NO closing segment
    // (open). A multi-line sums its parts. Anything else (neither single nor multi) is NaN.
    [[nodiscard]] double calcLineLength(const IGeometryLonLatLine& line) const;

    // u_GeoCalc.pas:166-216. A contour's perimeter is a closed loop: the open sum plus a closing
    // segment (points.back() -> points.front()) whenever the contour has more than one point --
    // legacy's own closed-enumerator guard (u_EnumDoublePointBySingleLine.pas:142), NOT
    // Datum::calcPolygonPerimeter's stricter size() > 2 guard (that one is for Datum's other
    // caller). A single polygon adds every hole's perimeter to its outer border's; a multi-polygon
    // sums its parts; anything else is NaN.
    [[nodiscard]] double calcPolygonPerimeter(const IGeometryLonLatPolygon& polygon) const;

    // u_GeoCalc.pas:221-260. A contour under 3 points has area 0; a single polygon's area is its
    // outer border's area MINUS every hole's; a multi-polygon sums its parts; anything else is
    // NaN. Cancellation propagates as NaN (x + NaN == NaN), so a partially-cancelled multi-polygon
    // yields NaN for the whole thing -- legacy behavior, callers already test std::isnan.
    [[nodiscard]] double calcPolygonArea(const IGeometryLonLatPolygon& polygon,
                                         const std::stop_token& cancel = {}) const;

private:
    [[nodiscard]] double calcSingleLineLength(const IGeometryLonLatSingleLine& line) const;
    [[nodiscard]] double calcContourPerimeter(const IGeometryLonLatContour& contour) const;
    [[nodiscard]] double calcSinglePolygonPerimeter(const IGeometryLonLatSinglePolygon& polygon) const;
    [[nodiscard]] double calcContourArea(const IGeometryLonLatContour& contour,
                                         const std::stop_token& cancel) const;
    [[nodiscard]] double calcSinglePolygonArea(const IGeometryLonLatSinglePolygon& polygon,
                                               const std::stop_token& cancel) const;

    DatumPtr datum_;
};

} // namespace cg::geo
