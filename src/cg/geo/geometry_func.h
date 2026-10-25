// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_GeometryFunc.pas (LonLat-only helpers, docs/specs/geo.md §3.8).
// Out of scope for T-0017A (see docs/handoffs/T-0017A.md): isProjectedPolygonSimpleRect,
// tryProjectedPolygonToTileRect and CalcTileCountIn* (the projected side, T-0017C/T-0018).
// geometryToPlainText lands here in T-0019A2 -- it needed CoordToStringConverter (T-0019A1),
// which had not been ported when this file was first written (ownership per T-0017A review F8,
// docs/handoffs/T-0017A-review.md: "no other ticket claims it").

#include "cg/geo/coord_to_string.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_lonlat.h"

#include <QString>

namespace cg::geo {

class IProjection;

// GetGeometryLonLatNearestPoint (u_GeometryFunc.pas:38-45, 393-432): the vertex of `geometry`
// nearest `currMapPixel` in map-pixel space, or an empty LonLat when none is within
// `maxDistInMapPixel` pixels. Iterates points() directly rather than through an enumerator
// (point streams are T-0017B) -- behaviorally identical, since a contour's synthesized closing
// vertex only ever duplicates points()[0] and so never changes which vertex is nearest.
// FEAT-GEO-011
[[nodiscard]] LonLat nearestPoint(const IGeometryLonLat& geometry, const IProjection& projection,
                                  DoublePoint currentMapPixel, double maxDistInMapPixel);

// IsValidLonLatLine (u_GeometryFunc.pas:679-694): at least 2 points (a single line needs 2+; a
// multi needs 2+ sub-lines, or exactly 1 sub-line with 2+ points).
[[nodiscard]] bool isValidLonLatLine(const IGeometryLonLatLine& geometry);

// IsValidLonLatPolygon (u_GeometryFunc.pas:696-732): an outer ring needs 3+ points.
[[nodiscard]] bool isValidLonLatPolygon(const IGeometryLonLatPolygon& geometry);

// IsLonLatPolygonSimpleRect (u_GeometryFunc.pas:825-857): a single, hole-free, 4-point polygon
// whose corners -- rounded ToTopLeft, exactly as legacy's PointFromDoublePoint -- form an
// axis-aligned rectangle in either winding order.
[[nodiscard]] bool isLonLatPolygonSimpleRect(const IGeometryLonLatPolygon& polygon);

// GeometryLonLatToPlainText (u_GeometryFunc.pas:1017-1034): dispatches on the geometry's own
// shape -- point / line (single-or-multi) / polygon (single-or-multi) -- and delegates to
// converter.lonLatConvert() per vertex. `pointSep` joins vertices within one line/contour,
// `contourSep` joins a polygon's outer border and its holes, `multiSep` joins the items of a
// multi-line or multi-polygon. A shape this port does not model as IGeometryLonLat (there is
// none) falls through to "" (legacy: `Result := ''` when none of the three Supports() checks
// match).
[[nodiscard]] QString geometryToPlainText(const IGeometryLonLat& geometry,
                                          const CoordToStringConverter& converter, const QString& pointSep,
                                          const QString& contourSep, const QString& multiSep);

} // namespace cg::geo
