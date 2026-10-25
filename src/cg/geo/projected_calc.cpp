// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/projected_calc.h"

#include <cmath>
#include <limits>

namespace cg::geo {
namespace {

[[nodiscard]] constexpr double nan() noexcept {
    return std::numeric_limits<double>::quiet_NaN();
}

// TProjectedCalc.CalcSingleLineLength (u_ProjectedCalc.pas:409-424): plain Euclidean sum over the
// line's own points, no closing segment -- a SingleLine's GetEnum is unclosed (AClosed=False,
// u_GeometryLonLat.pas-equivalent u_GeometryProjected.pas ctor).
[[nodiscard]] double singleLineLength(const IGeometryProjectedSingleLine& line) {
    const auto pts = line.points();
    if (pts.size() < 2) {
        return 0.0;
    }
    double result = 0.0;
    DoublePoint prev = pts[0];
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const DoublePoint cur = pts[i];
        result += std::hypot(prev.x() - cur.x(), prev.y() - cur.y());
        prev = cur;
    }
    return result;
}

// TProjectedCalc.CalcContourPerimeter (u_ProjectedCalc.pas:322-337): a CLOSED walk -- the
// contour's own GetEnum re-emits points()[0] after the last vertex, so the final segment wraps
// from the last point back to the first (same implicit-closure convention as
// GeometryProjectedContour::calcArea, geometry_projected.cpp).
[[nodiscard]] double contourPerimeter(const IGeometryProjectedContour& contour) {
    const auto pts = contour.points();
    if (pts.size() < 2) {
        return 0.0;
    }
    double result = 0.0;
    DoublePoint prev = pts[pts.size() - 1];
    for (const DoublePoint& cur : pts) {
        result += std::hypot(prev.x() - cur.x(), prev.y() - cur.y());
        prev = cur;
    }
    return result;
}

// TProjectedCalc.CalcSinglePolygonPerimeter / CalcMultiPolygonPerimeter (u_ProjectedCalc.pas:
// 339-360): outer + every hole for a single polygon, plain sum for a multi.
[[nodiscard]] double singlePolygonPerimeter(const IGeometryProjectedSinglePolygon& polygon) {
    double result = contourPerimeter(*polygon.outerBorder());
    for (std::size_t i = 0; i < polygon.holeCount(); ++i) {
        result += contourPerimeter(*polygon.holeBorder(i));
    }
    return result;
}

} // namespace

bool isPointOnLine(const IGeometryProjectedLine& line, DoublePoint point, double dist) {
    // TProjectedCalc.IsPointOnLine dispatches Supports(Single)/Supports(Multi) to two hand
    // copies of this exact formula (u_ProjectedCalc.pas:433-476/498-513); IGeometryProjectedLine
    // has no third concrete implementer, so calling the already-verified virtual directly
    // (geometry_projected.cpp, C1) is behaviourally identical.
    return line.isPointOnPath(point, dist);
}

bool isRectIntersectLine(const IGeometryProjectedLine& line, const DoubleRect& rect) {
    return line.isRectIntersectPath(rect);
}

double calcLineLength(const IGeometryProjectedLine& line) {
    if (const auto* single = dynamic_cast<const IGeometryProjectedSingleLine*>(&line)) {
        return singleLineLength(*single);
    }
    if (const auto* multi = dynamic_cast<const IGeometryProjectedMultiLine*>(&line)) {
        double result = 0.0;
        for (std::size_t i = 0; i < multi->count(); ++i) {
            result += singleLineLength(*multi->item(i));
        }
        return result;
    }
    return nan(); // TProjectedCalc.CalcLineLength's else branch (u_ProjectedCalc.pas:530).
}

bool isPointInPolygon(const IGeometryProjectedPolygon& polygon, DoublePoint point) {
    if (dynamic_cast<const IGeometryProjectedSinglePolygon*>(&polygon) != nullptr ||
        dynamic_cast<const IGeometryProjectedMultiPolygon*>(&polygon) != nullptr) {
        return polygon.isPointInPolygon(point);
    }
    return false; // TProjectedCalc.IsPointInPolygon's else branch (bare Contour -- see header note).
}

bool isPointOnPolygonBorder(const IGeometryProjectedPolygon& polygon, DoublePoint point, double dist) {
    if (dynamic_cast<const IGeometryProjectedSinglePolygon*>(&polygon) != nullptr ||
        dynamic_cast<const IGeometryProjectedMultiPolygon*>(&polygon) != nullptr) {
        return polygon.isPointOnBorder(point, dist);
    }
    return false;
}

bool isRectIntersectPolygon(const IGeometryProjectedPolygon& polygon, const DoubleRect& rect) {
    if (dynamic_cast<const IGeometryProjectedSinglePolygon*>(&polygon) != nullptr ||
        dynamic_cast<const IGeometryProjectedMultiPolygon*>(&polygon) != nullptr) {
        return polygon.isRectIntersectPolygon(rect);
    }
    return false;
}

bool isRectIntersectPolygonBorder(const IGeometryProjectedPolygon& polygon, const DoubleRect& rect) {
    if (dynamic_cast<const IGeometryProjectedSinglePolygon*>(&polygon) != nullptr ||
        dynamic_cast<const IGeometryProjectedMultiPolygon*>(&polygon) != nullptr) {
        return polygon.isRectIntersectBorder(rect);
    }
    return false;
}

double calcPolygonArea(const IGeometryProjectedPolygon& polygon) {
    if (dynamic_cast<const IGeometryProjectedSinglePolygon*>(&polygon) != nullptr ||
        dynamic_cast<const IGeometryProjectedMultiPolygon*>(&polygon) != nullptr) {
        return polygon.calcArea();
    }
    return nan(); // TProjectedCalc.CalcPolygonArea's else branch (u_ProjectedCalc.pas:271).
}

double calcPolygonPerimeter(const IGeometryProjectedPolygon& polygon) {
    if (const auto* single = dynamic_cast<const IGeometryProjectedSinglePolygon*>(&polygon)) {
        return singlePolygonPerimeter(*single);
    }
    if (const auto* multi = dynamic_cast<const IGeometryProjectedMultiPolygon*>(&polygon)) {
        double result = 0.0;
        for (std::size_t i = 0; i < multi->count(); ++i) {
            result += singlePolygonPerimeter(*multi->item(i));
        }
        return result;
    }
    return nan(); // TProjectedCalc.CalcPolygonPerimeter's else branch (u_ProjectedCalc.pas:285).
}

} // namespace cg::geo
