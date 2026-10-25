// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-REG-011 (overwrite, by size, by date), FEAT-REG-012 (retry TNE, by date), FEAT-REG-013 (start
// paused), FEAT-REG-018 (progress, pause, resume), FEAT-REG-020 (version for download): contribute only.
// Ports: Src/RegionProcess/Download/u_ThreadDownloadTiles.pas (:153-575).
//
// Rig: a scripted ITileDownloadSubsystem (records every getRequestTask, answers per call index), a scripted
// ITileStorage, a scripted IDownloadTaskProvider over real tile iterators and a real Google projection set,
// a recording decorator in front of a real RegionProcessProgressInfoDownload (+ real DownloadSession), a real
// LogRingBuffer (levels are observed through its minimum level), a real net::DownloadInfo.
// Every expected line, level and number is a literal from the Pascal unit or u_ResStrings.pas.
// Every test here starts a worker thread; the state shared with it lives behind the Hub's mutex or is atomic.
// No sleep is used to synchronise: the test waits on a condition the double signals. The only time-based
// assertions are LOWER bounds (a wait that must not be shorter), which cannot flake on a slow machine.
#include "cg/core/language_manager.h"
#include "cg/core/log_ring_buffer.h"
#include "cg/core/string_by_language.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/geo/tile_iterator.h"
#include "cg/maptype/map_abilities_config.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/tile_download_subsystem.h"
#include "cg/maptype/tile_downloader_state.h"
#include "cg/maptype/tile_request_result.h"
#include "cg/net/download_stats.h"
#include "cg/net/global_download_config.h"
#include "cg/region/download_session.h"
#include "cg/region/region_process_progress_info_download.h"
#include "cg/region/thread_download_tiles.h"
#include "cg/storage/tile_storage.h"

#include <QImage>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <latch>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "region_fake_map_type.h"

namespace {

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using cg::geo::PointI;
using cg::geo::RectI;
using cg::maptype::ITileRequestPtr;
using cg::maptype::ITileRequestResultPtr;
using cg::maptype::ITileRequestTaskPtr;
using cg::net::ResultKind;
using cg::region::ThreadDownloadTiles;
using cg::storage::TileInfo;
using cg::storage::TileInfoType;
using Clock = std::chrono::steady_clock;

constexpr auto kBound = 60s; // a failure bound only: nothing waits for it on a passing run

const PointI kUnset{-1, -1};

// --- synchronisation -------------------------------------------------------------------------------------

struct Hub {
    std::mutex mutex;
    std::condition_variable cv;
    // The predicate runs with the mutex held.
    template <class Pred> bool waitUntil(Pred pred) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, kBound, pred);
    }
    void notify() { cv.notify_all(); }
};

// --- the scripted subsystem
// -----------------------------------------------------------------------------------

enum class Mode { Finish, FinishOnOtherThread, Hold, ReturnNull, Throw };
using Maker = std::function<ITileRequestResultPtr(const ITileRequestPtr&)>;

struct Reply {
    Mode mode = Mode::Finish;
    Maker result;
};

struct Call {
    PointI tile;
    quint8 zoom = 0;
    cg::storage::MapVersionInfoPtr version;
    bool checkSize = false;
    std::stop_token soft;
    std::stop_token cancel;
    Clock::time_point at;
};

cg::net::DownloadResult dl(ResultKind kind, QByteArray data = {}, QByteArray contentType = {},
                           QString text = {}) {
    cg::net::DownloadResult r;
    r.kind = kind;
    r.data = std::move(data);
    r.contentType = std::move(contentType);
    r.text = std::move(text);
    return r;
}

cg::net::DownloadRequest requestOf(const ITileRequestPtr& request) {
    cg::net::DownloadRequest r;
    r.source = request;
    return r;
}

Maker withKind(ResultKind kind, QByteArray data = {}, QByteArray contentType = {}, QString text = {}) {
    return [=](const ITileRequestPtr& request) -> ITileRequestResultPtr {
        return std::make_shared<cg::maptype::TileRequestResultOk>(requestOf(request),
                                                                  dl(kind, data, contentType, text));
    };
}

Maker okBytes(qsizetype n) {
    return withKind(ResultKind::Ok, QByteArray(n, 'x'), "image/png");
}

Reply finishWith(Maker maker) {
    return Reply{Mode::Finish, std::move(maker)};
}

class ScriptedSubsystem final : public cg::maptype::ITileDownloadSubsystem {
public:
    explicit ScriptedSubsystem(Hub& hub) : hub_(hub) {}
    ~ScriptedSubsystem() override {
        for (auto& t : threads_) {
            t.join();
        }
    }

    // Set before start(); read-only afterwards. index = 0-based number of the getRequestTask call.
    std::function<Reply(int)> script = [](int) { return finishWith(okBytes(5)); };
    // Runs on the worker thread inside download(), before the task is finished.
    std::function<void(int)> onDownload;
    std::shared_ptr<cg::maptype::TileDownloaderStateInternal> stateInternal =
        std::make_shared<cg::maptype::TileDownloaderStateInternal>();

    cg::maptype::ITileRequestTaskPtr
    getRequestTask(std::stop_token softCancel, std::stop_token cancel,
                   cg::maptype::ITileRequestTaskFinishNotifierPtr finishNotifier, cg::geo::PointI tile,
                   quint8 zoom, const cg::storage::MapVersionInfoPtr& version, bool checkTileSize) override {
        int index = 0;
        {
            const std::lock_guard lock(hub_.mutex);
            index = static_cast<int>(calls_.size());
            calls_.push_back(Call{tile, zoom, version, checkTileSize, softCancel, cancel, Clock::now()});
            notifier_ = finishNotifier;
        }
        hub_.notify();
        const Reply reply = script(index);
        if (reply.mode == Mode::Throw) {
            throw std::runtime_error("boom");
        }
        if (reply.mode == Mode::ReturnNull) {
            return nullptr;
        }
        auto request = std::make_shared<const cg::maptype::TileRequest>(tile, zoom, version);
        auto task =
            std::make_shared<cg::maptype::TileRequestTask>(request, softCancel, cancel, finishNotifier);
        {
            const std::lock_guard lock(hub_.mutex);
            pending_[task.get()] = Pending{index, reply};
            tasks_.push_back(task);
        }
        return task;
    }

    std::optional<cg::net::DownloadRequest> getRequest(cg::geo::PointI, quint8,
                                                       const cg::storage::MapVersionInfoPtr&) override {
        return std::nullopt;
    }

    void download(const cg::maptype::ITileRequestTaskPtr& task) override {
        Pending p;
        {
            const std::lock_guard lock(hub_.mutex);
            p = pending_.at(task.get());
            ++downloads_;
        }
        hub_.notify();
        if (onDownload) {
            onDownload(p.index);
        }
        if (p.reply.mode == Mode::Hold) {
            return;
        }
        const ITileRequestResultPtr result = p.reply.result ? p.reply.result(task->tileRequest()) : nullptr;
        auto internal = std::dynamic_pointer_cast<cg::maptype::ITileRequestTaskInternal>(task);
        {
            const std::lock_guard lock(hub_.mutex);
            deliveredAt_[p.index] = Clock::now();
            if (p.reply.mode == Mode::FinishOnOtherThread) {
                threads_.emplace_back([internal, result] { internal->setFinished(result); });
                return;
            }
        }
        internal->setFinished(result); // synchronously on this thread, as a disabled subsystem does
    }

    std::shared_ptr<cg::maptype::ITileDownloaderStateChangeable> state() const override {
        return stateInternal;
    }

    std::vector<Call> calls() const {
        const std::lock_guard lock(hub_.mutex);
        return calls_;
    }
    int downloads() const {
        const std::lock_guard lock(hub_.mutex);
        return downloads_;
    }
    cg::maptype::ITileRequestTaskFinishNotifierPtr notifier() const {
        const std::lock_guard lock(hub_.mutex);
        return notifier_;
    }
    ITileRequestTaskPtr task(int index) const {
        const std::lock_guard lock(hub_.mutex);
        return tasks_.at(static_cast<size_t>(index));
    }
    Clock::time_point deliveredAt(int index) const {
        const std::lock_guard lock(hub_.mutex);
        return deliveredAt_.at(index);
    }
    // Predicates for Hub::waitUntil (mutex already held).
    size_t callsLocked() const { return calls_.size(); }
    int downloadsLocked() const { return downloads_; }

private:
    struct Pending {
        int index = 0;
        Reply reply;
    };
    Hub& hub_;
    std::vector<Call> calls_;
    std::map<const cg::maptype::ITileRequestTask*, Pending> pending_;
    std::vector<ITileRequestTaskPtr> tasks_;
    std::map<int, Clock::time_point> deliveredAt_;
    std::vector<std::thread> threads_;
    int downloads_ = 0;
    cg::maptype::ITileRequestTaskFinishNotifierPtr notifier_;
};

// --- the scripted storage ------------------------------------------------------------------------------

class ScriptedStorage final : public cg::storage::ITileStorage {
public:
    struct Query {
        PointI tile;
        quint8 zoom = 0;
        const cg::storage::MapVersionRequest* request = nullptr;
        cg::storage::LoadMode mode = cg::storage::LoadMode::WithData;
    };
    std::map<std::pair<int, int>, TileInfo> infos; // set before start()
    std::optional<cg::core::Error> failure;        // set before start()

    std::vector<Query> queries() const {
        const std::lock_guard lock(mutex_);
        return queries_;
    }

    [[nodiscard]] cg::core::Result<TileInfo> tileInfoEx(PointI xy, quint8 zoom,
                                                        const cg::storage::MapVersionRequest& request,
                                                        cg::storage::LoadMode mode) const override {
        {
            const std::lock_guard lock(mutex_);
            queries_.push_back(Query{xy, zoom, &request, mode});
        }
        if (failure) {
            return std::unexpected(*failure);
        }
        const auto it = infos.find({xy.x(), xy.y()});
        return it == infos.end() ? TileInfo{} : it->second;
    }

    [[nodiscard]] cg::storage::TileStorageTypeAbilities typeAbilities() const override { unused(); }
    [[nodiscard]] cg::storage::StorageStatePtr state() const override { unused(); }
    [[nodiscard]] cg::core::Changeable& stateChangeable() override { unused(); }
    [[nodiscard]] const cg::geo::IProjectionSet& projectionSet() const override { unused(); }
    [[nodiscard]] cg::storage::TilePyramidNotifier& tileNotifier() override { unused(); }
    [[nodiscard]] QString tileFileName(PointI, quint8, const cg::storage::MapVersionInfoPtr&) const override {
        unused();
    }
    [[nodiscard]] cg::core::Result<TileInfo> tileInfo(PointI, quint8, const cg::storage::MapVersionInfoPtr&,
                                                      cg::storage::LoadMode) const override {
        unused();
    }
    [[nodiscard]] cg::core::Result<bool> deleteTile(PointI, quint8,
                                                    const cg::storage::MapVersionInfoPtr&) override {
        unused();
    }
    [[nodiscard]] cg::core::Result<bool> saveTile(PointI, quint8, const cg::storage::MapVersionInfoPtr&,
                                                  cg::storage::TileDate, QByteArrayView, const QByteArray&,
                                                  bool) override {
        unused();
    }
    [[nodiscard]] cg::core::Result<cg::storage::MapVersionList>
    listVersions(PointI, quint8, const cg::storage::MapVersionRequest&) const override {
        unused();
    }
    [[nodiscard]] cg::core::Result<cg::storage::TileRectInfoPtr>
    tileRectInfo(RectI, quint8, const cg::storage::MapVersionRequest&, std::stop_token) const override {
        unused();
    }
    [[nodiscard]] cg::core::Result<cg::storage::EnumTileInfoPtr> scanTiles(bool, bool) override { unused(); }

private:
    [[noreturn]] static void unused() { throw std::logic_error("ScriptedStorage: not used by the worker"); }
    mutable std::mutex mutex_;
    mutable std::vector<Query> queries_;
};

// --- the scripted task provider -----------------------------------------------------------------------

class ScriptedProvider final : public cg::region::IDownloadTaskProvider {
public:
    struct Part {
        quint8 zoom;
        RectI rect;
    };
    ScriptedProvider(Hub& hub, cg::geo::ProjectionSetPtr set) : hub_(hub), set_(std::move(set)) {}

    std::vector<Part> parts; // set before start()
    qint64 total = 0;
    std::optional<std::string> throwOutOfRange; // set before start()
    std::function<void()> onCall;               // runs on the worker thread inside getTasksList

    void getTasksList(int workerIndex, qint64& tilesTotal,
                      std::vector<cg::geo::TileIteratorPtr>& tasksList) override {
        {
            const std::lock_guard lock(hub_.mutex);
            ++calls_;
            workerIndex_ = workerIndex;
        }
        if (onCall) {
            onCall();
        }
        if (throwOutOfRange) {
            throw std::out_of_range(*throwOutOfRange);
        }
        tasksList.clear();
        for (const auto& p : parts) {
            tasksList.push_back(
                std::make_unique<cg::geo::TileIteratorByRect>(cg::geo::TileRect(set_->zoom(p.zoom), p.rect)));
        }
        tilesTotal = total;
    }
    int calls() const {
        const std::lock_guard lock(hub_.mutex);
        return calls_;
    }
    int workerIndex() const {
        const std::lock_guard lock(hub_.mutex);
        return workerIndex_;
    }

private:
    Hub& hub_;
    cg::geo::ProjectionSetPtr set_;
    int calls_ = 0;
    int workerIndex_ = -1;
};

// --- the recording progress object
// --------------------------------------------------------------------------

// Forwards every call to the real object FIRST and records it afterwards, so a test that has seen an event
// may read the real object and find the effect already there.
class RecordingProgress final : public cg::region::IRegionProcessProgressInfoDownloadInternal {
public:
    RecordingProgress(Hub& hub, std::shared_ptr<cg::region::RegionProcessProgressInfoDownload> real)
        : hub_(hub), real_(std::move(real)) {}

    bool needPause() const override { return real_->needPause(); }
    void setNeedPause(bool v) override {
        real_->setNeedPause(v);
        record(v ? "needPause 1" : "needPause 0");
    }
    void finish() override {
        real_->finish();
        {
            const std::lock_guard lock(hub_.mutex);
            ++finishCalls_;
            finishedAt_ = Clock::now();
            events_.emplace_back("finish");
        }
        hub_.notify();
    }
    void setPaused() override {
        real_->setPaused();
        record("paused");
    }
    void setStarted() override {
        real_->setStarted();
        record("started");
    }
    void addManyProcessedTile(PointI t, quint32 n) override {
        real_->addManyProcessedTile(t, n);
        record("many " + std::to_string(t.x()) + "," + std::to_string(t.y()) + " " + std::to_string(n));
    }
    void addProcessedTile(PointI t) override {
        real_->addProcessedTile(t);
        record("processed " + std::to_string(t.x()) + "," + std::to_string(t.y()));
    }
    void addDownloadedTile(PointI t, quint32 size) override {
        real_->addDownloadedTile(t, size);
        record("downloaded " + std::to_string(t.x()) + "," + std::to_string(t.y()) + " " +
               std::to_string(size));
    }
    void addNotNecessaryTile(PointI t) override {
        real_->addNotNecessaryTile(t);
        record("notNecessary " + std::to_string(t.x()) + "," + std::to_string(t.y()));
    }
    void setTotalToProcess(qint64 v) override {
        real_->setTotalToProcess(v);
        record("total " + std::to_string(v));
    }
    std::shared_ptr<cg::core::LogRingBuffer> log() const override {
        {
            const std::lock_guard lock(hub_.mutex);
            ++logCalls_;
        }
        hub_.notify();
        return real_->log();
    }
    void noteDownloadAnswer(bool connectionError) override {
        real_->noteDownloadAnswer(connectionError);
        record(connectionError ? "answer 1" : "answer 0");
    }
    void noteFrameAnswer(cg::region::FrameAnswer answer) override { // T-0356: kept apart from events_
        real_->noteFrameAnswer(answer);
        const std::lock_guard lock(hub_.mutex);
        notServed_.push_back(static_cast<int>(answer));
    }
    std::vector<int> notServedCalls() const {
        const std::lock_guard lock(hub_.mutex);
        return notServed_;
    }
    void setZoom(quint8 v) override {
        real_->setZoom(v);
        record("zoom " + std::to_string(static_cast<int>(v)));
    }

    std::vector<std::string> events() const {
        const std::lock_guard lock(hub_.mutex);
        return events_;
    }
    Clock::time_point finishedAt() const {
        const std::lock_guard lock(hub_.mutex);
        return finishedAt_;
    }
    // Predicates for Hub::waitUntil (mutex already held).
    int countLocked(const std::string& e) const { return static_cast<int>(std::ranges::count(events_, e)); }
    int logCallsLocked() const { return logCalls_; }
    int finishCallsLocked() const { return finishCalls_; }

private:
    void record(std::string e) {
        {
            const std::lock_guard lock(hub_.mutex);
            events_.push_back(std::move(e));
        }
        hub_.notify();
    }
    Hub& hub_;
    std::shared_ptr<cg::region::RegionProcessProgressInfoDownload> real_;
    mutable std::vector<std::string> events_;
    std::vector<int> notServed_;
    mutable int logCalls_ = 0;
    mutable int finishCalls_ = 0;
    mutable Clock::time_point finishedAt_;
};

// --- the rig
// ------------------------------------------------------------------------------------------------

class ZmpGuiName final : public cg::maptype::IZmpInfoGui {
public:
    [[nodiscard]] cg::core::IStringByLanguagePtr name() const override {
        return std::make_shared<cg::core::StringByLanguageWithStaticList>(QStringList{u"Test Map"_s});
    }
    [[nodiscard]] int sortIndex() const override { return -1; }
    [[nodiscard]] quint16 hotKey() const override { return 0; }
    [[nodiscard]] bool separator() const override { return false; }
    [[nodiscard]] cg::core::IStringByLanguagePtr parentSubMenu() const override {
        return std::make_shared<cg::core::StringByLanguageWithStaticList>(QStringList{QString()});
    }
    [[nodiscard]] bool enabled() const override { return true; }
    [[nodiscard]] cg::core::IStringByLanguagePtr infoUrl() const override {
        return std::make_shared<cg::core::StringByLanguageWithStaticList>(QStringList{QString()});
    }
    [[nodiscard]] QImage bmp18() const override { return {}; }
    [[nodiscard]] QImage bmp24() const override { return {}; }
};

std::shared_ptr<cg::core::LanguageManager> languages() {
    static QTemporaryDir dir;
    static auto manager = std::make_shared<cg::core::LanguageManager>(dir.path());
    return manager;
}

struct Options {
    bool paused = false;
    bool replaceExist = false;
    bool checkSize = false;
    bool checkDate = false;
    bool secondLoadTne = false;
    bool checkTne = false;
    double checkDateDays = 0; // Delphi days; the date of the exists test
    double tneDateDays = 0;   // Delphi days; the date of the TNE test
    bool goNext = true;       // IsGoNextTileIfDownloadError
    ThreadDownloadTiles::Timing timing{1, 1, 1, 1};
    int minLevel = 0;
    int workerIndex = 2;
    bool useDownload = true;
    QString disableReason; // non-empty: the subsystem state is disabled with it
    bool cancelStopped = false;
    bool appClosingStopped = false;
};

struct Rig {
    Hub hub;
    Options opt;
    std::stop_source cancel;
    std::stop_source appClosing;
    std::atomic<double> clock{100.0};
    std::shared_ptr<cg::region::test::FakeMapType> map = std::make_shared<cg::region::test::FakeMapType>();
    std::shared_ptr<ScriptedSubsystem> subsystem = std::make_shared<ScriptedSubsystem>(hub);
    std::shared_ptr<ScriptedStorage> storage = std::make_shared<ScriptedStorage>();
    std::shared_ptr<ScriptedProvider> provider;
    std::shared_ptr<cg::region::DownloadSession> session = std::make_shared<cg::region::DownloadSession>();
    std::shared_ptr<cg::core::LogRingBuffer> log;
    std::shared_ptr<cg::region::RegionProcessProgressInfoDownload> real;
    std::shared_ptr<RecordingProgress> progress;
    std::shared_ptr<cg::net::GlobalDownloadConfig> downloadConfig =
        std::make_shared<cg::net::GlobalDownloadConfig>();
    std::shared_ptr<cg::net::DownloadInfo> downloadInfo = std::make_shared<cg::net::DownloadInfo>();
    cg::maptype::MapVersionRequestPtr versionForCheck =
        std::make_shared<const cg::storage::MapVersionRequest>();
    cg::storage::MapVersionInfoPtr versionForDownload =
        std::make_shared<const cg::storage::MapVersionInfo>(QStringLiteral("vd"), cg::core::kNoHash);
    std::unique_ptr<ThreadDownloadTiles> worker; // last: destroyed first

    explicit Rig(Options o = {}) : opt(std::move(o)) {
        if (opt.cancelStopped) {
            cancel.request_stop();
        }
        if (opt.appClosingStopped) {
            appClosing.request_stop();
        }
        cg::geo::ProjectionSetFactory setFactory{
            cg::geo::HashSeed{}, std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{})};
        map->projectionSetValue =
            setFactory.byCode(cg::geo::kProjEpsgGoogle, cg::geo::kTileSplitQuadrate256x256);
        map->abilitiesValue = std::make_shared<cg::maptype::MapAbilitiesConfig>(
            std::make_shared<const cg::maptype::MapAbilitiesConfigStatic>(
                cg::maptype::MapAbilitiesConfigStatic{true, opt.useDownload}));
        map->guiConfigValue =
            std::make_shared<cg::maptype::MapTypeGuiConfig>(languages(), std::make_shared<ZmpGuiName>());
        map->tileDownloadSubsystemValue = subsystem;
        map->tileStorageValue = storage;
        map->getTileShowNameFn = [](PointI p, quint8 z, const cg::storage::MapVersionInfoPtr& v) {
            return QStringLiteral("N%1_%2_%3_%4")
                .arg(int(z))
                .arg(p.x())
                .arg(p.y())
                .arg(v ? v->storeString() : u"-"_s);
        };
        if (!opt.disableReason.isEmpty()) {
            subsystem->stateInternal->disable(opt.disableReason);
        }
        provider = std::make_shared<ScriptedProvider>(hub, map->projectionSetValue);
        provider->parts = {{3, RectI{1, 2, 2, 3}}}; // one tile: (1,2) at zoom 3
        provider->total = 1;
        log = std::make_shared<cg::core::LogRingBuffer>(1000, opt.minLevel);
        real = std::make_shared<cg::region::RegionProcessProgressInfoDownload>(
            log, session, opt.paused, [this] { return clock.load(); });
        progress = std::make_shared<RecordingProgress>(hub, real);
        downloadConfig->setGoNextTileIfDownloadError(opt.goNext);
    }
    ~Rig() { cancel.request_stop(); } // before the worker (a member) is destroyed: the caller's contract

    void make() {
        worker = std::make_unique<ThreadDownloadTiles>(
            opt.workerIndex, cancel.get_token(), progress, appClosing.get_token(), map, versionForCheck,
            versionForDownload, provider, downloadConfig, downloadInfo, opt.replaceExist, opt.checkSize,
            opt.checkDate, cg::storage::fromDelphiDateTime(opt.checkDateDays), opt.secondLoadTne,
            opt.checkTne, cg::storage::fromDelphiDateTime(opt.tneDateDays), opt.timing);
    }
    void start() {
        make();
        worker->start();
    }

    // Waits for the events the double signals. All return false on timeout.
    bool waitEvent(const std::string& e, int n = 1) {
        return hub.waitUntil([&] { return progress->countLocked(e) >= n; });
    }
    bool waitCalls(size_t n) {
        return hub.waitUntil([&] { return subsystem->callsLocked() >= n; });
    }
    bool waitDownloads(int n) {
        return hub.waitUntil([&] { return subsystem->downloadsLocked() >= n; });
    }
    bool waitLogCalls(int n) {
        return hub.waitUntil([&] { return progress->logCallsLocked() >= n; });
    }
    bool waitFinished() {
        return hub.waitUntil([&] { return progress->finishCallsLocked() >= 1; });
    }
    // The worker ends by itself (no cancel): wait for finish(), then join.
    bool runToEnd() {
        start();
        const bool ok = waitFinished();
        worker.reset();
        return ok;
    }
    // Cancel, wait until the worker calls finish() ON ITS OWN (the destructor is not what wakes it), then
    // destroy the object. Returns the time the whole thing took; a worker that never finishes gives a huge
    // value.
    std::chrono::milliseconds cancelAndJoin() {
        const auto t0 = Clock::now();
        cancel.request_stop();
        const bool finished = waitFinished();
        worker.reset();
        return finished ? std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0) : 24h;
    }
    QStringList lines() const {
        std::uint64_t id = 0;
        return log->since(id, 100000);
    }
    int count(const std::string& e) const {
        const auto ev = progress->events();
        return static_cast<int>(std::ranges::count(ev, e));
    }
};

// "Processing tile: [z4/x1/y2]\r\nN3_1_2_vd ..." -- u_ResStrings SAS_STR_ProcessedFile around the unit's
// '[z%d/x%d/y%d]' + #13#10 + '%s' (zoom + 1 is printed).
QString processing(int zoom, int x, int y) {
    return u"Processing tile: [z%1/x%2/y%3]\r\nN%4_%5_%6_vd ..."_s.arg(zoom + 1)
        .arg(x)
        .arg(y)
        .arg(zoom)
        .arg(x)
        .arg(y);
}
const QString kProcessingTile = processing(3, 1, 2);
const QString kDownloading = u"Downloading..."_s;
const QString kDownloadingRepl = u"Downloading with overwrite..."_s;
const QString kCompleted = u"The task is completed!"_s;
const QString kPaused = u"Paused by user..."_s;
const QString kOk = u"(Ok!)"_s;

TileInfo infoOf(TileInfoType type, double delphiDays) {
    TileInfo i;
    i.type = type;
    if (type == TileInfoType::Exists || type == TileInfoType::TneExists) {
        i.loadDate = cg::storage::fromDelphiDateTime(delphiDays);
        i.size = type == TileInfoType::Exists ? 10 : 0;
    }
    return i;
}

} // namespace

// === Proof 1: the decision table of :320-353 ===

namespace {

struct TableCase {
    const char* name;
    TileInfoType type;
    bool replace, checkDate, secondTne, checkTne;
    double checkDays, tneDays; // the two dates of the options
    bool checkSize;
    std::vector<QString> lines; // between the "Processing" line and the end
    bool requests;
};

// The tile's load date is 45000.5. A date <= it makes the tile "newer than required" (>=): 44999.5 skips,
// 45000.5 (equal) skips, 45001.5 does not. The date the case does not use is the opposite of the outcome,
// so a swapped pair of arguments changes the result.
constexpr double kLoad = 45000.5;
constexpr double kOlder = 44999.5; // check date older than the tile: skip
constexpr double kEqual = 45000.5; // equal: skip (>=)
constexpr double kNewer = 45001.5; // check date newer than the tile: do not skip

const std::vector<TableCase>& tableCases() {
    static const std::vector<TableCase> cases{
        // exists, replaceExistTiles off: FileExistsShort, never the date test (:323-326, 331)
        {"ExistsNoReplace",
         TileInfoType::Exists,
         false,
         false,
         true,
         true,
         kOlder,
         kOlder,
         true,
         {u"The tile already exists"_s},
         false},
        {"ExistsNoReplaceDateIgnored",
         TileInfoType::Exists,
         false,
         true,
         true,
         true,
         kOlder,
         kNewer,
         false,
         {u"The tile already exists"_s},
         false},
        // exists, replace on, date test on: >= skips, equal included
        {"ExistsReplaceEqualDateSkips",
         TileInfoType::Exists,
         true,
         true,
         false,
         false,
         kEqual,
         40000,
         false,
         {kDownloadingRepl, u"The tile is newer than the entered age, skipping."_s},
         false},
        {"ExistsReplaceOlderCheckDateSkips",
         TileInfoType::Exists,
         true,
         true,
         false,
         false,
         kOlder,
         40000,
         false,
         {kDownloadingRepl, u"The tile is newer than the entered age, skipping."_s},
         false},
        {"ExistsReplaceNewerCheckDateDownloads",
         TileInfoType::Exists,
         true,
         true,
         true,
         true,
         kNewer,
         kOlder,
         true,
         {kDownloadingRepl},
         true},
        {"ExistsReplaceDateTestOff",
         TileInfoType::Exists,
         true,
         false,
         false,
         true,
         kOlder,
         kOlder,
         true,
         {kDownloadingRepl},
         true},
        // TNE marker, secondLoadTne off: TneExistsShort, never the date test (:339-342, 347)
        {"TneNoSecondLoad",
         TileInfoType::TneExists,
         true,
         true,
         false,
         true,
         kOlder,
         kOlder,
         false,
         {u"The tne already exists"_s},
         false},
        {"TneNoSecondLoadDateIgnored",
         TileInfoType::TneExists,
         false,
         false,
         false,
         true,
         kNewer,
         kOlder,
         true,
         {u"The tne already exists"_s},
         false},
        // TNE, second load on, date test on
        {"TneSecondLoadEqualDateSkips",
         TileInfoType::TneExists,
         false,
         true,
         true,
         true,
         46000,
         kEqual,
         false,
         {kDownloading, u"The tne is newer than the entered age, skipping."_s},
         false},
        {"TneSecondLoadOlderDateSkips",
         TileInfoType::TneExists,
         false,
         true,
         true,
         true,
         46000,
         kOlder,
         false,
         {kDownloading, u"The tne is newer than the entered age, skipping."_s},
         false},
        {"TneSecondLoadNewerDateDownloads",
         TileInfoType::TneExists,
         true,
         true,
         true,
         true,
         40000,
         kNewer,
         true,
         {kDownloading},
         true},
        {"TneSecondLoadDateTestOff",
         TileInfoType::TneExists,
         false,
         true,
         true,
         false,
         40000,
         kOlder,
         false,
         {kDownloading},
         true},
        // neither: LoadProcess and a request
        {"NotExists",
         TileInfoType::NotExists,
         true,
         true,
         true,
         true,
         kOlder,
         kOlder,
         true,
         {kDownloading},
         true},
        {"Unknown",
         TileInfoType::Unknown,
         false,
         false,
         false,
         false,
         kOlder,
         kOlder,
         false,
         {kDownloading},
         true},
    };
    return cases;
}

} // namespace

TEST(ThreadDownloadTiles, DecisionTableOfProcessTask) {
    for (const auto& c : tableCases()) {
        SCOPED_TRACE(c.name);
        Options o;
        o.replaceExist = c.replace;
        o.checkDate = c.checkDate;
        o.secondLoadTne = c.secondTne;
        o.checkTne = c.checkTne;
        o.checkDateDays = c.checkDays;
        o.tneDateDays = c.tneDays;
        o.checkSize = c.checkSize;
        Rig rig(o);
        rig.storage->infos[{1, 2}] = infoOf(c.type, kLoad);
        ASSERT_TRUE(rig.runToEnd());

        QStringList expected{kProcessingTile};
        for (const auto& l : c.lines) {
            expected << l;
        }
        if (c.requests) {
            expected << kOk;
        }
        expected << kCompleted;
        EXPECT_EQ(rig.lines(), expected);

        const auto calls = rig.subsystem->calls();
        ASSERT_EQ(calls.size(), c.requests ? 1u : 0u);
        if (c.requests) {
            EXPECT_EQ(calls[0].tile, PointI(1, 2));
            EXPECT_EQ(calls[0].zoom, 3);
            EXPECT_EQ(calls[0].version, rig.versionForDownload); // FEAT-REG-020
            EXPECT_EQ(calls[0].checkSize, c.checkSize);          // only handed over, plays no part above
        }
        // The tile is counted as processed in every row (skipped or downloaded).
        EXPECT_EQ(rig.real->processed(), 1);
        EXPECT_EQ(rig.real->downloaded(), c.requests ? 1 : 0);

        const auto queries = rig.storage->queries();
        ASSERT_EQ(queries.size(), 1u);
        EXPECT_EQ(queries[0].tile, PointI(1, 2));
        EXPECT_EQ(queries[0].zoom, 3);
        EXPECT_EQ(queries[0].request, rig.versionForCheck.get());
        EXPECT_EQ(queries[0].mode, cg::storage::LoadMode::AsIs);
    }
}

// Every line of the table, "Processing tile", "(Ok!)" and "The task is completed!" is level 0: with the
// minimum at 1 nothing is written (:300-353 call WriteText(..., 0)).
TEST(ThreadDownloadTiles, TableLinesAreLevelZero) {
    for (const auto& c : tableCases()) {
        SCOPED_TRACE(c.name);
        Options o;
        o.replaceExist = c.replace;
        o.checkDate = c.checkDate;
        o.secondLoadTne = c.secondTne;
        o.checkTne = c.checkTne;
        o.checkDateDays = c.checkDays;
        o.tneDateDays = c.tneDays;
        o.minLevel = 1;
        Rig rig(o);
        rig.storage->infos[{1, 2}] = infoOf(c.type, kLoad);
        ASSERT_TRUE(rig.runToEnd());
        EXPECT_TRUE(rig.lines().isEmpty());
    }
}

// === Proof 2: the 12 rows of :500-575 ===

namespace {

struct Row {
    const char* name;
    Reply first;
    QString text; // the line(s) the row logs after "Downloading...", empty = none
    int level;    // its level
    bool repeats; // the same tile is requested again
    bool pauses;  // needPause is set: the worker parks until resume
    qint64 downloaded;
    quint64 downloadSize;
    quint64 infoCount;
    quint64 infoSize;
};

// A second request is answered with (Ok!) and 7 bytes, so the counters of a repeating row end at 1 / 7.
const QString kWait0 = u"\r\nWait 0 seconds..."_s; // timing < 1000 ms: `div 1000` = 0

std::vector<Row> rows() {
    return {
        // 1: Ok and also ITileRequestResultError (downloaded, not saved)
        {"OkNotSaved", finishWith([](const ITileRequestPtr& r) -> ITileRequestResultPtr {
             return std::make_shared<cg::maptype::TileRequestResultErrorAfterDownloadRequest>(
                 requestOf(r), dl(ResultKind::Ok, QByteArray(5, 'x'), "image/png"), u"disk full"_s);
         }),
         u"Error: disk full"_s, 0, true, true, 1, 7, 2, 12},
        // 2: Ok
        {"OkSaved", finishWith(okBytes(5)), kOk, 0, false, false, 1, 5, 1, 5},
        // 3: NotNecessary
        {"NotNecessary", finishWith(withKind(ResultKind::NotNecessary)),
         u"The tile size is equal to the existing one, skipping."_s, 0, false, false, 0, 0, 0, 0},
        // 4: ProxyError -> Authorization + wait, tile repeated
        {"ProxyError", finishWith(withKind(ResultKind::ProxyError)), u"Proxy authorization error"_s + kWait0,
         10, true, false, 1, 7, 1, 7},
        // 5: Banned
        {"Banned", finishWith(withKind(ResultKind::Banned)),
         u"Most likely you've been banned by the server!"_s + kWait0, 10, true, false, 1, 7, 1, 7},
        // 6: BadContentType: the result's contentType, level 1, moves on
        {"BadContentType", finishWith(withKind(ResultKind::BadContentType, {}, "text/html")),
         u"Server returned unexpected type \"text/html\""_s, 1, false, false, 0, 0, 0, 0},
        // 7: DataNotExists
        {"DataNotExists", finishWith(withKind(ResultKind::DataNotExists)), u"Tile is not found!"_s, 1, false,
         false, 0, 0, 0, 0},
        // 8: NoConnectToServer prints the download result's OWN text and never moves on (LR-7); the config is
        // go-next = true in the runner, so a swap with row 9 would move on
        {"NoConnectToServer",
         finishWith(withKind(ResultKind::NoConnectToServer, {}, {}, u"Connection refused"_s)),
         u"Connection refused"_s + kWait0, 10, true, false, 1, 7, 1, 7},
        // 10: Canceled kind with a download result
        {"CanceledWithDownloadResult", finishWith(withKind(ResultKind::Canceled)),
         u"Error: Unknown download result!"_s, 10, true, false, 1, 7, 1, 7},
        // 11: no download result, Canceled: false, no log (LR-5 is its neighbour)
        {"CanceledWithoutDownloadResult", finishWith([](const ITileRequestPtr& r) -> ITileRequestResultPtr {
             return std::make_shared<cg::maptype::TileRequestResultCanceledBeforBuildDownloadRequest>(r);
         }),
         QString(), 0, true, false, 1, 7, 1, 7},
        // 12: no download result, not Canceled: true; D-row of T-0271 (L-269): one log line, level 10
        {"PlainResultWithoutDownloadResult",
         finishWith([](const ITileRequestPtr& r) -> ITileRequestResultPtr {
             return std::make_shared<cg::maptype::TileRequestResult>(r);
         }),
         u"Error: Unknown download result!"_s, 10, false, false, 0, 0, 0, 0},
        // 12: an ITileRequestResultError without a download result (a failed request builder): its text
        {"ErrorResultWithoutDownloadResult",
         finishWith([](const ITileRequestPtr& r) -> ITileRequestResultPtr {
             return std::make_shared<cg::maptype::TileRequestResultErrorBeforBuildDownloadRequest>(
                 r, u"script failed: Город"_s);
         }),
         u"Error: script failed: Город"_s, 10, false, false, 0, 0, 0, 0},
        // 12: a NULL result (a subsystem that is disabled finishes with null), L-269 line as above
        {"NullResult", finishWith([](const ITileRequestPtr&) -> ITileRequestResultPtr { return nullptr; }),
         u"Error: Unknown download result!"_s, 10, false, false, 0, 0, 0, 0},
        // the catch of :395-399 (exception from getRequestTask) and the null task (:389-392)
        {"GetRequestTaskThrows", Reply{Mode::Throw, {}}, u"std::runtime_error: boom"_s, 0, true, true, 1, 7,
         1, 7},
        {"GetRequestTaskReturnsNull", Reply{Mode::ReturnNull, {}},
         u"Error: Downloading is disabled for this map!"_s, 0, true, true, 1, 7, 1, 7},
    };
}

struct Outcome {
    QStringList lines;
    std::vector<Call> calls;
    std::vector<std::string> events;
    qint64 downloaded = -1, processed = -1;
    quint64 downloadSize = 0, infoCount = 0, infoSize = 0;
    PointI lastSuccessful;
    bool ok = false;
};

// Runs the row's first reply, then (a repeat or a resume) answers Ok with 7 bytes. A parked worker is
// resumed.
void runRow(const Row& row, int minLevel, bool goNext, Outcome& out, ResultKind* kindOverride = nullptr) {
    Options o;
    o.minLevel = minLevel;
    o.goNext = goNext;
    Rig rig(o);
    Reply first = row.first;
    if (kindOverride != nullptr) {
        first = finishWith(withKind(*kindOverride));
    }
    rig.subsystem->script = [first](int i) { return i == 0 ? first : finishWith(okBytes(7)); };
    rig.start();
    if (row.pauses) {
        if (!rig.waitEvent("paused")) {
            return;
        }
        // Parked: not advanced, no second request yet.
        EXPECT_EQ(rig.subsystem->calls().size(), 1u);
        EXPECT_EQ(rig.real->processed(), 0);
        rig.real->resume();
    }
    out.ok = rig.waitFinished();
    rig.worker.reset();
    out.lines = rig.lines();
    out.calls = rig.subsystem->calls();
    out.events = rig.progress->events();
    out.downloaded = rig.real->downloaded();
    out.processed = rig.real->processed();
    out.downloadSize = rig.real->downloadSize();
    out.infoCount = rig.downloadInfo->tileCount();
    out.infoSize = rig.downloadInfo->size();
    out.lastSuccessful = rig.session->lastSuccessfulPoint();
}

int countOf(const std::vector<std::string>& ev, const std::string& e) {
    return static_cast<int>(std::ranges::count(ev, e));
}

} // namespace

TEST(ThreadDownloadTiles, ResultRowsLogCountAndRepeat) {
    for (const auto& row : rows()) {
        SCOPED_TRACE(row.name);
        Outcome out;
        runRow(row, 0, true, out);
        ASSERT_TRUE(out.ok);

        QStringList expected{kProcessingTile, kDownloading};
        if (!row.text.isEmpty()) {
            expected << row.text;
        }
        if (row.pauses) {
            expected << kPaused;
        }
        if (row.repeats) {
            expected << kProcessingTile << kDownloading << kOk;
        }
        expected << kCompleted;
        EXPECT_EQ(out.lines, expected);

        ASSERT_EQ(out.calls.size(), row.repeats ? 2u : 1u);
        if (row.repeats) {
            EXPECT_EQ(out.calls[1].tile, out.calls[0].tile); // the SAME tile is requested again
        }
        EXPECT_EQ(out.processed, 1);
        EXPECT_EQ(out.downloaded, row.downloaded);
        EXPECT_EQ(out.downloadSize, row.downloadSize);
        EXPECT_EQ(out.infoCount, row.infoCount);
        EXPECT_EQ(out.infoSize, row.infoSize);
        // AddDownloadedTile and AddNotNecessaryTile (:182) both set the last successful point
        const bool successPoint = row.downloaded > 0 || std::string(row.name) == "NotNecessary";
        EXPECT_EQ(out.lastSuccessful, successPoint ? PointI(1, 2) : kUnset);
        EXPECT_EQ(countOf(out.events, "needPause 1"), row.pauses ? 1 : 0);
        EXPECT_EQ(countOf(out.events, "paused"), row.pauses ? 1 : 0);
        EXPECT_EQ(countOf(out.events, "processed 1,2"), 1);
    }
}

// The level of each line: present with the minimum equal to it, dropped with the minimum one above.
TEST(ThreadDownloadTiles, ResultRowsLogAtTheirLevel) {
    for (const auto& row : rows()) {
        if (row.text.isEmpty()) {
            continue;
        }
        SCOPED_TRACE(row.name);
        Outcome at, above;
        runRow(row, row.level, true, at);
        runRow(row, row.level + 1, true, above);
        ASSERT_TRUE(at.ok);
        ASSERT_TRUE(above.ok);
        EXPECT_TRUE(at.lines.contains(row.text));
        EXPECT_FALSE(above.lines.contains(row.text));
    }
}

// Row 9: every other error kind prints "Error connecting to server" and follows IsGoNextTileIfDownloadError.
TEST(ThreadDownloadTiles, OtherErrorKindsFollowTheGoNextSetting) {
    Row row{"error",
            Reply{Mode::Finish, Maker{}},
            u"Error connecting to server"_s + kWait0,
            10,
            true,
            false,
            1,
            7,
            1,
            7};
    for (ResultKind kind : {ResultKind::BadContentLength, ResultKind::BadContentEncoding,
                            ResultKind::LoadError, ResultKind::UnknownError}) {
        SCOPED_TRACE(static_cast<int>(kind));
        Outcome retry, next;
        ResultKind k = kind;
        runRow(row, 0, false, retry, &k);
        runRow(row, 0, true, next, &k);
        ASSERT_TRUE(retry.ok);
        ASSERT_TRUE(next.ok);
        const QString wait = u"Error connecting to server"_s + kWait0;
        // go-next off: the tile is requested again
        EXPECT_EQ(retry.lines, (QStringList{kProcessingTile, kDownloading, wait, kProcessingTile,
                                            kDownloading, kOk, kCompleted}));
        EXPECT_EQ(retry.calls.size(), 2u);
        // go-next on: the tile counts as processed, nothing downloaded
        EXPECT_EQ(next.lines, (QStringList{kProcessingTile, kDownloading, wait, kCompleted}));
        EXPECT_EQ(next.calls.size(), 1u);
        EXPECT_EQ(next.processed, 1);
        EXPECT_EQ(next.downloaded, 0);
        EXPECT_EQ(next.infoCount, 0u);
    }
}

// Row 8 is not row 9 (LR-7): NoConnectToServer repeats even when go-next is on (rows() runs with it on); with
// it off it repeats too.
TEST(ThreadDownloadTiles, NoConnectToServerNeverMovesOnWhateverTheSetting) {
    for (bool goNext : {false, true}) {
        SCOPED_TRACE(goNext);
        Outcome out;
        Row row = rows()[7];
        ASSERT_STREQ(row.name, "NoConnectToServer");
        runRow(row, 0, goNext, out);
        ASSERT_TRUE(out.ok);
        EXPECT_EQ(out.calls.size(), 2u);
        EXPECT_EQ(out.lines[2], u"Connection refused"_s + kWait0);
    }
}

// The waits last their full time: a lower bound on the interval between the delivery of the result and the
// next thing the worker does. The row's own timing field is 250 ms, the other three are 1 ms, so a wait that
// used another field, or that returned at once because the event was still set (:511 removed), is shorter.
TEST(ThreadDownloadTiles, EachWaitRowSleepsItsOwnTime) {
    struct W {
        const char* name;
        ResultKind kind;
        bool goNext;
        quint32 ThreadDownloadTiles::Timing::* field;
    };
    const W waits[] = {
        {"proxy", ResultKind::ProxyError, true, &ThreadDownloadTiles::Timing::proxyAuthMs},
        {"ban", ResultKind::Banned, true, &ThreadDownloadTiles::Timing::banMs},
        {"noConnect", ResultKind::NoConnectToServer, true, &ThreadDownloadTiles::Timing::downloadErrorMs},
        {"loadErrorRetry", ResultKind::LoadError, false, &ThreadDownloadTiles::Timing::downloadErrorMs},
        {"loadErrorNext", ResultKind::LoadError, true, &ThreadDownloadTiles::Timing::downloadErrorMs},
    };
    for (const auto& w : waits) {
        SCOPED_TRACE(w.name);
        Options o;
        o.goNext = w.goNext;
        o.timing.*w.field = 250;
        Rig rig(o);
        const ResultKind kind = w.kind;
        rig.subsystem->script = [kind](int i) {
            return i == 0 ? finishWith(withKind(kind)) : finishWith(okBytes(7));
        };
        ASSERT_TRUE(rig.runToEnd());
        const auto calls = rig.subsystem->calls();
        const auto delivered = rig.subsystem->deliveredAt(0);
        // retry: the second request; go-next: the end of the worker
        const auto next = calls.size() > 1 ? calls[1].at : rig.progress->finishedAt();
        EXPECT_GE(next - delivered, 200ms);
    }
}

// The texts and levels of the four waits with the legacy default timing, and the cut: cancel right after the
// line was written ends the worker long before the 5 / 10 seconds (the cancel callback sets the event).
TEST(ThreadDownloadTiles, DefaultWaitsTextLevelAndCancelCutsTheWait) {
    EXPECT_EQ(ThreadDownloadTiles::Timing{}.pausePollMs, 100u); // :194-197
    EXPECT_EQ(ThreadDownloadTiles::Timing{}.banMs, 5000u);
    EXPECT_EQ(ThreadDownloadTiles::Timing{}.proxyAuthMs, 10000u);
    EXPECT_EQ(ThreadDownloadTiles::Timing{}.downloadErrorMs, 5000u);

    struct W {
        const char* name;
        ResultKind kind;
        QString line;
    };
    const W waits[] = {
        {"proxy", ResultKind::ProxyError, u"Proxy authorization error\r\nWait 10 seconds..."_s},
        {"ban", ResultKind::Banned, u"Most likely you've been banned by the server!\r\nWait 5 seconds..."_s},
        {"noConnect", ResultKind::NoConnectToServer, u"\r\nWait 5 seconds..."_s}, // own text is empty here
        {"loadError", ResultKind::LoadError, u"Error connecting to server\r\nWait 5 seconds..."_s},
    };
    for (const auto& w : waits) {
        SCOPED_TRACE(w.name);
        QStringList seen[3];
        const int minLevels[3] = {0, 10, 11};
        for (int m = 0; m < 3; ++m) {
            Options o;
            o.minLevel = minLevels[m];
            o.timing = ThreadDownloadTiles::Timing{};
            Rig rig(o);
            const ResultKind kind = w.kind;
            rig.subsystem->script = [kind](int) { return finishWith(withKind(kind)); };
            rig.start();
            // Processing, Downloading, the row's line: the third log() call comes after the event reset of
            // :511.
            ASSERT_TRUE(rig.waitLogCalls(3));
            const auto took = rig.cancelAndJoin();
            EXPECT_LT(took, 3000ms);
            seen[m] = rig.lines();
            EXPECT_EQ(rig.subsystem->calls().size(), 1u); // cancelled during the wait: no second request
            EXPECT_EQ(rig.real->processed(), 0);
        }
        EXPECT_EQ(seen[0], (QStringList{kProcessingTile, kDownloading, w.line}));
        EXPECT_EQ(seen[1], (QStringList{w.line})); // level 10
        EXPECT_TRUE(seen[2].isEmpty());            // nothing at level 11
    }
}

// === Proof 3: pause ===

TEST(ThreadDownloadTiles, StartPausedMakesNoRequestBeforeResume) {
    Options o;
    o.paused = true;
    Rig rig(o);
    rig.start();
    ASSERT_TRUE(rig.waitEvent("paused"));
    EXPECT_EQ(rig.subsystem->calls().size(), 0u);
    EXPECT_EQ(rig.storage->queries().size(), 0u);
    EXPECT_TRUE(rig.real->isPaused());

    rig.real->resume();
    ASSERT_TRUE(rig.waitFinished());
    rig.worker.reset();
    const auto ev = rig.progress->events();
    const auto at = [&](const std::string& e) { return std::ranges::find(ev, e) - ev.begin(); };
    EXPECT_LT(at("paused"), at("started"));
    EXPECT_LT(at("started"), at("downloaded 1,2 5"));
    EXPECT_EQ(rig.lines(), (QStringList{kPaused, kProcessingTile, kDownloading, kOk, kCompleted}));
    EXPECT_EQ(rig.subsystem->calls().size(), 1u);
}

// "Paused by user..." is written at level 10 (:294).
TEST(ThreadDownloadTiles, UserStopIsLoggedAtLevelTen) {
    for (int minLevel : {10, 11}) {
        SCOPED_TRACE(minLevel);
        Options o;
        o.paused = true;
        o.minLevel = minLevel;
        Rig rig(o);
        rig.start();
        ASSERT_TRUE(rig.waitLogCalls(1));
        rig.cancelAndJoin();
        EXPECT_EQ(rig.lines(), minLevel == 10 ? QStringList{kPaused} : QStringList{});
    }
}

TEST(ThreadDownloadTiles, PauseInTheMiddleFinishesTheCurrentRequestAndStopsTheClock) {
    Options o;
    Rig rig(o);
    rig.provider->parts = {{3, RectI{0, 3, 3, 4}}}; // (0,3) (1,3) (2,3)
    rig.provider->total = 3;
    // While the first request is in flight: 4 days pass, then the user pauses.
    rig.subsystem->onDownload = [&rig](int i) {
        if (i == 0) {
            rig.clock = 104.0;
            rig.real->pause();
        }
    };
    rig.start();
    ASSERT_TRUE(rig.waitEvent("paused"));
    // The current request finished and was counted; the next did not start.
    EXPECT_EQ(rig.subsystem->calls().size(), 1u);
    EXPECT_EQ(rig.real->processed(), 1);
    EXPECT_EQ(rig.real->downloaded(), 1);
    // Elapsed time stops: 4 days up to the pause (clock 100 -> 104), and not a second more while it lasts.
    EXPECT_DOUBLE_EQ(rig.real->elapsedTime(), 4.0);
    rig.clock = 200.0;
    EXPECT_DOUBLE_EQ(rig.real->elapsedTime(), 4.0);

    rig.real->resume();
    ASSERT_TRUE(rig.waitFinished());
    rig.worker.reset();
    EXPECT_EQ(rig.subsystem->calls().size(), 3u);
    EXPECT_EQ(rig.count("started"), 1);
    EXPECT_EQ(rig.count("paused"), 1);
    EXPECT_EQ(rig.real->processed(), 3);
    EXPECT_EQ(rig.lines(),
              (QStringList{processing(3, 0, 3), kDownloading, kOk, kPaused, processing(3, 1, 3), kDownloading,
                           kOk, processing(3, 2, 3), kDownloading, kOk, kCompleted}));
}

// === Proof 4: cancel ===

TEST(ThreadDownloadTiles, CancelWhileWaitingForAResultEndsTheWorker) {
    Rig rig;
    rig.subsystem->script = [](int) { return Reply{Mode::Hold, {}}; };
    rig.start();
    ASSERT_TRUE(rig.waitDownloads(1));
    // Both tokens handed to getRequestTask are the cancel token (:432-433): live before, stopped after.
    const auto before = rig.subsystem->calls();
    ASSERT_EQ(before.size(), 1u);
    EXPECT_TRUE(before[0].soft.stop_possible());
    EXPECT_TRUE(before[0].cancel.stop_possible());
    EXPECT_FALSE(before[0].soft.stop_requested());
    EXPECT_FALSE(before[0].cancel.stop_requested());

    EXPECT_LT(rig.cancelAndJoin(), 3000ms);
    EXPECT_TRUE(rig.real->finished());
    EXPECT_TRUE(before[0].soft.stop_requested());
    EXPECT_TRUE(before[0].cancel.stop_requested());
    EXPECT_EQ(rig.lines(), (QStringList{kProcessingTile, kDownloading})); // no result line, no "completed"
    EXPECT_EQ(rig.real->processed(), 0);
    EXPECT_EQ(rig.count("total 1"), 1);
}

TEST(ThreadDownloadTiles, CancelWhilePausedEndsTheWorkerWithoutResuming) {
    Options o;
    o.paused = true;
    o.timing = ThreadDownloadTiles::Timing{}; // the poll is 100 ms; the cancel does not wait for it
    Rig rig(o);
    rig.start();
    ASSERT_TRUE(rig.waitEvent("paused"));
    EXPECT_LT(rig.cancelAndJoin(), 3000ms);
    EXPECT_TRUE(rig.real->finished());
    EXPECT_EQ(rig.subsystem->calls().size(), 0u);
    EXPECT_EQ(rig.count("started"), 0); // the exit of the pause loop returns before SetStarted (:296)
    EXPECT_EQ(rig.lines(), (QStringList{kPaused}));
}

// The constructor registers the listener and runs it at once when a token is already stopped (:237-239): the
// worker is terminated before it starts, GetTasksList is never called (Execute exits on Terminated, :410).
TEST(ThreadDownloadTiles, CancelBeforeStartFinishesWithoutRequestOrTotal) {
    Options o;
    o.cancelStopped = true;
    Rig rig(o);
    ASSERT_TRUE(rig.runToEnd());
    EXPECT_TRUE(rig.real->finished());
    EXPECT_EQ(rig.provider->calls(), 0);
    EXPECT_EQ(rig.count("total 1"), 0);
    EXPECT_EQ(rig.subsystem->calls().size(), 0u);
    EXPECT_TRUE(rig.lines().isEmpty());
}

// Cancel requested while GetTasksList runs: the test after it (:431) returns before SetTotalToProcess.
TEST(ThreadDownloadTiles, CancelDuringGetTasksListEndsBeforeTheTotal) {
    Rig rig;
    rig.provider->onCall = [&rig] { rig.cancel.request_stop(); };
    ASSERT_TRUE(rig.runToEnd());
    EXPECT_TRUE(rig.real->finished());
    EXPECT_EQ(rig.provider->calls(), 1);
    EXPECT_EQ(rig.provider->workerIndex(), 2);
    EXPECT_EQ(rig.count("total 1"), 0);
    EXPECT_EQ(rig.subsystem->calls().size(), 0u);
    EXPECT_TRUE(rig.lines().isEmpty());
}

// === Proof 5: the constructor exits ===

TEST(ThreadDownloadTiles, UseDownloadOffLogsFinishesAndMakesNoRequest) {
    const QString line = u"Download of map Test Map disabled by map params"_s;
    for (int minLevel : {10, 11}) {
        SCOPED_TRACE(minLevel);
        Options o;
        o.useDownload = false;
        o.minLevel = minLevel;
        Rig rig(o);
        // The line is written by the constructor, before start().
        rig.make();
        EXPECT_EQ(rig.lines(), minLevel == 10 ? QStringList{line} : QStringList{}); // level 10 exactly
        rig.worker->start();
        ASSERT_TRUE(rig.waitFinished());
        rig.worker.reset();
        EXPECT_TRUE(rig.real->finished());
        EXPECT_EQ(rig.subsystem->calls().size(), 0u);
        EXPECT_EQ(rig.provider->calls(), 0);
        EXPECT_EQ(rig.count("total 1"), 0);
    }
}

TEST(ThreadDownloadTiles, DisabledSubsystemStateLogsItsReason) {
    const QString line = u"Download of map Test Map disabled. Reason: no network"_s;
    for (int minLevel : {10, 11}) {
        SCOPED_TRACE(minLevel);
        Options o;
        o.disableReason = u"no network"_s;
        o.minLevel = minLevel;
        Rig rig(o);
        rig.make();
        EXPECT_EQ(rig.lines(), minLevel == 10 ? QStringList{line} : QStringList{});
        rig.worker->start();
        ASSERT_TRUE(rig.waitFinished());
        rig.worker.reset();
        EXPECT_TRUE(rig.real->finished());
        EXPECT_EQ(rig.subsystem->calls().size(), 0u);
        EXPECT_EQ(rig.provider->calls(), 0);
        EXPECT_EQ(rig.count("total 1"), 0);
    }
}

// An application-closing token that is already stopped at construction runs the callback at once
// (:237-239 registers the listener, the notifier fires): the worker is terminated before it starts.
TEST(ThreadDownloadTiles, AppClosingBeforeConstructionTerminatesTheWorker) {
    Options o;
    o.appClosingStopped = true;
    Rig rig(o);
    ASSERT_TRUE(rig.runToEnd());
    EXPECT_TRUE(rig.real->finished());
    EXPECT_EQ(rig.provider->calls(), 0); // Execute exits on Terminated (:410) before GetTasksList
    EXPECT_EQ(rig.subsystem->calls().size(), 0u);
    EXPECT_TRUE(rig.lines().isEmpty());
}

// === Proof 7: order ===

TEST(ThreadDownloadTiles, TilesInIteratorOrderZoomAfterZoom) {
    Rig rig;
    rig.provider->parts = {{3, RectI{0, 3, 2, 5}}, {4, RectI{5, 6, 6, 8}}};
    rig.provider->total = 6;
    ASSERT_TRUE(rig.runToEnd());

    const auto calls = rig.subsystem->calls();
    const std::vector<std::pair<PointI, int>> expected{{{0, 3}, 3}, {{1, 3}, 3}, {{0, 4}, 3},
                                                       {{1, 4}, 3}, {{5, 6}, 4}, {{5, 7}, 4}};
    ASSERT_EQ(calls.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(calls[i].tile, expected[i].first) << i;
        EXPECT_EQ(calls[i].zoom, expected[i].second) << i;
    }
    // SetTotalToProcess once, first; SetZoom once per iterator, before its tiles.
    const std::vector<std::string> events{"total 6",          "zoom 3",
                                          "answer 0",         "downloaded 0,3 5",
                                          "processed 0,3",    "answer 0",
                                          "downloaded 1,3 5", "processed 1,3",
                                          "answer 0",         "downloaded 0,4 5",
                                          "processed 0,4",    "answer 0",
                                          "downloaded 1,4 5", "processed 1,4",
                                          "zoom 4",           "answer 0",
                                          "downloaded 5,6 5", "processed 5,6",
                                          "answer 0",         "downloaded 5,7 5",
                                          "processed 5,7",    "finish"};
    EXPECT_EQ(rig.progress->events(), events);
    const auto lines = rig.lines();
    ASSERT_EQ(lines.size(), 19); // 6 tiles x (Processing, Downloading, (Ok!)) + completed
    EXPECT_EQ(lines.last(), kCompleted);
    EXPECT_EQ(lines[0], processing(3, 0, 3));
    EXPECT_EQ(lines[12], processing(4, 5, 6)); // zoom 4 is printed as z5
    EXPECT_EQ(rig.provider->workerIndex(), 2);
}

// === Proof 8: exceptions ===

// Finding b of the ticket: a Result error of tileInfoEx is a legacy exception raised OUTSIDE the inner try
// (:318): the outer handler logs "<type>: <message>" at level 10 and the worker finishes with the area
// undone.
TEST(ThreadDownloadTiles, StorageErrorEndsTheWorkerWithTheTypeAndMessage) {
    const QString line = u"std::runtime_error: disk gone"_s;
    for (int minLevel : {10, 11}) {
        SCOPED_TRACE(minLevel);
        Options o;
        o.minLevel = minLevel;
        Rig rig(o);
        rig.provider->parts = {{3, RectI{0, 3, 2, 5}}}; // 4 tiles, only the first is tried
        rig.provider->total = 4;
        rig.storage->failure = cg::core::Error(cg::core::ErrorCode::IoError, u"disk gone"_s);
        ASSERT_TRUE(rig.runToEnd());
        EXPECT_TRUE(rig.real->finished());
        EXPECT_EQ(rig.storage->queries().size(), 1u);
        EXPECT_EQ(rig.subsystem->calls().size(), 0u);
        EXPECT_EQ(rig.real->processed(), 0);
        if (minLevel == 10) {
            EXPECT_EQ(rig.lines(), (QStringList{line}));
        } else {
            EXPECT_TRUE(rig.lines().isEmpty());
        }
    }
}

TEST(ThreadDownloadTiles, ThrowingTaskProviderEndsTheWorkerWithTheTypeAndMessage) {
    const QString line = u"std::out_of_range: no zoom"_s;
    for (int minLevel : {10, 11}) {
        SCOPED_TRACE(minLevel);
        Options o;
        o.minLevel = minLevel;
        Rig rig(o);
        rig.provider->throwOutOfRange = "no zoom";
        ASSERT_TRUE(rig.runToEnd());
        EXPECT_TRUE(rig.real->finished());
        EXPECT_EQ(rig.count("total 1"), 0);
        EXPECT_EQ(rig.subsystem->calls().size(), 0u);
        if (minLevel == 10) {
            EXPECT_EQ(rig.lines(), (QStringList{line}));
        } else {
            EXPECT_TRUE(rig.lines().isEmpty());
        }
    }
}

// === Proof 9: destruction ===

// After cancel the destructor returns; a late finish from the queue (the notifier kept by the subsystem)
// finds the notifier disabled and does nothing; the stop tokens no longer reach the destroyed object.
TEST(ThreadDownloadTiles, DestructionAfterCancelThenLateFinishChangesNothing) {
    Rig rig;
    rig.subsystem->script = [](int) { return Reply{Mode::Hold, {}}; };
    rig.start();
    ASSERT_TRUE(rig.waitDownloads(1));
    const auto notifier = rig.subsystem->notifier();
    const auto task = rig.subsystem->task(0);
    ASSERT_NE(notifier, nullptr);
    EXPECT_TRUE(notifier->enabled());

    EXPECT_LT(rig.cancelAndJoin(), 3000ms);
    EXPECT_FALSE(notifier->enabled());
    const auto linesBefore = rig.lines();
    notifier->notify(*task, withKind(ResultKind::Ok, QByteArray(9, 'y'), "image/png")(task->tileRequest()));
    rig.appClosing.request_stop(); // nobody listens any more
    EXPECT_EQ(rig.lines(), linesBefore);
    EXPECT_EQ(rig.real->downloaded(), 0);
    EXPECT_EQ(rig.real->processed(), 0);
    EXPECT_EQ(rig.downloadInfo->tileCount(), 0u);
}

TEST(ThreadDownloadTiles, DestructionOfAFinishedOrNeverStartedWorker) {
    {
        Rig rig;
        ASSERT_TRUE(rig.runToEnd()); // the worker finished by itself, then the destructor joined it
        EXPECT_EQ(rig.real->processed(), 1);
        EXPECT_FALSE(rig.subsystem->notifier()->enabled());
        rig.appClosing.request_stop(); // no callback of the destroyed worker
    }
    {
        Rig rig;
        rig.make(); // never started: nothing to join
        rig.worker.reset();
        EXPECT_EQ(rig.subsystem->calls().size(), 0u);
        EXPECT_FALSE(rig.real->finished());
    }
}

// Only "TSan is quiet": a stop request on a foreign thread races with the destructor, which waits for a
// callback that is running.
TEST(ThreadDownloadTiles, AppClosingRacingWithTheDestructor) {
    Rig rig;
    rig.subsystem->script = [](int) { return Reply{Mode::Hold, {}}; };
    rig.start();
    ASSERT_TRUE(rig.waitDownloads(1));
    rig.cancel.request_stop();
    std::latch go(2);
    std::thread closer([&] {
        go.arrive_and_wait();
        rig.appClosing.request_stop();
    });
    go.arrive_and_wait();
    rig.worker.reset();
    closer.join();
    EXPECT_TRUE(rig.real->finished());
}

// === T-0271: the streak of connection errors ===

// Connection error kinds: UnknownError, LoadError (5xx, 403, 429), NoConnectToServer. Every other result that
// carries a download result resets the streak. goNext is on, so every failed tile is skipped (the same tile
// is not requested again).
TEST(ThreadDownloadTiles, ConnectionErrorStreakCountsAndResets) {
    struct Step {
        ResultKind kind;
        const char* answer;
        qint64 streak;
    };
    const std::vector<Step> steps{
        {ResultKind::UnknownError, "answer 1", 1}, {ResultKind::LoadError, "answer 1", 2},
        {ResultKind::LoadError, "answer 1", 3},    {ResultKind::DataNotExists, "answer 0", 0},
        {ResultKind::LoadError, "answer 1", 1},    {ResultKind::Ok, "answer 0", 0},
        {ResultKind::UnknownError, "answer 1", 1}, {ResultKind::BadContentType, "answer 0", 0},
        {ResultKind::LoadError, "answer 1", 1},
    };
    Rig rig;
    rig.provider->parts = {{3, RectI{0, 0, static_cast<int>(steps.size()), 1}}};
    rig.provider->total = static_cast<qint64>(steps.size());
    rig.subsystem->script = [steps](int i) {
        const ResultKind k = steps[static_cast<std::size_t>(i)].kind;
        return finishWith(k == ResultKind::Ok ? okBytes(5) : withKind(k));
    };
    ASSERT_TRUE(rig.runToEnd());
    std::vector<std::string> answers;
    for (const auto& e : rig.progress->events()) {
        if (e.starts_with("answer")) {
            answers.push_back(e);
        }
    }
    std::vector<std::string> expected;
    for (const auto& st : steps) {
        expected.emplace_back(st.answer);
    }
    EXPECT_EQ(answers, expected);
    EXPECT_EQ(rig.real->processed(), static_cast<qint64>(steps.size()));
    EXPECT_EQ(rig.real->connectionErrorStreak(), steps.back().streak); // the sum of the +1 / reset steps
}

// A tile that is skipped without a request neither counts nor resets.
TEST(ThreadDownloadTiles, SkippedTileLeavesTheStreakAlone) {
    Rig rig;
    rig.provider->parts = {{3, RectI{0, 0, 2, 1}}};
    rig.provider->total = 2;
    rig.storage->infos[{1, 0}] = infoOf(TileInfoType::Exists, kLoad); // replaceExist off: skipped
    rig.subsystem->script = [](int) { return finishWith(withKind(ResultKind::LoadError)); };
    ASSERT_TRUE(rig.runToEnd());
    EXPECT_EQ(rig.subsystem->calls().size(), 1u);
    EXPECT_EQ(rig.real->connectionErrorStreak(), 1);
    EXPECT_EQ(rig.real->processed(), 2);
}

// === T-0356: the class "the frame is not served yet" ===

Maker withStatus(ResultKind kind, quint32 status) {
    return [=](const ITileRequestPtr& request) -> ITileRequestResultPtr {
        cg::net::DownloadResult r = dl(kind);
        r.statusCode = status;
        return std::make_shared<cg::maptype::TileRequestResultOk>(requestOf(request), r);
    };
}

// 404 / the empty tile (DataNotExists) is Missing (2), the ban tile and HTTP 5xx are Unavailable (1); both
// are in the class; an answer with no status,
// a 4xx other than 404, a refused connection, a wrong content type and a stored tile are not.
TEST(ThreadDownloadTiles, NotServedClassIsDataNotExistsBannedAndHttp5xx) {
    struct Step {
        ResultKind kind;
        quint32 status;
        int expected;
    };
    const std::vector<Step> steps{
        {ResultKind::DataNotExists, 404, 2}, {ResultKind::Ok, 200, 0},
        {ResultKind::Banned, 0, 1},          {ResultKind::LoadError, 502, 1},
        {ResultKind::LoadError, 500, 1},     {ResultKind::LoadError, 599, 1},
        {ResultKind::LoadError, 499, 0},     {ResultKind::LoadError, 600, 0},
        {ResultKind::LoadError, 403, 0},     {ResultKind::LoadError, 0, 0},
        {ResultKind::UnknownError, 0, 0},    {ResultKind::NoConnectToServer, 0, 0},
        {ResultKind::LoadError, 503, 1},     {ResultKind::BadContentType, 200, 0},
        {ResultKind::LoadError, 504, 1},     {ResultKind::NotNecessary, 200, 0},
    };
    Rig rig;
    rig.provider->parts = {{3, RectI{0, 0, static_cast<int>(steps.size()), 1}}};
    rig.provider->total = static_cast<qint64>(steps.size());
    rig.subsystem->script = [steps](int i) {
        if (static_cast<std::size_t>(i) >= steps.size()) {
            return finishWith(okBytes(5)); // a retried tile (Banned, NoConnectToServer) ends here
        }
        const Step& st = steps[static_cast<std::size_t>(i)];
        return finishWith(st.kind == ResultKind::Ok ? okBytes(5) : withStatus(st.kind, st.status));
    };
    ASSERT_TRUE(rig.runToEnd());
    std::vector<int> expected;
    for (const Step& st : steps) {
        expected.push_back(st.expected);
    }
    const std::vector<int> calls = rig.progress->notServedCalls();
    ASSERT_GE(calls.size(), expected.size());
    EXPECT_EQ(std::vector<int>(calls.begin(), calls.begin() + static_cast<std::ptrdiff_t>(expected.size())),
              expected);
}

// A tile skipped because it is stored in this frame says "served" (Other); one skipped because a
// TNE row of this frame holds it says "not served" (Missing); the counts of the real progress info follow.
TEST(ThreadDownloadTiles, SkippedTilesSayServedWhenStoredAndNotServedWhenTne) {
    Rig rig;
    rig.provider->parts = {{3, RectI{0, 0, 3, 1}}};
    rig.provider->total = 3;
    rig.storage->infos[{0, 0}] = infoOf(TileInfoType::TneExists, kLoad);
    rig.storage->infos[{1, 0}] = infoOf(TileInfoType::TneExists, kLoad);
    rig.storage->infos[{2, 0}] = infoOf(TileInfoType::Exists, kLoad);
    ASSERT_TRUE(rig.runToEnd());
    EXPECT_EQ(rig.subsystem->calls().size(), 0u);
    EXPECT_EQ(rig.progress->notServedCalls(), (std::vector<int>{2, 2, 0}));
    const cg::region::FrameAnswers answers = rig.real->frameAnswers();
    EXPECT_EQ(answers.inClass, 2);
    EXPECT_EQ(answers.other, 1);
    EXPECT_EQ(answers.unavailableRun, 0);
}

// === Proof 10: LR-5 / LR-6 ===

// LR-6: the application is closing but cancel was not requested. The waiting tile is counted as processed
// with a null result (the result slot was cleared before the request, :383, so the earlier tile's result is
// not reused); the next tile's request is made with a disabled notifier, so it never finishes; only cancel
// ends it.
TEST(ThreadDownloadTiles, AppClosingWithoutCancelCountsTheWaitingTileAndStallsTheNext) {
    Rig rig;
    rig.provider->parts = {{3, RectI{0, 3, 3, 4}}}; // (0,3) (1,3) (2,3)
    rig.provider->total = 3;
    rig.subsystem->script = [](int i) { return i == 0 ? finishWith(okBytes(5)) : Reply{Mode::Hold, {}}; };
    rig.start();
    ASSERT_TRUE(rig.waitDownloads(2)); // tile 0 downloaded, tile 1 is waiting for its result
    rig.appClosing.request_stop();
    ASSERT_TRUE(rig.waitCalls(3));
    ASSERT_TRUE(rig.waitDownloads(3));
    ASSERT_TRUE(rig.waitEvent("processed 1,3"));
    EXPECT_EQ(rig.real->processed(), 2);
    EXPECT_EQ(rig.real->downloaded(), 1); // tile 1 was counted without a download
    EXPECT_EQ(rig.subsystem->calls()[2].tile, PointI(2, 3));

    // The third request can no longer finish: the notifier is disabled, a late delivery is dropped.
    const auto notifier = rig.subsystem->notifier();
    EXPECT_FALSE(notifier->enabled());
    const auto task = rig.subsystem->task(2);
    notifier->notify(*task, withKind(ResultKind::Ok, QByteArray(5, 'x'), "image/png")(task->tileRequest()));
    EXPECT_EQ(rig.real->downloaded(), 1);
    EXPECT_FALSE(rig.real->finished());

    EXPECT_LT(rig.cancelAndJoin(), 3000ms); // only cancel ends it
    EXPECT_TRUE(rig.real->finished());
    EXPECT_EQ(rig.real->processed(), 2);
    EXPECT_EQ(rig.real->downloaded(), 1);
    EXPECT_EQ(rig.lines(),
              (QStringList{processing(3, 0, 3), kDownloading, kOk, processing(3, 1, 3), kDownloading,
                           u"Error: Unknown download result!"_s, processing(3, 2, 3), kDownloading}));
}

// === Proof 11: results delivered from the queue's threads ===

// The result slot is written by a foreign thread and read by the worker: 40 tiles, each finished from its own
// thread. Proves the counters and that TSan is quiet about the slot.
TEST(ThreadDownloadTiles, ResultsDeliveredFromOtherThreads) {
    Rig rig;
    rig.provider->parts = {{3, RectI{0, 0, 8, 5}}};
    rig.provider->total = 40;
    rig.subsystem->script = [](int) { return Reply{Mode::FinishOnOtherThread, okBytes(5)}; };
    ASSERT_TRUE(rig.runToEnd());
    EXPECT_EQ(rig.real->downloaded(), 40);
    EXPECT_EQ(rig.real->processed(), 40);
    EXPECT_EQ(rig.real->downloadSize(), 200u);
    EXPECT_EQ(rig.downloadInfo->tileCount(), 40u);
    EXPECT_EQ(rig.downloadInfo->size(), 200u);
    EXPECT_EQ(rig.subsystem->calls().size(), 40u);
    EXPECT_EQ(rig.lines().last(), kCompleted);
}
