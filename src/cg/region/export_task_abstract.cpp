// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/RegionProcess/Export/u_ExportTaskAbstract.pas
#include "cg/region/export_task_abstract.h"

#include <algorithm>
#include <utility>

namespace cg::region {

ExportTaskAbstract::ExportTaskAbstract(std::shared_ptr<IRegionProcessProgressInfoInternal> progressInfo,
                                       std::shared_ptr<const geo::IGeometryLonLatPolygon> polygon,
                                       std::span<const quint8> zooms,
                                       std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory)
    : RegionProcessTaskAbstract(std::move(progressInfo), std::move(polygon), std::move(tileIteratorFactory)) {
    // :70-73 the Asserts are off in the shipped build; the code that follows handles the same cases.
    // A filter, not a sort: only the first 24 entries are looked at (:74-76), an entry is kept when
    // it is below 24 and strictly greater than the last kept one (:78-93).
    const std::size_t sourceCount = std::min<std::size_t>(zooms.size(), 24);
    for (std::size_t i = 0; i < sourceCount; ++i) {
        const quint8 zoom = zooms[i];
        if (zoom < 24 && (zooms_.empty() || zooms_.back() < zoom)) {
            zooms_.push_back(zoom);
        }
    }
}

core::Result<void> ExportTaskAbstract::processRegion() {
    if (zooms_.empty()) { // :99-101, text verbatim (not translated in legacy)
        return core::fail(core::ErrorCode::ParseError, QStringLiteral("Please select at least one zoom"));
    }
    return {};
}

} // namespace cg::region
