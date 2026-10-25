// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_download_subsystem.h"

#include "cg/maptype/download_checker_stuped.h"
#include "cg/maptype/predicate_by_static_sample_list.h"
#include "cg/maptype/tile_download_request_builder_factory_js.h"
#include "cg/maptype/tile_download_request_builder_lazy.h"
#include "cg/maptype/tile_request.h"
#include "cg/maptype/tile_request_result.h"
#include "cg/net/anti_ban.h"
#include "cg/net/downloader_ttl.h"
#include "cg/net/retry_downloader.h"

#include <QByteArray>

#include <exception>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <utility>

namespace cg::maptype {

namespace {

constexpr auto kParamsFileName = u"params.txt";
constexpr auto kParamsSection = u"PARAMS";
constexpr auto kScriptFileName = u"GetUrlScript.txt";  // u_TileDownloadSubsystem.pas:59
constexpr auto kJsScriptFileName = u"GetUrlScript.js"; // T-0225: SourceJsonContainer

// The anti-ban's preload-page fetch (D-14, in place of the invisible browser): every call builds a fresh
// downloader from the factory, so no curl session is ever used from two worker threads (CurlSession is
// single-thread) and the fetch is as independent of the slots as a browser would be.
class PerCallDownloader final : public net::IDownloader {
public:
    PerCallDownloader(std::shared_ptr<net::IDownloaderFactory> factory, net::DownloaderOptions options)
        : factory_(std::move(factory)), options_(std::move(options)) {}
    net::DownloadResult doRequest(const net::DownloadRequest& request, std::stop_token stop) override {
        return factory_->build(options_)->doRequest(request, std::move(stop));
    }

private:
    std::shared_ptr<net::IDownloaderFactory> factory_;
    net::DownloaderOptions options_;
};

// TTileDownloaderList.CreateDownloader / TRequestBuilderProviderInternal.DoInit read the two flags the same
// way.
net::DownloaderOptions downloaderOptions(const net::TileDownloaderConfig& config) {
    const std::shared_ptr<const net::TileDownloaderSettings> settings = config.settings();
    net::DownloaderOptions options;
    options.allowCookies = settings->allowUseCookie;
    options.tryDetectContentType = settings->detectMimeType;
    return options;
}

std::shared_ptr<net::TtlDownloader> makeTtlDownloader(core::Notifier& gc,
                                                      const std::shared_ptr<net::IDownloaderFactory>& factory,
                                                      const net::TileDownloaderConfig& config) {
    auto ttl = std::make_shared<net::TtlDownloader>(factory, downloaderOptions(config));
    ttl->subscribeGcTick(gc);
    return ttl;
}

using TaskInternalPtr = std::shared_ptr<ITileRequestTaskInternal>;

} // namespace

namespace detail {

// The body of TTileDownloaderSimple.Download (u_TileDownloaderSimple.pas:152-274) plus the finishing that
// TTileRequestQueueProcessorThread.Execute does around it (:104-120). The task is finished exactly once:
// `task->setFinished(result)` as the last statement, outside the try, or `setFinished(nullptr); throw;` in
// the catch that covers everything before it. No early return inside the try, no scope guard (setFinished
// may throw: a finish subscriber's exception must leave the job, not be answered by a second notification).
void runTileDownloadJob(const TaskInternalPtr& task, ITileDownloadResultSaver& saver,
                        net::RetryDownloader& loop, void* slotPointer, const std::stop_token& queueStop) {
    ITileRequestResultPtr result; // legacy VResult := nil
    try {
        auto* const slot = static_cast<SlotState*>(slotPointer);
        if (slot == nullptr || !slot->builder || !slot->lastResponse) {
            throw std::logic_error("TileDownloadSubsystem: the job has no slot state");
        }
        // Two tokens (rule 2): hard = the queue's stop OR the task's cancel; soft = the task's soft cancel
        // alone.
        std::stop_source hardSource;
        const auto stopHard = [&hardSource] { hardSource.request_stop(); };
        const std::stop_callback queueCallback(queueStop, stopHard);
        const std::stop_callback taskCallback(task->cancel(), stopHard);
        const std::stop_token hard = hardSource.get_token();
        const std::stop_token soft = task->softCancel();

        QString buildError;
        std::optional<net::DownloadRequest> lastRequest;
        ITileRequestResultPtr saved;
        std::exception_ptr saveFailure;

        // :183-207. Must not throw (RetryDownloader contract): the exception becomes the text of the result.
        const net::BuildFn build = [&]() -> std::optional<net::DownloadRequest> {
            try {
                lastRequest = slot->builder->buildRequest(task->tileRequest(), slot->lastResponse, hard);
                if (!lastRequest) {
                    buildError = QStringLiteral("Tile does not exist"); // :205
                }
            } catch (const std::exception& e) {
                lastRequest.reset();
                buildError = QString::fromUtf8(e.what()); // :194, E.Message
            }
            return lastRequest;
        };

        // :234-253. Must not throw, so the loop reaches its `until` test after a failed save (rule 4). What
        // legacy lets out of Download here (the result constructors throw for a request without a tile
        // source) is kept in saveFailure and rethrown after run().
        const net::SaveFn save = [&](const net::DownloadResult& downloadResult) {
            try {
                saved.reset();
                try {
                    saved = saver.saveDownloadResult(*lastRequest, downloadResult); // :235
                } catch (const std::exception& e) {
                    saved = std::make_shared<const TileRequestResultErrorAfterDownloadRequest>(
                        *lastRequest, downloadResult, QString::fromUtf8(e.what())); // :236-243
                }
                if (!saved) { // :244-253
                    if (isDownloadErrorKind(downloadResult.kind)) {
                        saved = std::make_shared<const TileRequestResultDownloadError>(*lastRequest,
                                                                                       downloadResult);
                    } else {
                        saved = std::make_shared<const TileRequestResultErrorAfterDownloadRequest>(
                            *lastRequest, downloadResult, QStringLiteral("Unknown error")); // :251
                    }
                }
            } catch (...) {
                saveFailure = std::current_exception();
            }
        };

        const net::RetryOutcome outcome = loop.run(build, *slot->lastResponse, save, soft, hard);
        if (saveFailure) {
            std::rethrow_exception(saveFailure);
        }
        switch (outcome.end) {
        case net::RetryEnd::CanceledBeforeBuild: // :179-181, :260-272
            result = std::make_shared<const TileRequestResultCanceledBeforBuildDownloadRequest>(
                task->tileRequest());
            break;
        case net::RetryEnd::NoRequest: // :191-207
            result = std::make_shared<const TileRequestResultErrorBeforBuildDownloadRequest>(
                task->tileRequest(), buildError);
            break;
        case net::RetryEnd::CanceledAfterBuild: // :208-211
            if (!lastRequest) {
                throw std::logic_error("TileDownloadSubsystem: canceled after build without a request");
            }
            result = std::make_shared<const TileRequestResultCanceledAfterBuildDownloadRequest>(*lastRequest);
            break;
        case net::RetryEnd::Sent:
            if (!lastRequest) {
                throw std::logic_error("TileDownloadSubsystem: sent without a request");
            }
            if (outcome.result.kind == net::ResultKind::Canceled) { // :222-225
                result = std::make_shared<const TileRequestResultCanceledAfterDownloadRequest>(
                    *lastRequest, outcome.result);
            } else {
                result = saved; // the LAST attempt's
                if (!result) {
                    throw std::logic_error("TileDownloadSubsystem: a sent result was not saved");
                }
            }
            break;
        }
    } catch (...) {
        task->setFinished(nullptr); // legacy `finally SetFinished(VResult)` with VResult = nil
        throw;                      // the queue's worker loop logs it and goes on (D-87)
    }
    task->setFinished(result);
}

} // namespace detail

// Ports TRequestBuilderProviderInternal (u_TileDownloadSubsystem.pas:200-256): the TTL downloader and the
// lazy builder that serve getRequest, rebuilt as a pair on every change of the tile-downloader config, on the
// thread that changed it. The config-change callback takes only the provider's own lock, never a builder's
// mutex (R4). Owned by a shared_ptr; the callback holds a weak reference (core::Subscription::reset does not
// wait for a callback in flight).
class RequestBuilderProviderInternal final
    : public std::enable_shared_from_this<RequestBuilderProviderInternal> {
public:
    RequestBuilderProviderInternal(core::Notifier& gc,
                                   std::shared_ptr<net::IDownloaderFactory> downloaderFactory,
                                   std::shared_ptr<net::TileDownloaderConfig> config,
                                   std::shared_ptr<ITileDownloadRequestBuilderFactory> factory)
        : gc_(gc), downloaderFactory_(std::move(downloaderFactory)), config_(std::move(config)),
          factory_(std::move(factory)) {
        doInit();
    }

    // Ports FTileDownloaderConfig.ChangeNotifier.Add (:213). Once, after make_shared.
    void subscribe() {
        configSubscription_ = config_->changed().subscribe([weak = weak_from_this()] {
            if (const auto self = weak.lock()) {
                self->doInit();
            }
        });
    }

    [[nodiscard]] ITileDownloadRequestBuilderPtr requestBuilder() const {
        const core::ReadLock lock(mutex_);
        return builder_;
    }
    // T-0366: the lazy builder of the current pair, to load its script now.
    [[nodiscard]] std::shared_ptr<TileDownloadRequestBuilderLazy> lazyBuilder() const {
        const core::ReadLock lock(mutex_);
        return builder_;
    }
    [[nodiscard]] std::mutex& requestBuilderLock() { return requestBuilderMutex_; }

private:
    void doInit() { // :224-246
        const std::shared_ptr<net::TtlDownloader> downloader =
            makeTtlDownloader(gc_, downloaderFactory_, *config_);
        std::shared_ptr<TileDownloadRequestBuilderLazy> builder =
            std::make_shared<TileDownloadRequestBuilderLazy>(downloader, factory_);
        {
            const core::WriteLock lock(mutex_);
            std::swap(builder_, builder);
        }
        // The previous pair is released here, outside the lock, unless a getRequest still holds it.
    }

    core::Notifier& gc_;
    std::shared_ptr<net::IDownloaderFactory> downloaderFactory_;
    std::shared_ptr<net::TileDownloaderConfig> config_;
    std::shared_ptr<ITileDownloadRequestBuilderFactory> factory_;
    mutable core::Mutex mutex_; // FLock
    std::shared_ptr<TileDownloadRequestBuilderLazy> builder_;
    std::mutex requestBuilderMutex_; // FRequestBuilderLock: calls to the builder are serialised per map

    core::Subscription configSubscription_; // last: torn down first
};

TileDownloadSubsystem::TileDownloadSubsystem(
    IZmpInfoPtr zmp, core::Notifier& gc, std::stop_token appClosing,
    std::shared_ptr<core::ILanguageManager> languageManager,
    std::shared_ptr<const net::IGlobalDownloadConfig> globalDownloadConfig,
    std::shared_ptr<net::IDownloaderFactory> downloaderFactory, storage::MapVersionFactoryPtr versionFactory,
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig,
    std::shared_ptr<TileDownloadRequestBuilderConfig> requestBuilderConfig,
    std::shared_ptr<const core::ContentTypeRegistry> contentTypes,
    std::shared_ptr<const ContentConverters> converters, core::ContentTypeInfo saveContentType,
    std::shared_ptr<MapAbilitiesConfig> mapAbilitiesConfig,
    std::shared_ptr<const script::IProjConverterFactory> projFactory, storage::ITileStoragePtr storage,
    int appId, const QString& logsPath)
    : zmp_(std::move(zmp)), storage_(std::move(storage)),
      tileDownloaderConfig_(std::move(tileDownloaderConfig)), contentTypes_(std::move(contentTypes)),
      zmpDownloadEnabled_(zmp_ != nullptr && zmp_->tileDownloaderConfig() != nullptr &&
                          zmp_->tileDownloaderConfig()->enabled) { // :310
    if (!zmp_ || !zmp_->tileDownloaderConfig()) {
        throw std::invalid_argument("TileDownloadSubsystem: no ZMP");
    }
    if (zmpDownloadEnabled_) {
        if (!storage_ || !tileDownloaderConfig_ || !contentTypes_ || !downloaderFactory ||
            !requestBuilderConfig || !globalDownloadConfig || !mapAbilitiesConfig) {
            throw std::invalid_argument("TileDownloadSubsystem: a collaborator is null");
        }
        // FEAT-NET-032
        // :313-319. An empty sample list is legacy's nil list: the predicate stays empty.
        PredicateByBinaryData banPredicate;
        PredicateByBinaryData emptyPredicate;
        if (!zmp_->banTileSamples().empty()) {
            banPredicate = makePredicateByStaticSampleList(zmp_->banTileSamples());
        }
        if (!zmp_->emptyTileSamples().empty()) {
            emptyPredicate = makePredicateByStaticSampleList(zmp_->emptyTileSamples());
        }

        // :321-327. TAntiBanStuped.Create reads the two keys of params.txt / [PARAMS]
        // (u_AntiBanStuped.pas:111-113).
        const core::IConfigProviderPtr data = zmp_->dataProvider();
        const core::IConfigProviderPtr paramsFile = data ? data->subItem(kParamsFileName) : nullptr;
        const core::IConfigProviderPtr params = paramsFile ? paramsFile->subItem(kParamsSection) : nullptr;
        if (!params) { // legacy: access violation in the constructor, no handler (unreachable from ZmpInfo)
            throw std::runtime_error("TileDownloadSubsystem: no params.txt / PARAMS in the ZMP");
        }
        const net::IAntiBanPtr antiBan = std::make_shared<net::PreloadPageAntiBan>(
            params->readInt(u"UsePreloadPage", 0), params->readString(u"PreloadPage").toUtf8(),
            std::make_shared<PerCallDownloader>(downloaderFactory, downloaderOptions(*tileDownloaderConfig_)),
            [](const QByteArray& /*url*/) { /* the on-ban browser call is dead in legacy (anti_ban.h) */ });
        const net::IDownloadCheckerPtr checker = std::make_shared<DownloadCheckerStuped>(
            antiBan, banPredicate, emptyPredicate, tileDownloaderConfig_, storage_);

        // :328-341. The script is read as raw bytes, BOM kept (docs/specs/maptype.md LD-14).
        // T-0225 (docs/specs/map-source-json.md section 7): a *.source.json container offers its JavaScript
        // as GetUrlScript.js and gets the JavaScript factory; every other source keeps the Pascal factory.
        const QByteArray jsScript = data->binary(kJsScriptFileName);
        if (!jsScript.isEmpty()) {
            factory_ = std::make_shared<TileDownloadRequestBuilderFactoryJs>(
                zmp_->fileName(), jsScript, std::move(requestBuilderConfig), tileDownloaderConfig_,
                zmp_->projectionSet(), checker, std::move(languageManager));
        } else {
            factory_ = std::make_shared<TileDownloadRequestBuilderFactoryPascalScript>(
                zmp_->fileName(), data->binary(kScriptFileName), std::move(requestBuilderConfig),
                tileDownloaderConfig_, zmp_->projectionSet(), checker, std::move(projFactory),
                std::move(languageManager), storage_, std::move(versionFactory), contentTypes_, appId,
                logsPath);
        }

        // :343-355. The same emptyPredicate object as the checker's (:348).
        saver_ = std::make_shared<TileDownloadResultSaverStuped>(
            std::move(globalDownloadConfig), emptyPredicate, *contentTypes_, std::move(converters),
            zmp_->contentTypeSubst(), std::move(saveContentType), zmp_->tilePostDownloadCropConfig(),
            storage_);

        // :357-363
        state_ = std::make_shared<TileDownloadSubsystemState>(true, factory_->state(), saver_->state(),
                                                              std::move(mapAbilitiesConfig));
        state_->subscribe();

        // :365-382. Capacity 256. The slot-state factory captures nothing that owns the queue (R1).
        queue_ = std::make_shared<net::DownloadQueue>(
            downloaderFactory, gc, *tileDownloaderConfig_, net::kQueueCapacity, net::SteadyClockFn{},
            [factory = factory_](const net::IDownloaderPtr& downloader) -> net::DownloadQueue::SlotState {
                // :146-168: the slot's builder runs its script's nested HTTP through the slot's downloader,
                // the one the transfer uses too (the queue hands it over).
                auto slot = std::make_shared<detail::SlotState>();
                slot->builder = std::make_shared<TileDownloadRequestBuilderLazy>(downloader, factory);
                slot->lastResponse = std::make_shared<net::LastResponseInfo>();
                return slot;
            });
        queue_->subscribe();

        // :385-391
        provider_ = std::make_shared<RequestBuilderProviderInternal>(gc, std::move(downloaderFactory),
                                                                     tileDownloaderConfig_, factory_);
        provider_->subscribe();
    } else {
        // :392-399
        state_ = std::make_shared<TileDownloadSubsystemState>(false, nullptr, nullptr, nullptr);
    }

    // :402-406. The stop_callback runs at once on a token that is already stopped.
    appClosingCallback_.emplace(std::move(appClosing),
                                [this] { destroySource_.request_stop(); }); // OnAppClosing
}

TileDownloadSubsystem::~TileDownloadSubsystem() {
    destroySource_.request_stop(); // :411-413 NextOperation: cancels the builds of getRequest
    appClosingCallback_.reset();   // :414-418; waits for a callback running on another thread
    provider_.reset();             // :419
    queue_.reset();                // joins the workers (R1): the jobs hold neither the queue nor this object
}

void TileDownloadSubsystem::download(const ITileRequestTaskPtr& task) {
    const TaskInternalPtr internal = std::dynamic_pointer_cast<ITileRequestTaskInternal>(task);
    if (!internal) { // :429 Supports(): nothing happens for any other task
        return;
    }
    if (zmpDownloadEnabled_ && state_->getStatic()->enabled) {
        // R1: the closure holds the task and the saver, nothing that owns the queue.
        queue_->push([internal, saver = saver_](net::RetryDownloader& loop, void* slotState,
                                                const std::stop_token& stop) {
            detail::runTileDownloadJob(internal, *saver, loop, slotState, stop);
        });
    } else {
        internal->setFinished(nullptr); // :434, :437
    }
}

void TileDownloadSubsystem::checkRequestBuilder() {
    // T-0366: only a JavaScript source is loaded ahead of its first tile (a port extension, no legacy unit);
    // a ZMP script stays lazy as in legacy. A failed load disables the factory state (disableReason).
    if (zmpDownloadEnabled_ &&
        dynamic_cast<TileDownloadRequestBuilderFactoryJs*>(factory_.get()) != nullptr) {
        provider_->lazyBuilder()->load();
    }
}

std::optional<net::DownloadRequest>
TileDownloadSubsystem::getRequest(geo::PointI xy, quint8 zoom, const storage::MapVersionInfoPtr& version) {
    if (!zmpDownloadEnabled_ || !factory_->state()->getStatic()->enabled) { // :453-454
        return std::nullopt;
    }
    const ITileRequestPtr request = std::make_shared<const TileRequest>(xy, zoom, version);
    const ITileDownloadRequestBuilderPtr builder = provider_->requestBuilder();
    const std::lock_guard lock(provider_->requestBuilderLock()); // :465
    return builder->buildRequest(request, nullptr, destroySource_.get_token());
}

ITileRequestTaskPtr TileDownloadSubsystem::getRequestTask(std::stop_token softCancel, std::stop_token cancel,
                                                          ITileRequestTaskFinishNotifierPtr finishNotifier,
                                                          geo::PointI tile, quint8 zoom,
                                                          const storage::MapVersionInfoPtr& version,
                                                          bool checkTileSize) {
    if (!zmpDownloadEnabled_ || !state_->getStatic()->enabled) { // :496-497
        return nullptr;
    }
    if (!zmp_->projectionSet()->zoom(zoom)->checkTilePosStrict(tile)) { // :498
        return nullptr;
    }
    ITileRequestPtr request;
    if (checkTileSize) { // :501 / :508
        request = std::make_shared<const TileRequestWithSizeCheck>(tile, zoom, version);
    } else {
        request = std::make_shared<const TileRequest>(tile, zoom, version);
    }
    return std::make_shared<TileRequestTask>(std::move(request), std::move(softCancel), std::move(cancel),
                                             std::move(finishNotifier)); // :515
}

std::shared_ptr<ITileDownloaderStateChangeable> TileDownloadSubsystem::state() const {
    return state_;
}

} // namespace cg::maptype
