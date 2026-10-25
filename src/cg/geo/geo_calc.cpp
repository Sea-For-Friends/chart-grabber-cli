// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/geo_calc.h"

#include <QtGlobal>

#include <cmath>
#include <limits>

namespace cg::geo {

GeoCalc::GeoCalc(DatumPtr datum) : datum_(std::move(datum)) {
    Q_ASSERT(datum_);
}

double GeoCalc::calcSingleLineLength(const IGeometryLonLatSingleLine& line) const {
    return datum_->calcDist(line.points());
}

double GeoCalc::calcLineLength(const IGeometryLonLatLine& line) const {
    // u_GeoCalc.pas:147-161: multi is tried first, then single, else NaN. Order matters if a
    // concrete class ever implemented both (it doesn't today), so keep it exactly as legacy.
    if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiLine*>(&line)) {
        double total = 0.0;
        for (std::size_t i = 0; i < multi->count(); ++i) {
            total += calcSingleLineLength(*multi->item(i));
        }
        return total;
    }
    if (const auto* single = dynamic_cast<const IGeometryLonLatSingleLine*>(&line)) {
        return calcSingleLineLength(*single);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double GeoCalc::calcContourPerimeter(const IGeometryLonLatContour& contour) const {
    // u_GeoCalc.pas:167-181 walks the contour's *closed* enumerator (AContour.GetEnum), which emits
    // the closing point (points.back() -> points.front()) whenever FCount > 1
    // (u_EnumDoublePointBySingleLine.pas:142/DoNext: "if FClosed and (FCount > 1)"). That is NOT
    // Datum::calcPolygonPerimeter's own guard (datum.cpp:222, size() > 2), which exists for that
    // function's other caller and would drop the closing leg for a 2-point contour (returning `d`
    // instead of legacy's `2d`). So the open sum plus the closing leg is reproduced here directly,
    // instead of delegating to calcPolygonPerimeter's stricter guard.
    const auto points = contour.points();
    double result = datum_->calcDist(points);
    if (!std::isnan(result) && points.size() > 1) {
        result += datum_->calcDist(points.back(), points.front());
    }
    return result;
}

double GeoCalc::calcSinglePolygonPerimeter(const IGeometryLonLatSinglePolygon& polygon) const {
    double total = calcContourPerimeter(*polygon.outerBorder());
    for (std::size_t i = 0; i < polygon.holeCount(); ++i) {
        total += calcContourPerimeter(*polygon.holeBorder(i));
    }
    return total;
}

double GeoCalc::calcPolygonPerimeter(const IGeometryLonLatPolygon& polygon) const {
    if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiPolygon*>(&polygon)) {
        double total = 0.0;
        for (std::size_t i = 0; i < multi->count(); ++i) {
            total += calcSinglePolygonPerimeter(*multi->item(i));
        }
        return total;
    }
    if (const auto* single = dynamic_cast<const IGeometryLonLatSinglePolygon*>(&polygon)) {
        return calcSinglePolygonPerimeter(*single);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double GeoCalc::calcContourArea(const IGeometryLonLatContour& contour, const std::stop_token& cancel) const {
    const auto points = contour.points();
    if (points.size() < 3) {
        return 0.0;
    }
    return datum_->calcPolygonArea(points, cancel);
}

double GeoCalc::calcSinglePolygonArea(const IGeometryLonLatSinglePolygon& polygon,
                                      const std::stop_token& cancel) const {
    double total = calcContourArea(*polygon.outerBorder(), cancel);
    for (std::size_t i = 0; i < polygon.holeCount(); ++i) {
        total -= calcContourArea(*polygon.holeBorder(i), cancel);
    }
    return total;
}

double GeoCalc::calcPolygonArea(const IGeometryLonLatPolygon& polygon, const std::stop_token& cancel) const {
    if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiPolygon*>(&polygon)) {
        double total = 0.0;
        for (std::size_t i = 0; i < multi->count(); ++i) {
            total += calcSinglePolygonArea(*multi->item(i), cancel);
        }
        return total;
    }
    if (const auto* single = dynamic_cast<const IGeometryLonLatSinglePolygon*>(&polygon)) {
        return calcSinglePolygonArea(*single, cancel);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace cg::geo
