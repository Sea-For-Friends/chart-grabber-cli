// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/Download/u_ProviderTilesDownload.pas:194-379 (headless half; the form :229-246 is
// ui)
//
// Starts the sessions and worker threads of an area download.
//
// Contract of the CALLER (docs/specs/region-download.md section 7):
//  - cancel (run.cancelNotifier->cancel()) before destroying a run whose worker has not finished: the
//    destructor of the thread joins it and would wait for the whole area;
//  - keep the maps set (and so the map) alive as long as any run exists;
//  - isPaused() without pause(): a run started paused waits for a human to call progressInfo->resume().
// Members of a run are declared so that the thread is destroyed FIRST.

#include "cg/core/log_ring_buffer.h"
#include "cg/core/operation_notifier.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/tile_count.h"
#include "cg/maptype/map_type_set.h"
#include "cg/net/download_stats.h"
#include "cg/net/global_download_config.h"
#include "cg/region/download_session.h"
#include "cg/region/download_task_provider.h"
#include "cg/region/region_process_progress_info_download.h"
#include "cg/region/thread_download_tiles.h"
#include "cg/region/tiles_download.h"

#include <QString>

#include <memory>
#include <stop_token>
#include <vector>

namespace cg::region {

// What the form and the self-freeing thread kept alive in legacy.
struct DownloadSessionRun {
    std::shared_ptr<IDownloadSession> session;
    std::shared_ptr<core::LogRingBuffer> log;                // (5000, 0), :210
    std::shared_ptr<core::OperationNotifier> cancelNotifier; // :214-219; cancel() ends the worker
    std::shared_ptr<RegionProcessProgressInfoDownload> progressInfo;
    std::unique_ptr<ThreadDownloadTiles> thread; // null if cancelled before start (:248); LAST: dies first
};

class ProviderTilesDownload {
public:
    ProviderTilesDownload(std::stop_token appClosing,
                          std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory,
                          std::shared_ptr<const maptype::IMapTypeSet> fullMapsSet,
                          std::shared_ptr<const geo::GeometryLonLatFactory> lonLatFactory,
                          std::shared_ptr<const geo::GeometryProjectedFactory> projectedFactory,
                          std::shared_ptr<const net::IGlobalDownloadConfig> downloadConfig,
                          std::shared_ptr<net::DownloadInfo> downloadInfo);

    // StartProcess :324-379; reads map and zooms from params, as legacy reads the frame (:333-335).
    // FEAT-REG-014 (split into N parts)
    [[nodiscard]] std::vector<DownloadSessionRun> startProcess(const PolygonPtr& polygon,
                                                               const TilesDownloadParams& params);

    // StartBySLS :274-322. FEAT-REG-019 (resume from .sls)
    [[nodiscard]] DownloadSessionRun startBySls(const QString& fileName, bool startPaused);

private:
    // StartSession :194-272 without the form
    [[nodiscard]] DownloadSessionRun startSession(std::shared_ptr<IDownloadSession> session,
                                                  std::shared_ptr<IDownloadTaskProvider> taskProvider,
                                                  std::shared_ptr<net::DownloadInfo> info, bool paused);

    std::stop_token appClosing_;
    std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory_;
    std::shared_ptr<const maptype::IMapTypeSet> fullMapsSet_;
    std::shared_ptr<const geo::GeometryLonLatFactory> lonLatFactory_;
    std::shared_ptr<const geo::GeometryProjectedFactory> projectedFactory_;
    std::shared_ptr<const net::IGlobalDownloadConfig> downloadConfig_;
    std::shared_ptr<net::DownloadInfo> downloadInfo_;
};

} // namespace cg::region
