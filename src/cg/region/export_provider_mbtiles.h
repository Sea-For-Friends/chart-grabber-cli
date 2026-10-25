// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/Export/MBTiles/u_ExportProviderMBTiles.pas:188-256 (PrepareTask). The provider's
// frame plumbing (GetProvider, PrepareBitmapTileProviders :154-186, the recompose arguments) is not ported:
// the recompose path is held open (FEAT-REG-069). docs/specs/region-export.md 2.7.

#include "cg/geo/geometry_lonlat.h"
#include "cg/geo/tile_count.h"
#include "cg/region/export_mbtiles.h"
#include "cg/region/region_process_progress_info.h"
#include "cg/region/region_process_task.h"

#include <memory>

namespace cg::region {

class ExportProviderMbtiles {
public:
    explicit ExportProviderMbtiles(std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory);

    [[nodiscard]] std::shared_ptr<IRegionProcessTask>
    prepareTask(const ExportMbtilesParams& p, std::shared_ptr<const geo::IGeometryLonLatPolygon> polygon,
                std::shared_ptr<IRegionProcessProgressInfoInternal> progressInfo) const;

private:
    std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory_;
};

} // namespace cg::region
