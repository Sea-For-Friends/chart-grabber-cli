// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/RegionProcess/Download/fr_TilesDownload.pas:49-83, 205-241, 362-394, 428-529 (headless half; the
// frame's widgets go to ui)

#include "cg/region/tiles_download.h"

#include "cg/geo/geometry_factory.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/tile_download_subsystem.h"
#include "cg/maptype/tile_downloader_state.h"
#include "cg/net/tile_downloader_config.h"
#include "cg/storage/simple_tile_storage_config.h"

#include <algorithm>

namespace cg::region {

bool allowDownload(const maptype::IMapType& map) { // :428-431
    return map.storageConfig()->allowAdd() && map.tileDownloadSubsystem()->state()->getStatic()->enabled;
}

int maxSplitCount(const maptype::IMapType& map) { // :376
    return static_cast<int>(map.tileDownloaderConfig()->maxConnectToServerCount());
}

int clampSplitCount(const maptype::IMapType& map, int requested) {
    const int max = maxSplitCount(map);
    if (max <= 1) { // :389-391, split off
        return 1;
    }
    if (requested <
        2) { // sePartsCount.MinValue 2 (dfm); GetSplitCount returns 1 when the split is not checked
        return 1;
    }
    constexpr int kDfmMaxValue = 64;                 // sePartsCount.MaxValue in the dfm
    return std::min({requested, max, kDfmMaxValue}); // :385-388
}

std::optional<ZoomInfo> boundingBoxTileCount(quint8 zoom, const maptype::IMapType& map,
                                             const geo::IGeometryLonLatPolygon* polygon,
                                             const geo::GeometryProjectedFactory& projectedFactory) {
    // legacy tests AMapType <> nil (:220); the frame calls it only with the selected map (:255-262)
    if (polygon == nullptr) { // :226
        return std::nullopt;
    }
    const geo::ProjectionSetPtr projectionSet = map.projectionSet();
    projectionSet->validateZoom(zoom); // :222 on a copy
    const geo::ProjectionPtr& projection = projectionSet->zoom(zoom);
    std::vector<geo::DoublePoint> scratch;
    const auto projected = projectedFactory.createPolygonByLonLatPolygon(*projection, *polygon, scratch);
    if (!projected) { // :232
        return std::nullopt;
    }
    ZoomInfo info;
    info.pixelRect = geo::roundRect(projected->bounds(), geo::RectRounding::Outside); // :234
    info.tileRect = projection->pixelRect2TileRect(info.pixelRect);                   // :235
    info.tilesCount = static_cast<qint64>(info.tileRect.right - info.tileRect.left) *
                      static_cast<qint64>(info.tileRect.bottom - info.tileRect.top); // :236-237
    return info;
}

} // namespace cg::region
