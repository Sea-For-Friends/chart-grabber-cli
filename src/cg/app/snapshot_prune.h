// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code. Inherits: Src/RegionProcess/Delete/u_ThreadDeleteTiles.pas ProcessRegion (per-tile lookup, then
// DeleteTile with the found version). Ports: nothing. docs/specs/weather-sources.md 9 (T-0246).
//
// Removes, for every tile of the area at the given zooms, every stored version except the map's current one.

#include "cg/core/result.h"
#include "cg/maptype/map_type.h"

#include <QtGlobal>

#include <span>
#include <stop_token>

namespace cg::geo {
class IGeometryLonLatPolygon;
}

namespace cg::app {

class AppContext;

// Returns the number of deleted tiles. Error = the first storage error (the caller warns); stop requested ->
// ErrorCode::Cancelled. The empty version is not listed by the SQLite storage and stays.
[[nodiscard]] core::Result<qint64> pruneOtherSnapshots(const maptype::IMapType& map,
                                                       const geo::IGeometryLonLatPolygon& area,
                                                       std::span<const quint8> zooms, const AppContext& app,
                                                       const std::stop_token& stop);

} // namespace cg::app
