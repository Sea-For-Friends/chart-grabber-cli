// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/Export/MBTiles/u_ExportTaskToMBTiles.pas:46-290
//
// FEAT-REG-069 (stage 0: the direct-copy branch only; the recompose branch returns Unsupported).
// docs/specs/region-export.md §2.6.

#include "cg/core/result.h"
#include "cg/region/export_task_abstract.h"
#include "cg/region/storage_export_to_mbtiles.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_storage.h"

#include <QString>
#include <QtGlobal>

#include <memory>
#include <span>

namespace cg::region {

class ExportTaskToMbtiles final : public ExportTaskAbstract {
public:
    // Create :101-161, legacy order minus the recompose-only AProjectionSetFactory, ABitmapTileSaver,
    // ABitmapTileProviderArr (held open).
    ExportTaskToMbtiles(std::shared_ptr<IRegionProcessProgressInfoInternal> progressInfo,
                        const QString& exportPath,
                        std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory,
                        std::shared_ptr<const geo::IGeometryLonLatPolygon> polygon,
                        std::span<const quint8> zoomArr, std::shared_ptr<storage::ITileStorage> tileStorage,
                        std::shared_ptr<const storage::MapVersionRequest> mapVersion, bool directTilesCopy,
                        bool useXYZScheme, bool makeTileMillCompatibility, const QString& name,
                        const QString& description, const QString& attribution, bool isLayer,
                        const QString& imgFormat, bool forceDropTarget, bool replaceTiles);

    [[nodiscard]] core::Result<void> processRegion() override; // :169-278

private:
    std::shared_ptr<storage::ITileStorage> tileStorage_;
    std::shared_ptr<const storage::MapVersionRequest> mapVersion_;
    bool directTilesCopy_;
    QString exportPath_; // the directory part with its trailing separator (:138)
    std::unique_ptr<SqliteStorageMbtilesBase> sqliteStorage_;
};

} // namespace cg::region
