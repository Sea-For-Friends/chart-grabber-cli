// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ProjectedCalc.pas (32 methods), u_ProjectedCalc.pas. docs/specs/geo.md §3.6.
//
// Nine free functions replace legacy's hand-devirtualised 32-method interface. Seven of them are
// pure delegation to the virtuals geometry_projected.h/.cpp (T-0017C1) already implements on
// IGeometryProjectedLine / IGeometryProjectedPolygon (isPointOnPath, isRectIntersectPath,
// isPointInPolygon, isPointOnBorder, isRectIntersectPolygon, isRectIntersectBorder, calcArea):
// legacy's TProjectedCalc re-derives the identical formula by hand in u_ProjectedCalc.pas instead
// of calling the object's own method (verified operator-for-operator against
// TGeometryProjectedLine.IsPointOnPath/.IsRectIntersectPath and
// TGeometryProjectedContour/TGeometryProjectedPolygonWithHoles' predicates -- the C1 review
// already cross-checked geometry_projected.cpp against those same legacy bodies). Two functions
// (calcLineLength, calcPolygonPerimeter) have no matching virtual anywhere in geometry_projected.h
// and are genuine new arithmetic here, a plain Euclidean walk.
//
// Legacy quirk kept: TProjectedCalc's four/five polygon dispatchers (`Supports(APoly,
// IGeometryProjectedSinglePolygon, ...) else Supports(..., IGeometryProjectedMultiPolygon, ...)
// else Result := False/NaN`) accept only IGeometryProjectedSinglePolygon /
// IGeometryProjectedMultiPolygon, never a bare IGeometryProjectedContour -- which
// GeometryProjectedFactory/GeometryProjectedPolygonBuilder (geometry_factory.h) never hand out as
// "the polygon" anyway, only ever as an outer/hole border. Passing a bare contour here returns
// false/NaN, matching legacy's fall-through branch, not the contour's own (perfectly valid)
// predicate. Reproduced via the same Single/Multi dynamic dispatch as legacy's `Supports`, not a
// direct virtual call, so this edge case matches bit-for-bit.

#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_projected.h"

namespace cg::geo {

// FEAT-GEO-013
[[nodiscard]] bool isPointOnLine(const IGeometryProjectedLine& line, DoublePoint point, double dist);
[[nodiscard]] bool isRectIntersectLine(const IGeometryProjectedLine& line, const DoubleRect& rect);
[[nodiscard]] double calcLineLength(const IGeometryProjectedLine& line);
[[nodiscard]] bool isPointInPolygon(const IGeometryProjectedPolygon& polygon, DoublePoint point);
[[nodiscard]] bool isPointOnPolygonBorder(const IGeometryProjectedPolygon& polygon, DoublePoint point,
                                          double dist);
[[nodiscard]] bool isRectIntersectPolygon(const IGeometryProjectedPolygon& polygon, const DoubleRect& rect);
[[nodiscard]] bool isRectIntersectPolygonBorder(const IGeometryProjectedPolygon& polygon,
                                                const DoubleRect& rect);
[[nodiscard]] double calcPolygonArea(const IGeometryProjectedPolygon& polygon);
[[nodiscard]] double calcPolygonPerimeter(const IGeometryProjectedPolygon& polygon);

} // namespace cg::geo
