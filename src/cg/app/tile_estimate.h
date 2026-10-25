// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: Src/RegionProcess/Download/frm_ProgressDownload.pas:395-408
// (GetLenEnd: size left = (downloadedSize / downloaded) x (total - processed), with the cache sample in place
// of the session counters), Src/RegionProcess/Download/fr_TilesDownload.pas:205-241 (GetZoomInfo, the bbox
// column; ported by T-0181), Src/RegionProcess/Download/u_ThreadDownloadTiles.pas:318-356 (the worker's tile
// classification). Ports: nothing. docs/specs/app-cli.md 3.7.
//
// Tile count and byte estimate before a download starts. Read-only towards the cache: LoadMode::WithoutData
// only.

#include "cg/core/result.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/value_to_string.h"
#include "cg/maptype/map_type.h"
#include "cg/storage/tile_date.h"

#include <QList>
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

struct ZoomEstimate {
    quint8 zoom = 0;      // 0-based
    qint64 tiles = 0;     // what the session shows as its total (polygon-exact)
    qint64 bboxTiles = 0; // the legacy frame's count (bounding box)
    double sharePresent = 0;
    double shareMarkedMissing = 0;
    double shareReplaced = 0; // of the present ones, the share a --replace download requests again
    double shareRetried = 0;  // of the sample, the markers a --retry-missing download requests again
    qint64 bytesToDownload = 0;
    bool assumed =
        false; // the mean size is --assume-tile-kb: no zoom of the job had a present tile in its sample
    qint64 bytesOnDisk =
        0; // T-0202: size when complete = all the area's tiles at this zoom that are not marked
           // missing, x mean (not what is on disk now: equals bytesToDownload on an empty cache)
};

// The legacy tile count of `area` at `zoom` (CalcTileCountInProjectedSinglePolygon, u_GeometryFunc.pas:734);
// 0 for an empty projected polygon. The download walk is budgeted by this number (T-0259).
[[nodiscard]] qint64 legacyTileCount(const geo::ProjectionPtr& projection,
                                     const geo::IGeometryLonLatPolygon& area, const AppContext& app);

// Per zoom: tiles, bbox tiles, a sample of at most ~500 tiles looked up in the cache, bytes to download.
// `replace` = --replace: a present tile is requested again (u_ThreadDownloadTiles.pas:322-325) unless
// `keepNewerThan` is valid (--replace-older-than, :328-332) and the tile's load date is not before it.
// `retryMissing` = --retry-missing: a TNE marker is requested again (:335-338) unless
// `retryMissingOlderThan` is valid (--retry-missing-older-than, :340-344) and the marker's load date is not
// before it.
// `assumeEmptyCache` (T-0243, weather-sources.md 5.1): no storage call; no tile counts as present or marked
// missing; every zoom is `assumed` and its bytes are tiles x assumeTileKb x 1024 (the estimate of a snapshot
// source, whose snapshot is not known before the download).
// A storage error from tileInfoEx is returned (storage errors end a legacy job too). Stop requested ->
// ErrorCode::Cancelled (the partial figures are not usable: the mean of the job needs every zoom).
[[nodiscard]] core::Result<std::vector<ZoomEstimate>>
estimateTiles(const maptype::IMapType& map, const geo::IGeometryLonLatPolygon& area,
              std::span<const quint8> zooms, const AppContext& app, int assumeTileKb, bool replace,
              const storage::TileDate& keepNewerThan, bool retryMissing,
              const storage::TileDate& retryMissingOlderThan, const std::stop_token& stop,
              bool assumeEmptyCache = false);

// S-13: false when every zoom has no tile at all (the caller refuses with exit 3).
[[nodiscard]] bool anyTile(const std::vector<ZoomEstimate>& estimates);

// stdout lines: header, one row per zoom (0-based zoom, then the 1-based "SAS z"), a total row. Byte figures
// go through dataSizeConvert and carry "~".
[[nodiscard]] QStringList estimateTable(const std::vector<ZoomEstimate>& estimates,
                                        const geo::ValueToStringConverter& converter);

// Right-aligns every cell to the widest cell of its column; columns are separated by two blanks. Shared with
// fillTable (fill_report.h).
[[nodiscard]] QStringList alignColumns(const QList<QStringList>& rows);

} // namespace cg::app
