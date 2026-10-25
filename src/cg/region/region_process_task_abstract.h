// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/u_RegionProcessTaskAbstract.pas:37-113

#include "cg/geo/projection.h"
#include "cg/geo/tile_count.h"
#include "cg/region/region_process_progress_info.h"
#include "cg/region/region_process_task.h"

#include <QtGlobal>

#include <memory>
#include <stop_token>

namespace cg::geo {
class IGeometryLonLatPolygon;
}

namespace cg::region {

// NOLINTNEXTLINE(readability-identifier-naming): abstract class with logic (as config_element.h)
class RegionProcessTaskAbstract : public IRegionProcessTask {
public:
    ~RegionProcessTaskAbstract() override; // Destroy :87-94: tells the progress object the task is gone

protected:
    RegionProcessTaskAbstract(std::shared_ptr<IRegionProcessProgressInfoInternal> progressInfo,
                              std::shared_ptr<const geo::IGeometryLonLatPolygon> polygon,
                              std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory); // :73-85

    [[nodiscard]] geo::TileIteratorPtr
    makeTileIterator(const geo::ProjectionPtr& projection) const;          // :96-102
    void progressFormUpdateOnProgress(qint64 processed, qint64 toProcess); // :104-113
    [[nodiscard]] const std::stop_token& stopToken() const noexcept { return stop_; }
    [[nodiscard]] IRegionProcessProgressInfoInternal& progressInfo() const noexcept { return *progressInfo_; }
    [[nodiscard]] const std::shared_ptr<const geo::IGeometryLonLatPolygon>& polygLL() const noexcept {
        return polygon_;
    }

private:
    std::stop_token stop_;
    std::shared_ptr<IRegionProcessProgressInfoInternal> progressInfo_;
    std::shared_ptr<const geo::IGeometryLonLatPolygon> polygon_;
    std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory_;
};

} // namespace cg::region
