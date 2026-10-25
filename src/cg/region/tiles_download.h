// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/Download/fr_TilesDownload.pas:49-83, 205-241, 362-394, 428-529 (headless half; the
// frame's widgets go to ui)
//
// The state of the "download tiles" frame as a record, and the three rules the frame computes from the map:
// whether the map may download, the cap of the split count, and the bounding-box tile count.

#include "cg/geo/geo_types.h"
#include "cg/maptype/map_type.h"
#include "cg/storage/tile_date.h"

#include <QString>
#include <QtGlobal>

#include <optional>
#include <vector>

namespace cg::geo {
class GeometryProjectedFactory;
class IGeometryLonLatPolygon;
} // namespace cg::geo

namespace cg::region {

// IRegionProcessParamsFrameTilesDownload (:49-83) plus the two frame interfaces it is read through
// (map: IRegionProcessParamsFrameOneMap, zooms: IRegionProcessParamsFrameZoomArray). Defaults are what a
// fresh frame returns: every check box off, invalid dates, no prefix.
struct TilesDownloadParams {
    maptype::IMapTypePtr mapType;       // GetMapType :472-475
    std::vector<quint8> zoomArray;      // GetZoomArray :509-512 (0-based, ascending)
    bool isStartPaused = false;         // GetIsStartPaused :453-456
    bool isCloseOnFinish = false;       // GetAutoCloseAtFinish :458-461; stored in the .sls only
    bool isIgnoreTne = false;           // GetIsIgnoreTne :433-436
    storage::TileDate loadTneOlderDate; // GetLoadTneOlderDate :463-470; invalid = legacy NaN (sub-option off)
    bool isReplace = false;             // GetIsReplace :438-441
    bool isReplaceIfDifSize = false;    // :443-446, read even when isReplace is false
    bool isReplaceIfOlder = false;      // :448-451, read even when isReplace is false
    storage::TileDate replaceDate;      // GetReplaceDate :477-480 (the frame sets it to Now in Init :521)
    int sessionAutosaveInterval = 0;    // GetSessionAutosaveInterval :482-489; 0 = off
    QString sessionAutosaveFilePrefix;  // GetSessionAutosaveFilePrefix :491-498
    int splitCount = 1;                 // GetSplitCount :500-507
};

// GetAllowDownload :428-431
[[nodiscard]] bool allowDownload(const maptype::IMapType& map);

// OnMapChange :362-394: VCount := MaxConnectToServerCount
[[nodiscard]] int maxSplitCount(const maptype::IMapType& map);

// What the frame can return from GetSplitCount (:500-507): the split is off (1) when the map allows one
// connection (:389-391) or fewer than two parts are requested; otherwise the value is clamped to
// sePartsCount.MaxValue := VCount (:385-388) and to the dfm maximum 64.
[[nodiscard]] int clampSplitCount(const maptype::IMapType& map, int requested);

struct ZoomInfo {
    geo::RectI tileRect;
    geo::RectI pixelRect;
    qint64 tilesCount = 0;
};

// GetZoomInfo :205-241 (FEAT-REG-010). The tile count of the bounding box, not polygon-exact.
// nullopt when polygon is null or the projected polygon is null.
[[nodiscard]] std::optional<ZoomInfo>
boundingBoxTileCount(quint8 zoom, const maptype::IMapType& map, const geo::IGeometryLonLatPolygon* polygon,
                     const geo::GeometryProjectedFactory& projectedFactory);

} // namespace cg::region
