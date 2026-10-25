// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: Src/RegionProcess/Download/u_ThreadDownloadTiles.pas:318-356 (the
// worker's tile-info classification as ported in region/thread_download_tiles.cpp processTask): exists / TNE
// marker / neither. Ports: nothing. docs/specs/app-cli.md 3.8.
//
// What the cache holds for the area after a download. Read-only: LoadMode::WithoutData only.

#include "cg/core/result.h"
#include "cg/geo/value_to_string.h"
#include "cg/maptype/map_type.h"

#include <QStringList>
#include <QtGlobal>

#include <span>
#include <stop_token>
#include <vector>

namespace cg::geo {
class IGeometryLonLatPolygon;
}

namespace cg::app {

class AppContext;

struct ZoomFill {
    quint8 zoom = 0;  // 0-based
    qint64 tiles = 0; // number of tiles iterated
    qint64 present = 0;
    qint64 markedMissing = 0;
    qint64 missing = 0;
    qint64 bytes = 0; // sum of the sizes of the present tiles
};

// A full pass per zoom, one tileInfoEx per tile. A storage error is returned; stop requested ->
// ErrorCode::Cancelled.
[[nodiscard]] core::Result<std::vector<ZoomFill>>
fillReport(const maptype::IMapType& map, const geo::IGeometryLonLatPolygon& area,
           std::span<const quint8> zooms, const AppContext& app, const std::stop_token& stop);

// stdout lines: header, one row per zoom, a total row.
[[nodiscard]] QStringList fillTable(const std::vector<ZoomFill>& fills,
                                    const geo::ValueToStringConverter& converter);

} // namespace cg::app
