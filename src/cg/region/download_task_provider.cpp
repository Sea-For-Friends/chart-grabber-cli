// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/region/download_task_provider.h"

#include "cg/geo/geometry_factory.h"

#include <optional>
#include <stdexcept>
#include <utility>

namespace cg::region {

// u_DownloadTaskProvider.pas:93-123 (the Asserts :104-106 are off in the shipped build)
DownloadTaskProvider::DownloadTaskProvider(
    maptype::IMapTypePtr mapType, PolygonPtr polygon,
    std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory,
    std::shared_ptr<const geo::GeometryProjectedFactory> projectedFactory, int partsCount,
    std::vector<quint8> zoomArray, quint8 lastProcessedZoom, geo::PointI lastProcessedPoint,
    qint64 lastProcessedCount)
    : mapType_(std::move(mapType)), polygon_(std::move(polygon)),
      tileIteratorFactory_(std::move(tileIteratorFactory)), projectedFactory_(std::move(projectedFactory)),
      partsCount_(partsCount), zoomArray_(std::move(zoomArray)), lastProcessedZoom_(lastProcessedZoom),
      lastProcessedPoint_(lastProcessedPoint), lastProcessedCount_(lastProcessedCount) {}

// u_DownloadTaskProvider.pas:125-168
void DownloadTaskProvider::prepareDataProviders() {
    core::WriteLock lock(mutex_);
    if (prepared_) {
        return;
    }

    // unreachable in legacy (u_ProviderTilesDownload.pas:304, :338 pass a non-nil polygon); no register row
    if (!polygon_) {
        throw std::runtime_error("Polygon does not exist!");
    }

    for (const quint8 zoom : zoomArray_) {
        const geo::ProjectionPtr projection = mapType_->projectionSet()->zoom(zoom);
        std::vector<geo::DoublePoint> scratch;
        auto projected = projectedFactory_->createPolygonByLonLatPolygon(*projection, *polygon_, scratch);
        dataProviders_.emplace_back(projection, std::move(projected), partsCount_);
    }

    prepared_ = true;
}

// u_DownloadTaskProvider.pas:170-244
void DownloadTaskProvider::getTasksList(int workerIndex, qint64& tilesTotal,
                                        std::vector<geo::TileIteratorPtr>& tasksList) {
    if (!prepared_) {
        prepareDataProviders();
    }

    tilesTotal = 0;
    tasksList.clear();

    for (const geo::TileIteratorDataProvider& provider : dataProviders_) {
        if (workerIndex >= provider.partsCount()) {
            continue;
        }

        const quint8 zoom = provider.projection()->zoom();

        // Start point is always a real tile position: the nullopt branch is unreachable in legacy
        // (u_DownloadTaskProvider.pas:213, u_ProviderTilesDownload.pas:345); no register row.
        geo::PointI startPoint = provider.startPoint(workerIndex);
        qint64 tilesToProcess = provider.tilesToProcess(workerIndex);

        tilesTotal += tilesToProcess;

        if (zoom >= lastProcessedZoom_) {
            bool seekToLastProcessedPoint = false;
            if (zoom == lastProcessedZoom_) {
                if (lastProcessedPoint_.x() >= 0 && lastProcessedPoint_.y() >= 0) {
                    startPoint = lastProcessedPoint_;
                    if (lastProcessedCount_ > 0) {
                        tilesToProcess -= lastProcessedCount_;
                        // DEVIATION (T-0226, deviations.md): the discarded next() below spends one unit of
                        // the budget in legacy (u_DownloadTaskProvider.pas:212-237, L-265), so the last tile
                        // of the part is never requested; one more unit is given here. Only with a count: a
                        // point without one (count <= 0) cannot say how much of the part is done, and +1
                        // would exceed the area total of a one-part rect iterator.
                        tilesToProcess += 1;
                    }
                    seekToLastProcessedPoint = true;
                }
            }

            geo::TileIteratorPtr iterator = tileIteratorFactory_->makeTileIterator(
                provider.projection(), polygon_, tilesToProcess, std::optional<geo::PointI>{startPoint});

            if (seekToLastProcessedPoint) {
                // The seek consumes one unit of the budget (compensated above, DEVIATION (T-0226,
                // deviations.md)). The Asserts :233-235 are off; the tile is discarded.
                geo::PointI discarded;
                (void)iterator->next(discarded);
            }

            tasksList.push_back(std::move(iterator));
        }
    }
}

} // namespace cg::region
