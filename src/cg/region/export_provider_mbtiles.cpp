// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/RegionProcess/Export/MBTiles/u_ExportProviderMBTiles.pas (PrepareTask, see
// export_provider_mbtiles.h)
#include "cg/region/export_provider_mbtiles.h"

#include "cg/core/delphi_text.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/region/export_task_to_mbtiles.h"

#include <utility>

namespace cg::region {

ExportProviderMbtiles::ExportProviderMbtiles(
    std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory)
    : tileIteratorFactory_(std::move(tileIteratorFactory)) {}

// FEAT-REG-069 (contributed to: the direct-copy branch of the export task; the recompose branch is absent)
std::shared_ptr<IRegionProcessTask>
ExportProviderMbtiles::prepareTask(const ExportMbtilesParams& p,
                                   std::shared_ptr<const geo::IGeometryLonLatPolygon> polygon,
                                   std::shared_ptr<IRegionProcessProgressInfoInternal> progressInfo) const {
    // :210-218 the map of GetMapType (the map, else the overlay); its storage and version are taken only
    // when there is one.
    const maptype::IMapTypePtr map = mapType(p);
    std::shared_ptr<storage::ITileStorage> tileStorage;
    std::shared_ptr<const storage::MapVersionRequest> mapVersion;
    if (map) {
        mapVersion = map->versionRequest()->getStatic();
        tileStorage = map->tileStorage();
    }

    // :221-222 ForceDropTarget and ReplaceExistingTiles of the frame
    const bool replaceTiles = !p.forceDropTarget && p.replaceExistingTiles;

    // :233-255 the export task, without the recompose-only arguments (saver, tile providers). The text
    // parameters are read through the frame's getters, which apply Delphi's Trim (:333, :338, :343, :372).
    return std::make_shared<ExportTaskToMbtiles>(
        std::move(progressInfo), core::delphiTrim(p.path), tileIteratorFactory_, std::move(polygon), p.zooms,
        std::move(tileStorage), std::move(mapVersion), directTilesCopy(p), p.useXYZScheme,
        p.makeTileMillCompatibility, core::delphiTrim(p.name), core::delphiTrim(p.description),
        core::delphiTrim(p.attribution), isLayer(p), imgFormat(p), p.forceDropTarget, replaceTiles);
}

} // namespace cg::region
