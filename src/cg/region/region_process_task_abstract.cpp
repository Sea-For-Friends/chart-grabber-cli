// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/region/region_process_task_abstract.h"

#include <QCoreApplication>
#include <QString>

#include <utility>

namespace cg::region {

RegionProcessTaskAbstract::RegionProcessTaskAbstract(
    std::shared_ptr<IRegionProcessProgressInfoInternal> progressInfo,
    std::shared_ptr<const geo::IGeometryLonLatPolygon> polygon,
    std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory)
    : stop_(progressInfo->stopToken()), // :80-81
      progressInfo_(std::move(progressInfo)), polygon_(std::move(polygon)),
      tileIteratorFactory_(std::move(tileIteratorFactory)) {}

RegionProcessTaskAbstract::~RegionProcessTaskAbstract() { // :87-94
    if (progressInfo_) {
        progressInfo_->finish();
        progressInfo_.reset();
    }
}

geo::TileIteratorPtr RegionProcessTaskAbstract::makeTileIterator(const geo::ProjectionPtr& projection) const {
    // :100 Assert(factory <> nil) is off in the shipped build; the factory is always given
    // (unreachable in legacy: frm_RegionProcess.pas:414, u_ExportProviderAbstract.pas:117); no register row.
    return tileIteratorFactory_->makeTileIterator(projection, polygon_); // :101
}

void RegionProcessTaskAbstract::progressFormUpdateOnProgress(qint64 processed, qint64 toProcess) {
    const double ratio =
        toProcess > 0 ? static_cast<double>(processed) / static_cast<double>(toProcess) : 0.0; // :110
    progressInfo_->setProcessedRatio(ratio);
    progressInfo_->setSecondLine(QCoreApplication::translate("cg::region", "Processed") + u' ' +
                                 QString::number(processed)); // :112
}

} // namespace cg::region
