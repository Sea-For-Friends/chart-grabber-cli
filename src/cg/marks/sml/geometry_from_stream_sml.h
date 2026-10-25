// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_GeometryFromStreamSML.pas (TGeometryFromStreamSML.Parse and its helpers).
// docs/specs/marks.md 2.8.3.

#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"

#include <QByteArrayView>

namespace cg::marks {

// Blob -> geometry. The blob is N x 24 bytes (geometry_point_sml.h); a trailing partial record is ignored
// (size div 24). Returns nullptr for an empty blob, for a single invalid point and when no valid vertex
// remains. Never reads outside `blob`, never throws for malformed content. `meta` is used for a line only,
// and only when it holds as many entries as there are records (separator records included, the terminator
// excluded); the entry of RAW record index I goes to the vertex of record I.
[[nodiscard]] geo::GeometryLonLatPtr parseGeometrySml(QByteArrayView blob,
                                                      const geo::GeometryLonLatFactory& factory,
                                                      const geo::PointsMeta* meta = nullptr);

} // namespace cg::marks
