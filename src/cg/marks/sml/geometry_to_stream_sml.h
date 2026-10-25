// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_GeometryToStreamSML.pas (TGeometryToStreamSML.Save and its helpers). docs/specs/marks.md 2.8.3.

#include "cg/geo/geometry_lonlat.h"

#include <QByteArray>

namespace cg::marks {

// Geometry -> blob of N x 24 bytes (geometry_point_sml.h). Point: one record. Line: the vertices, parts
// separated by (NaN, NaN), then one (NaN, NaN) terminator. Polygon: every contour as its vertices plus the
// first vertex again (the legacy contour enumerator closes it), each hole preceded by (NaN, -1), polygons of
// a multi-polygon separated by (NaN, NaN), then one record repeating the first vertex of the first outer
// contour. A null geometry, a multi-point, a bare contour and a multi-geometry without items give an empty
// blob (legacy: Assert(False) with assertions off, nothing written).
[[nodiscard]] QByteArray saveGeometrySml(const geo::IGeometryLonLat* geometry);

} // namespace cg::marks
