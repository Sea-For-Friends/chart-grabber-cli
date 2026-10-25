// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/Export/u_ExportTaskAbstract.pas:34-102
//
// docs/specs/region-export.md §2.4.

#include "cg/core/result.h"
#include "cg/region/region_process_task_abstract.h"

#include <QtGlobal>

#include <memory>
#include <span>
#include <vector>

namespace cg::region {

// NOLINTNEXTLINE(readability-identifier-naming): abstract class with logic (as RegionProcessTaskAbstract)
class ExportTaskAbstract : public RegionProcessTaskAbstract {
public:
    // Legacy ProcessRegion :96-102 raises when no zoom is left; the port returns the error. A derived
    // class starts with it (= `inherited`).
    [[nodiscard]] core::Result<void> processRegion() override;

protected:
    ExportTaskAbstract(std::shared_ptr<IRegionProcessProgressInfoInternal> progressInfo,
                       std::shared_ptr<const geo::IGeometryLonLatPolygon> polygon,
                       std::span<const quint8> zooms,
                       std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory); // :53-94

    std::vector<quint8> zooms_; // FZooms :37
};

} // namespace cg::region
