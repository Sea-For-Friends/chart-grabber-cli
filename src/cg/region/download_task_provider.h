// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/Download/i_DownloadTaskProvider.pas, u_DownloadTaskProvider.pas
//
// Hands every worker thread of one download its per-zoom tile iterators, split by worker index.
// One provider is shared by all workers of a start; the data providers are prepared once, under a
// lock, by the first caller.

#include "cg/core/sync.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/tile_count.h"
#include "cg/geo/tile_iterator.h"
#include "cg/maptype/map_type.h"
#include "cg/region/download_session.h"

#include <QtGlobal>

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

namespace cg::geo {
class GeometryProjectedFactory;
} // namespace cg::geo

namespace cg::region {

// IDownloadTaskProvider (i_DownloadTaskProvider.pas:30-39).
class IDownloadTaskProvider {
public:
    virtual ~IDownloadTaskProvider() = default;
    // GetTasksList: tilesTotal and tasksList are outputs; tasksList is cleared first.
    virtual void getTasksList(int workerIndex, qint64& tilesTotal,
                              std::vector<geo::TileIteratorPtr>& tasksList) = 0;
};

// TDownloadTaskProvider (u_DownloadTaskProvider.pas:40-75). FEAT-REG-014 (split into N parts)
class DownloadTaskProvider final : public IDownloadTaskProvider {
public:
    DownloadTaskProvider(maptype::IMapTypePtr mapType, PolygonPtr polygon,
                         std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory,
                         std::shared_ptr<const geo::GeometryProjectedFactory> projectedFactory,
                         int partsCount, std::vector<quint8> zoomArray, quint8 lastProcessedZoom,
                         geo::PointI lastProcessedPoint, qint64 lastProcessedCount);

    void getTasksList(int workerIndex, qint64& tilesTotal,
                      std::vector<geo::TileIteratorPtr>& tasksList) override;

private:
    void prepareDataProviders(); // _PrepareDataProviders

    maptype::IMapTypePtr mapType_;
    PolygonPtr polygon_;
    std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory_;
    std::shared_ptr<const geo::GeometryProjectedFactory> projectedFactory_;
    int partsCount_;
    std::vector<quint8> zoomArray_;
    quint8 lastProcessedZoom_;
    geo::PointI lastProcessedPoint_;
    qint64 lastProcessedCount_;

    core::Mutex mutex_;
    std::deque<geo::TileIteratorDataProvider> dataProviders_; // not movable (holds a mutex)
    // Legacy reads FPrepared unlocked before taking the write lock (:186); atomic keeps that race-free.
    std::atomic<bool> prepared_{false};
};

} // namespace cg::region
