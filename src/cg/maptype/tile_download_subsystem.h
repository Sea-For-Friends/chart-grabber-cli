// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileDownloadSubsystem.pas, u_TileDownloadSubsystem.pas (with its private
// TRequestBuilderProviderInternal), and the maptype side of u_TileDownloaderSimple.pas:152-274 -- the
// per-task job (docs/specs/maptype.md 2.13.6, 3.1 R1-R4, net.md 2.15).
//
// One subsystem per map. Built from the ZMP only when the ZMP allows downloading: the sample predicates, the
// anti-ban, the checker, the request-builder factory, the result saver, the subsystem state, the download
// queue (capacity 256) and the provider that serves getRequest. A disabled-by-ZMP subsystem has none of them:
// its state says so and every call answers "nothing".
//
// Threads. getRequestTask / download / getRequest are called by the UI and by region workers; the jobs run on
// the queue's workers. The destructor must run on a thread that is not a worker of this queue (it joins
// them).
//
// Lifetime (R1). A queued job holds only shared_ptrs to the task and the saver -- never the subsystem, the
// queue or the factory -- so the last reference to the queue is the member below and ~DownloadQueue never
// joins the thread it runs on. The destructor first fires the destroy token (this cancels the builds of
// getRequest and, through the queue's stop, every transfer in flight), then drops the provider and joins the
// queue's workers, and only then do the saver, the factory and the storage go (member order, last = first
// destroyed). Jobs still waiting in the queue when the subsystem dies are dropped unfinished, as in legacy
// (TInterfaceQueue.Destroy): their owners must not wait for them after destroying the map.
// The saver unsubscribes from the storage state in its destructor but a callback already running on another
// thread is not waited for (core/notifier.cpp): whoever changes the storage's state must not do it
// concurrently with the destruction of this object (docs/specs/maptype.md 2.13.5 item 8).

#include "cg/core/content_type.h"
#include "cg/core/language_manager.h"
#include "cg/core/notifier.h"
#include "cg/geo/geo_types.h"
#include "cg/maptype/map_abilities_config.h"
#include "cg/maptype/tile_download_request_builder_config.h"
#include "cg/maptype/tile_download_request_builder_factory_pascal_script.h"
#include "cg/maptype/tile_download_result_saver_stuped.h"
#include "cg/maptype/tile_download_subsystem_state.h"
#include "cg/maptype/tile_request_task.h"
#include "cg/maptype/zmp_info.h"
#include "cg/net/download_queue.h"
#include "cg/net/download_stats.h"
#include "cg/net/downloader.h"
#include "cg/net/global_download_config.h"
#include "cg/net/retry_downloader.h"
#include "cg/net/tile_downloader_config.h"
#include "cg/script/script_host.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_storage.h"

#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <stop_token>

namespace cg::maptype {

// i_TileDownloadSubsystem.pas:32-59; parameter order as there.
class ITileDownloadSubsystem {
public:
    virtual ~ITileDownloadSubsystem() = default;

    // Null unless the subsystem is enabled (ZMP and state) and the tile lies inside the zoom's tile grid
    // (checkTilePosStrict). A zoom the projection set does not have throws std::out_of_range (legacy indexes
    // its zoom list, which raises). `checkTileSize` selects TileRequestWithSizeCheck.
    [[nodiscard]] virtual ITileRequestTaskPtr
    getRequestTask(std::stop_token softCancel, std::stop_token cancel,
                   ITileRequestTaskFinishNotifierPtr finishNotifier, geo::PointI tile, quint8 zoom,
                   const storage::MapVersionInfoPtr& version, bool checkTileSize) = 0;

    // The URL a tile would be fetched from ("copy tile URL"): nullopt unless the ZMP allows downloading AND
    // the request-builder factory is enabled (not the subsystem state: a map switched off in its abilities
    // still answers). Runs the builder (a whole script, nested HTTP included) on the calling thread under the
    // provider's request-builder lock; exceptions of the builder propagate.
    [[nodiscard]] virtual std::optional<net::DownloadRequest>
    getRequest(geo::PointI xy, quint8 zoom, const storage::MapVersionInfoPtr& version) = 0;

    // Queues the task when the subsystem is enabled (blocks while the queue is full); otherwise finishes it
    // with a null result at once, on the calling thread. A task that is not an ITileRequestTaskInternal is
    // ignored (legacy Supports()).
    virtual void download(const ITileRequestTaskPtr& task) = 0;

    [[nodiscard]] virtual std::shared_ptr<ITileDownloaderStateChangeable> state() const = 0;

    // T-0366 (port extension): loads the script of a JavaScript source now, so that a script that does not
    // load turns the state off with its reason before the first tile. No effect for any other source.
    virtual void checkRequestBuilder() {}
};

class RequestBuilderProviderInternal; // tile_download_subsystem.cpp

namespace detail {

// What the queue's SlotStateFactory makes for one slot (u_TileDownloaderList.pas:146-168): the slot's lazy
// request builder and its LastResponseInfo. Never shared between slots; touched only inside the callables of
// RetryDownloader::run (its run lock serialises two workers on one slot, net.md 2.15).
struct SlotState {
    ITileDownloadRequestBuilderPtr builder;
    std::shared_ptr<net::LastResponseInfo> lastResponse;
};

// The job the subsystem pushes for one task: the body of TTileDownloaderSimple.Download
// (u_TileDownloaderSimple.pas:152-274) plus the finishing that TTileRequestQueueProcessorThread.Execute does
// around it (:104-120). Exposed for the tests, which drive it with a scripted builder, saver and downloader.
// The task is finished exactly once: with the result, or with null when anything but the builder or the saver
// throws (the exception is then rethrown). `slotState` is a SlotState*; null is the same kind of failure.
// `queueStop` is the queue's hard cancel; it is merged with the task's own cancel into the transfer's token.
void runTileDownloadJob(const std::shared_ptr<ITileRequestTaskInternal>& task,
                        ITileDownloadResultSaver& saver, net::RetryDownloader& loop, void* slotState,
                        const std::stop_token& queueStop);

} // namespace detail

class TileDownloadSubsystem final : public ITileDownloadSubsystem {
public:
    // Legacy argument order (u_TileDownloadSubsystem.pas:226-258) where an argument exists; what the ZMP
    // knows is read from `zmp` (file name, enabled flag, projection set, samples, crop config, content-type
    // substitution, the provider holding params.txt and GetUrlScript.txt). Not taken, with the reason:
    // ProjectionSetFactory (unused in legacy's body), ImageResampler / Bitmap32StaticFactory /
    // DownloadResultFactory (saver arguments of the unported crop/cut half, T-0119), InvisibleBrowser (the
    // anti-ban fetches through the downloader factory, D-14), ThreadConfig (the queue has no thread-priority
    // input). `gc` and `tileDownloaderConfig` must outlive the subsystem's queue (the subsystem holds the
    // config by shared_ptr; `gc` is the application notifier). `contentTypes` is held by shared_ptr because
    // the saver keeps a reference to it. `appClosing`: once it stops, the destroy token fires (a token
    // already stopped at construction acts at once). Null arguments that legacy would dereference are
    // std::invalid_argument; a ZMP without params.txt / [PARAMS] (ZmpInfo cannot produce one) is
    // std::runtime_error.
    TileDownloadSubsystem(IZmpInfoPtr zmp, core::Notifier& gc, std::stop_token appClosing,
                          std::shared_ptr<core::ILanguageManager> languageManager,
                          std::shared_ptr<const net::IGlobalDownloadConfig> globalDownloadConfig,
                          std::shared_ptr<net::IDownloaderFactory> downloaderFactory,
                          storage::MapVersionFactoryPtr versionFactory,
                          std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig,
                          std::shared_ptr<TileDownloadRequestBuilderConfig> requestBuilderConfig,
                          std::shared_ptr<const core::ContentTypeRegistry> contentTypes,
                          std::shared_ptr<const ContentConverters> converters,
                          core::ContentTypeInfo saveContentType,
                          std::shared_ptr<MapAbilitiesConfig> mapAbilitiesConfig,
                          std::shared_ptr<const script::IProjConverterFactory> projFactory,
                          storage::ITileStoragePtr storage, int appId, const QString& logsPath);
    ~TileDownloadSubsystem() override;
    TileDownloadSubsystem(const TileDownloadSubsystem&) = delete;
    TileDownloadSubsystem& operator=(const TileDownloadSubsystem&) = delete;

    [[nodiscard]] ITileRequestTaskPtr getRequestTask(std::stop_token softCancel, std::stop_token cancel,
                                                     ITileRequestTaskFinishNotifierPtr finishNotifier,
                                                     geo::PointI tile, quint8 zoom,
                                                     const storage::MapVersionInfoPtr& version,
                                                     bool checkTileSize) override;
    [[nodiscard]] std::optional<net::DownloadRequest>
    getRequest(geo::PointI xy, quint8 zoom, const storage::MapVersionInfoPtr& version) override;
    void download(const ITileRequestTaskPtr& task) override;
    [[nodiscard]] std::shared_ptr<ITileDownloaderStateChangeable> state() const override;
    void checkRequestBuilder() override;

private:
    // Construction order = declaration order; destruction is the reverse, after the destructor body (which
    // drops the app-closing callback, the provider and the queue first).
    const IZmpInfoPtr zmp_;
    const storage::ITileStoragePtr
        storage_; // outlives the saver (it holds the storage and listens to its state)
    const std::shared_ptr<net::TileDownloaderConfig>
        tileDownloaderConfig_;                                            // the queue holds a reference to it
    const std::shared_ptr<const core::ContentTypeRegistry> contentTypes_; // the saver holds a reference to it
    std::stop_source destroySource_; // ports FDestroyNotifier / NextOperation
    const bool zmpDownloadEnabled_;
    std::shared_ptr<TileDownloadSubsystemState> state_;
    std::shared_ptr<ITileDownloadRequestBuilderFactory> factory_;
    std::shared_ptr<ITileDownloadResultSaver> saver_;
    std::shared_ptr<net::DownloadQueue> queue_;
    std::shared_ptr<RequestBuilderProviderInternal> provider_;
    // Last: constructed after everything it touches, destroyed (and, if running, waited for) before it.
    std::optional<std::stop_callback<std::function<void()>>> appClosingCallback_;
};

} // namespace cg::maptype
