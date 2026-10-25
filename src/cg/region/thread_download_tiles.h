// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/Download/u_ThreadDownloadTiles.pas
//
// One worker of an area download: walks the iterators of its part and downloads one tile at a time.
// FEAT-REG-018 (progress, pause, resume: the worker feeds the progress object).
//
// Lifetime contract of the CALLER (DR-1: legacy frees the thread object from the thread itself, here the
// object owns a std::jthread and the destructor joins it):
//  - the destructor must not run on the worker thread itself;
//  - it must not run before `cancel` was requested, unless the worker has finished (otherwise the join
//    waits for the whole area);
//  - the map (hence its download subsystem) must outlive the worker;
//  - on application shutdown `cancel` must be requested too: `appClosing` alone ends no wait of the loop
//    (LR-6).

#include "cg/geo/tile_iterator.h"
#include "cg/maptype/map_abilities_config.h"
#include "cg/maptype/map_type.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/tile_download_subsystem.h"
#include "cg/maptype/tile_request_task.h"
#include "cg/net/download_stats.h"
#include "cg/net/global_download_config.h"
#include "cg/region/download_task_provider.h"
#include "cg/region/region_process_progress_info_download.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_date.h"

#include <QtGlobal>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>

namespace cg::region {

// Legacy constants (:194-197), a test seam: the milliseconds of the four waits. Declared outside the class
// because a default member initializer of a nested struct cannot be used in a default argument of the class.
struct ThreadDownloadTilesTiming {
    quint32 pausePollMs = 100;
    quint32 banMs = 5000;
    quint32 proxyAuthMs = 10000;
    quint32 downloadErrorMs = 5000;
};

class ThreadDownloadTiles final {
public:
    using Timing = ThreadDownloadTilesTiming;

    // :153-240, legacy argument order. (ACancelNotifier, AOperationID) is `cancel`.
    ThreadDownloadTiles(int workerIndex, std::stop_token cancel,
                        std::shared_ptr<IRegionProcessProgressInfoDownloadInternal> progressInfo,
                        std::stop_token appClosing, maptype::IMapTypePtr mapType,
                        maptype::MapVersionRequestPtr versionForCheck,
                        storage::MapVersionInfoPtr versionForDownload,
                        std::shared_ptr<IDownloadTaskProvider> downloadTaskProvider,
                        std::shared_ptr<const net::IGlobalDownloadConfig> downloadConfig,
                        std::shared_ptr<net::DownloadInfo> downloadInfo, bool replaceExistTiles,
                        bool checkExistTileSize, bool checkExistTileDate, storage::TileDate replaceOlderDate,
                        bool secondLoadTne, bool checkTneOlderDate, storage::TileDate replaceTneOlderDate,
                        Timing timing = {});
    ~ThreadDownloadTiles(); // :242-267, then joins
    ThreadDownloadTiles(const ThreadDownloadTiles&) = delete;
    ThreadDownloadTiles& operator=(const ThreadDownloadTiles&) = delete;

    void start(); // TThread.Start: the constructor only prepares (legacy Create(True))

private:
    class Event; // manual-reset event with the result slot, thread_download_tiles.cpp

    void execute();                                 // :408-456
    void processTask(geo::ITileIterator& iterator); // :274-406
    [[nodiscard]] bool
    processResultAndCheckGotoNextTile(const maptype::ITileRequestResultPtr& result); // :500-575
    void sleepCancelable(quint32 ms);                                                // :269-272
    void onAppClosing();                                                             // :458-467
    void onTileDownloadFinish(const maptype::ITileRequestTask& task,
                              const maptype::ITileRequestResultPtr& result); // :469-478
    void prepareStrings();                                                   // :480-498
    void log(const QString& text, int level) const;

    int workerIndex_;
    std::stop_token cancel_;
    std::shared_ptr<IRegionProcessProgressInfoDownloadInternal> progressInfo_;
    maptype::IMapTypePtr mapType_;
    maptype::MapVersionRequestPtr versionForCheck_;
    storage::MapVersionInfoPtr versionForDownload_;
    std::shared_ptr<IDownloadTaskProvider> downloadTaskProvider_;
    std::shared_ptr<const net::IGlobalDownloadConfig> downloadConfig_;
    std::shared_ptr<net::DownloadInfo> downloadInfo_;
    bool replaceExistTiles_;
    bool checkExistTileSize_;
    bool checkExistTileDate_;
    storage::TileDate checkTileDate_;
    bool secondLoadTne_;
    bool checkTneOlderDate_;
    storage::TileDate replaceTneOlderDate_;
    Timing timing_;

    // The 16 texts of the unit (PrepareStrings); %1 / %2 placeholders as in the English source.
    QString resUserStop_, resProcessedFile_, resLoadProcessRepl_, resLoadProcess_, resFileBeCreateTime_,
        resTneBeCreateTime_, resFileBeCreateLen_, resAuthorization_, resWaitTime_, resBan_, resBadMime_,
        resTileNotExists_, resNoConnectionsToInternet_, resFileExistsShort_, resTneExistsShort_,
        resProcessFilesComplete_;

    std::atomic<bool> terminated_{false}; // TThread.Terminated
    std::unique_ptr<Event> finishEvent_;  // null when the constructor left early (legacy: nil)
    maptype::ITileRequestTaskFinishNotifierPtr taskFinishNotifier_;
    std::jthread thread_;

    // LAST: destroyed first, so every member onAppClosing touches is alive when a callback fires in the
    // constructor or runs on a foreign thread (the destructor of a stop_callback waits for a running one).
    std::optional<std::stop_callback<std::function<void()>>> appClosingCallback_;
    std::optional<std::stop_callback<std::function<void()>>> cancelCallback_;
};

} // namespace cg::region
