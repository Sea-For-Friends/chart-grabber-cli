// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/ConfigData/u_ConfigProviderHelpers.pas:239-419 -- ReadPolygon, WritePolygon and their
// file-local WriteContour / WriteSinglePolygon / WriteMultiPolygon. A polygon is stored in an INI
// section as point keys (PointLon_<i> / PointLat_<i>) with NaN separators between contours.
// docs/specs/region-download.md:132-179 (ticket T-0158). The appearance half of the same Pascal unit
// is cg::vector (vector/config_provider_helpers.h).

#include "cg/core/config_provider.h"
#include "cg/core/config_writer.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"

#include <memory>

namespace cg::geo {

// FEAT-GEO-024 (contributes; the .hlg reader is other work, see docs/parity/held-open.md)
// Null when no first point is found (:271-297 key chain, :244-257 validity).
[[nodiscard]] std::shared_ptr<const IGeometryLonLatPolygon> readPolygon(const core::IConfigProvider& section,
                                                                        const GeometryLonLatFactory& factory);

// FEAT-REG-006 (contributes; the selection-file UI is other work, see docs/parity/held-open.md)
// A null polygon writes nothing (:409).
void writePolygon(core::IConfigWriter& section, const std::shared_ptr<const IGeometryLonLatPolygon>& polygon);

} // namespace cg::geo
