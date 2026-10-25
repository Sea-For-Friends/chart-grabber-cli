// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/RegionProcess/Download/u_ThreadDownloadTiles.pas
#include "cg/region/thread_download_tiles.h"

#include "cg/core/platform/thread_os.h"
#include "cg/maptype/tile_request_result.h"

#include <QCoreApplication>

#include <condition_variable>
#include <cstdlib>
#include <cxxabi.h>
#include <mutex>
#include <stdexcept>
#include <typeinfo>
#include <utility>

namespace cg::region {

namespace {

constexpr const char* kTrContext = "cg::region";

QString tr(const char* text) {
    return QCoreApplication::translate(kTrContext, text);
}

// E.ClassName + ': ' + E.Message. The Delphi class names cannot occur in the port (spec 12 item 5).
QString exceptionText(const std::exception& e) {
    int status = 0;
    char* demangled = // NOLINT(cppcoreguidelines-owning-memory): C allocation, freed below
        abi::__cxa_demangle(typeid(e).name(), nullptr, nullptr, &status);
    const QString name =
        (status == 0 && demangled != nullptr) ? QString::fromLatin1(demangled) : QStringLiteral("Exception");
    std::free(demangled); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    return name + QStringLiteral(": ") + QString::fromUtf8(e.what());
}

// `a >= b` on Delphi day counts. A NaN date (invalid here) compares false, as in Delphi; an invalid
// load date is 0.0 (storage::toDelphiDateTime), as legacy's "no info" load date.
bool dateAtLeast(const storage::TileDate& a, const storage::TileDate& b) {
    return b.isValid() && storage::toDelphiDateTime(a) >= storage::toDelphiDateTime(b);
}

} // namespace

// TEvent (manual reset) plus the result slot that onTileDownloadFinish writes and the worker reads: both
// are guarded by the event's mutex.
class ThreadDownloadTiles::Event {
public:
    void set() {
        {
            const std::lock_guard lock(mutex_);
            set_ = true;
        }
        cv_.notify_all();
    }
    void setWithResult(const maptype::ITileRequestResultPtr& result) {
        {
            const std::lock_guard lock(mutex_);
            result_ = result;
            set_ = true;
        }
        cv_.notify_all();
    }
    void reset() {
        const std::lock_guard lock(mutex_);
        set_ = false;
    }
    void clearResult() {
        const std::lock_guard lock(mutex_);
        result_.reset();
    }
    [[nodiscard]] maptype::ITileRequestResultPtr result() const {
        const std::lock_guard lock(mutex_);
        return result_;
    }
    void wait() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return set_; });
    }
    void waitFor(quint32 ms) {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return set_; });
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool set_ = false;
    maptype::ITileRequestResultPtr result_;
};

ThreadDownloadTiles::ThreadDownloadTiles(
    int workerIndex, std::stop_token cancel,
    std::shared_ptr<IRegionProcessProgressInfoDownloadInternal> progressInfo, std::stop_token appClosing,
    maptype::IMapTypePtr mapType, maptype::MapVersionRequestPtr versionForCheck,
    storage::MapVersionInfoPtr versionForDownload,
    std::shared_ptr<IDownloadTaskProvider> downloadTaskProvider,
    std::shared_ptr<const net::IGlobalDownloadConfig> downloadConfig,
    std::shared_ptr<net::DownloadInfo> downloadInfo, bool replaceExistTiles, bool checkExistTileSize,
    bool checkExistTileDate, storage::TileDate replaceOlderDate, bool secondLoadTne, bool checkTneOlderDate,
    storage::TileDate replaceTneOlderDate, Timing timing)
    : workerIndex_(workerIndex), cancel_(std::move(cancel)), progressInfo_(std::move(progressInfo)),
      mapType_(std::move(mapType)), versionForCheck_(std::move(versionForCheck)),
      versionForDownload_(std::move(versionForDownload)),
      downloadTaskProvider_(std::move(downloadTaskProvider)), downloadConfig_(std::move(downloadConfig)),
      downloadInfo_(std::move(downloadInfo)), replaceExistTiles_(replaceExistTiles),
      checkExistTileSize_(checkExistTileSize), checkExistTileDate_(checkExistTileDate),
      checkTileDate_(std::move(replaceOlderDate)), secondLoadTne_(secondLoadTne),
      checkTneOlderDate_(checkTneOlderDate), replaceTneOlderDate_(std::move(replaceTneOlderDate)),
      timing_(timing) { // :153-240
    if (!mapType_) {
        terminated_ =
            true; // :211-214, unreachable in legacy (u_ProviderTilesDownload.pas:250); no register row
        return;
    }
    if (!mapType_->abilities()->useDownload()) {
        log(QStringLiteral("Download of map %1 disabled by map params")
                .arg(mapType_->guiConfig()->name()->value()),
            10);
        terminated_ = true;
        return;
    }
    const auto state = mapType_->tileDownloadSubsystem()->state()->getStatic();
    if (!state->enabled) {
        log(QStringLiteral("Download of map %1 disabled. Reason: %2")
                .arg(mapType_->guiConfig()->name()->value(), state->disableReason),
            10);
        terminated_ = true;
        return;
    }

    prepareStrings();

    finishEvent_ = std::make_unique<Event>();

    taskFinishNotifier_ = std::make_shared<maptype::TileRequestTaskFinishNotifier>(
        [this](const maptype::ITileRequestTask& task, const maptype::ITileRequestResultPtr& result) {
            onTileDownloadFinish(task, result);
        });

    // A token that is already stopped runs the callback at once: that is what :237-239 does.
    appClosingCallback_.emplace(std::move(appClosing), [this] { onAppClosing(); });
    cancelCallback_.emplace(cancel_, [this] { onAppClosing(); });
}

ThreadDownloadTiles::~ThreadDownloadTiles() { // :242-267 plus the join (DR-1)
    if (taskFinishNotifier_) {
        taskFinishNotifier_->setEnabled(false);
    }
    if (finishEvent_) {
        finishEvent_->clearResult();
        finishEvent_->set();
    }
    // A running callback is waited for by the destructor of its stop_callback.
    cancelCallback_.reset();
    appClosingCallback_.reset();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ThreadDownloadTiles::start() {
    thread_ = std::jthread([this] { execute(); });
}

void ThreadDownloadTiles::log(const QString& text, int level) const {
    progressInfo_->log()->append(text, level);
}

void ThreadDownloadTiles::sleepCancelable(quint32 ms) { // :269-272
    finishEvent_->waitFor(ms);
}

void ThreadDownloadTiles::processTask(geo::ITileIterator& iterator) { // :274-406
    const quint8 zoom = mapType_->projectionSet()->suitableZoom(*iterator.tilesRect().projection());
    progressInfo_->setZoom(zoom);

    const auto tileStorage = mapType_->tileStorage();
    const auto subsystem = mapType_->tileDownloadSubsystem();

    geo::PointI tile;
    while (iterator.next(tile)) {
        bool gotoNextTile = false;
        while (!gotoNextTile) {
            finishEvent_->reset();
            if (progressInfo_->needPause()) { // FEAT-REG-013
                progressInfo_->setPaused();
                log(resUserStop_, 10);
                while (progressInfo_->needPause()) {
                    if (cancel_.stop_requested()) {
                        return;
                    }
                    sleepCancelable(timing_.pausePollMs);
                }
                progressInfo_->setStarted();
            }
            if (cancel_.stop_requested()) {
                return;
            }

            const QString processingTileMsg =
                QStringLiteral("[z%1/x%2/y%3]\r\n%4")
                    .arg(QString::number(zoom + 1), QString::number(tile.x()), QString::number(tile.y()),
                         mapType_->getTileShowName(tile, zoom,
                                                   versionForDownload_)); // one pass: '%' in a name is safe
            log(resProcessedFile_.arg(processingTileMsg), 0);

            // We are only interested in the tile metadata, so LoadMode::AsIs is used here. A Result error
            // is legacy's raised exception, outside the inner try (:318): execute's handler logs it.
            // *versionForCheck_ is dereferenced as legacy passes it: unreachable in legacy as null
            // (u_ProviderTilesDownload.pas:222-250); no register row.
            const auto tileInfo =
                tileStorage->tileInfoEx(tile, zoom, *versionForCheck_, storage::LoadMode::AsIs);
            if (!tileInfo) {
                throw std::runtime_error(tileInfo.error().what().toStdString());
            }

            // FEAT-REG-011 (overwrite, by size, by date), FEAT-REG-012 (retry TNE, by date)
            if (tileInfo->isExists() || tileInfo->isExistsTne()) {
                if (tileInfo->isExists()) {
                    if (replaceExistTiles_) {
                        log(resLoadProcessRepl_, 0);
                    } else {
                        log(resFileExistsShort_, 0);
                        gotoNextTile = true;
                    }
                    if (!gotoNextTile) {
                        if (checkExistTileDate_ && dateAtLeast(tileInfo->loadDate, checkTileDate_)) {
                            // skip (existing tile is newer than required)
                            log(resFileBeCreateTime_, 0);
                            gotoNextTile = true;
                        }
                    }
                } else {
                    if (secondLoadTne_) {
                        log(resLoadProcess_, 0);
                    } else {
                        log(resTneExistsShort_, 0);
                        gotoNextTile = true;
                    }
                    if (!gotoNextTile) {
                        if (checkTneOlderDate_ && dateAtLeast(tileInfo->loadDate, replaceTneOlderDate_)) {
                            // skip (existing tne is newer than required)
                            log(resTneBeCreateTime_, 0);
                            gotoNextTile = true;
                        }
                    }
                }
            } else {
                log(resLoadProcess_, 0);
            }

            if (gotoNextTile) {
                // DEVIATION: the D row of T-0356 (counts only). A tile present in this frame says it is
                // served; a TNE row of it says it is not served
                progressInfo_->noteFrameAnswer(tileInfo->isExists() ? FrameAnswer::Other
                                                                    : FrameAnswer::Missing);
                progressInfo_->addProcessedTile(tile);
            } else {
                try { // download tile
                    // FEAT-REG-020: version for download. Both cancel arguments are the same token
                    // (:432-433).
                    const auto requestTask =
                        subsystem->getRequestTask(cancel_, cancel_, taskFinishNotifier_, tile, zoom,
                                                  versionForDownload_, checkExistTileSize_);
                    if (requestTask) {
                        finishEvent_->clearResult();
                        subsystem->download(requestTask); // may finish the task on this thread
                        if (cancel_.stop_requested()) {
                            return;
                        }
                        finishEvent_->wait();
                        if (cancel_.stop_requested()) {
                            return;
                        }
                        gotoNextTile = processResultAndCheckGotoNextTile(finishEvent_->result());
                        if (cancel_.stop_requested()) {
                            return;
                        }
                        if (gotoNextTile) {
                            progressInfo_->addProcessedTile(tile);
                        }
                    } else {
                        log(QStringLiteral("Error: Downloading is disabled for this map!"), 0);
                        gotoNextTile = false;
                        progressInfo_->setNeedPause(true);
                    }
                } catch (const std::exception& e) {
                    log(exceptionText(e), 0);
                    gotoNextTile = false;
                    progressInfo_->setNeedPause(true);
                }
            }
            if (cancel_.stop_requested()) {
                return;
            }
        }
        if (cancel_.stop_requested()) {
            return;
        }
    }
}

void ThreadDownloadTiles::execute() { // :408-456
    struct Finish { // the `finally`; no finish reason: docs/specs/region-download.md OQ-5, not ruled
        IRegionProcessProgressInfoDownloadInternal& progress;
        ~Finish() { progress.finish(); }
    } finally{*progressInfo_};

    if (terminated_) {
        return;
    }

    core::platform::setCurrentThreadName(QStringLiteral("TThreadDownloadTiles"));

    try {
        qint64 tilesTotal = 0;
        std::vector<geo::TileIteratorPtr> tasksList;
        downloadTaskProvider_->getTasksList(workerIndex_, tilesTotal, tasksList);

        if (cancel_.stop_requested()) {
            return;
        }

        progressInfo_->setTotalToProcess(tilesTotal);

        for (const auto& iterator : tasksList) {
            processTask(*iterator);
            if (cancel_.stop_requested()) {
                return;
            }
        }

        log(resProcessFilesComplete_, 0);
    } catch (const std::exception& e) {
        log(exceptionText(e), 10);
    }
}

void ThreadDownloadTiles::onAppClosing() { // :458-467
    if (taskFinishNotifier_) {
        taskFinishNotifier_->setEnabled(false);
    }
    terminated_ = true;
    if (finishEvent_) {
        finishEvent_->set();
    }
}

void ThreadDownloadTiles::onTileDownloadFinish(const maptype::ITileRequestTask& /*task*/,
                                               const maptype::ITileRequestResultPtr& result) { // :469-478
    if (finishEvent_) {
        finishEvent_->setWithResult(result);
    }
}

void ThreadDownloadTiles::prepareStrings() { // :480-498
    resUserStop_ = tr("Paused by user...");
    resProcessedFile_ = tr("Processing tile: %1 ...");
    resLoadProcessRepl_ = tr("Downloading with overwrite...");
    resLoadProcess_ = tr("Downloading...");
    resFileBeCreateTime_ = tr("The tile is newer than the entered age, skipping.");
    resTneBeCreateTime_ = tr("The tne is newer than the entered age, skipping.");
    resFileBeCreateLen_ = tr("The tile size is equal to the existing one, skipping.");
    resAuthorization_ = tr("Proxy authorization error");
    resWaitTime_ = tr("Wait %1 seconds...");
    resBan_ = tr("Most likely you've been banned by the server!");
    resBadMime_ = tr("Server returned unexpected type \"%1\"");
    resTileNotExists_ = tr("Tile is not found!");
    resNoConnectionsToInternet_ = tr("Error connecting to server");
    resFileExistsShort_ = tr("The tile already exists");
    resTneExistsShort_ = tr("The tne already exists");
    resProcessFilesComplete_ = tr("The task is completed!");
}

bool ThreadDownloadTiles::processResultAndCheckGotoNextTile(
    const maptype::ITileRequestResultPtr& result) { // :500-575
    const auto* withDownload =
        dynamic_cast<const maptype::ITileRequestResultWithDownloadResult*>(result.get());
    if (withDownload != nullptr) {
        finishEvent_->reset(); // :511, so that the waits below last their full time
        const net::DownloadResult& downloadResult = withDownload->downloadResult();
        // DEVIATION D-163 (T-0271): the runner stops a run after many connection errors in a row; legacy
        // (u_ThreadDownloadTiles.pas:550-563) skips every such tile and finishes. Return values stay legacy.
        progressInfo_->noteDownloadAnswer(downloadResult.kind == net::ResultKind::UnknownError ||
                                          downloadResult.kind == net::ResultKind::NoConnectToServer ||
                                          downloadResult.kind == net::ResultKind::LoadError);
        // DEVIATION: the D row of T-0356 (counts only; return values stay legacy). The class "the frame is
        // not served yet": 404 / the empty tile (DataNotExists) is Missing, the ban tile and HTTP 5xx are
        // Unavailable; a connection that gave no answer (statusCode 0) is not in the class.
        const bool http5xx = downloadResult.kind == net::ResultKind::LoadError &&
                             downloadResult.statusCode >= 500 && downloadResult.statusCode <= 599;
        progressInfo_->noteFrameAnswer(
            downloadResult.kind == net::ResultKind::DataNotExists         ? FrameAnswer::Missing
            : (downloadResult.kind == net::ResultKind::Banned || http5xx) ? FrameAnswer::Unavailable
                                                                          : FrameAnswer::Other);
        switch (downloadResult.kind) {
        case net::ResultKind::Ok: {
            bool gotoNext = false;
            if (const auto* resultError =
                    dynamic_cast<const maptype::ITileRequestResultError*>(result.get())) {
                // tile downloaded successfully, but was not saved
                log(QStringLiteral("Error: ") + resultError->errorText(), 0);
                progressInfo_->setNeedPause(true);
            } else {
                // tile downloaded and saved successfully
                log(QStringLiteral("(Ok!)"), 0);
                progressInfo_->addDownloadedTile(result->request()->tile(),
                                                 static_cast<quint32>(downloadResult.data.size()));
                gotoNext = true;
            }
            downloadInfo_->add(1, static_cast<quint64>(downloadResult.data.size()));
            return gotoNext;
        }
        case net::ResultKind::NotNecessary:
            // same file size - assuming the tiles are identical
            log(resFileBeCreateLen_, 0);
            progressInfo_->addNotNecessaryTile(result->request()->tile());
            return true;
        case net::ResultKind::ProxyError:
            log(resAuthorization_ + QStringLiteral("\r\n") + resWaitTime_.arg(timing_.proxyAuthMs / 1000),
                10);
            sleepCancelable(timing_.proxyAuthMs);
            return false;
        case net::ResultKind::Banned:
            log(resBan_ + QStringLiteral("\r\n") + resWaitTime_.arg(timing_.banMs / 1000), 10);
            sleepCancelable(timing_.banMs);
            return false;
        case net::ResultKind::BadContentType:
            log(resBadMime_.arg(QString::fromLatin1(downloadResult.contentType)), 1);
            return true;
        case net::ResultKind::DataNotExists:
            log(resTileNotExists_, 1);
            return true;
        default:
            break;
        }
        if (maptype::isDownloadErrorKind(downloadResult.kind)) {
            // LR-7: the two branches look exchanged, reproduced
            if (downloadResult.kind == net::ResultKind::NoConnectToServer) {
                log(downloadResult.text + QStringLiteral("\r\n") +
                        resWaitTime_.arg(timing_.downloadErrorMs / 1000),
                    10);
                sleepCancelable(timing_.downloadErrorMs);
                return false;
            }
            log(resNoConnectionsToInternet_ + QStringLiteral("\r\n") +
                    resWaitTime_.arg(timing_.downloadErrorMs / 1000),
                10);
            sleepCancelable(timing_.downloadErrorMs);
            return downloadConfig_->isGoNextTileIfDownloadError();
        }
        log(QStringLiteral("Error: Unknown download result!"), 10);
        return false;
    }
    // DEVIATION D-164 (T-0271, L-269): no download result (a request that failed before one existed) and a
    // null result count as processed; legacy (u_ThreadDownloadTiles.pas:568-574) writes no log line, the
    // port writes one unless the request was canceled.
    if (dynamic_cast<const maptype::ITileRequestResultCanceled*>(result.get()) == nullptr) {
        if (const auto* resultError = dynamic_cast<const maptype::ITileRequestResultError*>(result.get())) {
            log(QStringLiteral("Error: ") + resultError->errorText(), 10);
        } else {
            log(QStringLiteral("Error: Unknown download result!"), 10);
        }
    }
    return dynamic_cast<const maptype::ITileRequestResultCanceled*>(result.get()) == nullptr;
}

} // namespace cg::region
