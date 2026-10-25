// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: MainForm/frm_Main.pas:7624-7636 (polygon along a line); uses the ports
// of u_EnumDoublePointLine2Poly.pas:269-286, u_GeometryLonLatFactory.pas:1123-1150,
// u_ConfigProviderHelpers.pas:239-419. Ports: nothing. docs/specs/app-cli.md 3.5, 5.2.
//
// The job's area: a rectangle, a .hlg selection file or a corridor along a GPX route.

#include "cg/app/job.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/geo/projection.h"

#include <QString>

#include <expected>
#include <functional>
#include <memory>

namespace cg::app {

// Exit 3 on every refusal except a GPX file that does not parse (exit 2). warn gets one line per warning.
[[nodiscard]] std::expected<std::shared_ptr<const geo::IGeometryLonLatPolygon>, CliError>
buildArea(const Job& job, const geo::GeometryLonLatFactory& factory,
          const geo::IProjection& corridorProjection, const std::function<void(const QString&)>& warn);

// widthNm is the TOTAL width of the corridor (the legacy filter takes the radius).
[[nodiscard]] std::shared_ptr<const geo::IGeometryLonLatPolygon>
corridorByLine(const geo::IGeometryLonLatLine& line, double widthNm, const geo::IProjection& projection,
               const geo::GeometryLonLatFactory& factory);

} // namespace cg::app
