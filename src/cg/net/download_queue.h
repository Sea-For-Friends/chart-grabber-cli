// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Network/u_TileDownloaderWithQueue.pas:64-94, Src/u_InterfaceQueue.pas:78-185,
// Src/Network/u_TileRequestProcessorPool.pas:95-190, Src/Network/u_TileDownloaderList.pas:186-215.
// docs/specs/net.md §2.15. FEAT-NET-010, FEAT-NET-015.
//
// One DownloadQueue per map: a bounded ring buffer (capacity 256, u_TileDownloadSubsystem.pas:381)
// plus a lazily-created worker set sized off TileDownloaderConfig::maxConnectToServerCount(),
// 1..64, or 0 while the map's downloading is disabled ("workerCount() == 0" is legal: the queue
// still accepts jobs, it just runs none, u_TileDownloaderList.pas:197-210). Workers are dedicated,
// never-shared OS threads, never a QThreadPool (ADR-0004 §Threading: a pool would scatter one
// map's connections, each of which caches a persistent curl handle, across arbitrary threads).
// Each worker is a plain std::thread rather than QThread -- verified (a minimal, application-code-
// free QThread::create()+start()+wait()+destroy loop reproduces it identically) that this system's
// prebuilt, non-TSan-instrumented Qt does not give ThreadSanitizer a real join: QThread::wait()
// returns without the underlying native thread's exit ever becoming visible to TSan's thread
// registry, so reusing that native thread id for the next generation's worker (exactly what the
// 60 s idle teardown + recreate-on-demand contract requires) deterministically aborts the TSan
// build ("CHECK failed: sanitizer_thread_registry.cpp:186"), regardless of how much real time
// separates the teardown from the recreate. The equivalent std::thread create/join/destroy loop is
// clean under the same TSan build. ADR-0004's actual target is QThreadPool's arbitrary-thread
// scheduling, not the QThread class specifically; std::thread keeps the same one-thread-per-worker,
// never-shared property. Recorded as a deviation, docs/parity/deviations.md. Each worker owns one
// RetryDownloader-over-TtlDownloader-over-`factory` chain, assembled here the way
// TTileDownloaderList.CreateDownloader assembled TDownloaderHttpWithTTL + TTileDownloaderSimple.

#include "cg/core/notifier.h"
#include "cg/net/downloader.h"
#include "cg/net/downloader_ttl.h"
#include "cg/net/retry_downloader.h"
#include "cg/net/tile_downloader_config.h"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace cg::net {

inline constexpr int kQueueCapacity = 256;                             // u_TileDownloadSubsystem.pas:381
inline constexpr std::chrono::milliseconds kWorkerIdleTimeout{60'000}; // u_TileRequestProcessorPool.pas:106

// Must be owned by a std::shared_ptr and subscribe() called once after construction: the GC-tick and
// config slots hold only a weak reference, so a tick racing the destructor is safe (T-0046 race
// class). Ticks run on the application GC thread (T-0048); every member onTick() touches is guarded
// by workersMutex_ (+ queueMutex_ for the emptiness check), the same locks push() takes.
// For the app: create with make_shared AND call subscribe() once (an unsubscribed queue never trims
// its workers); the notifier must outlive the queue; a job must never hold the last shared_ptr to its
// own queue (the destructor would join the worker thread it runs on).
class DownloadQueue final : public std::enable_shared_from_this<DownloadQueue> {
public:
    // The job gets its slot's retry loop and the slot's state (opaque to net: whatever the
    // SlotStateFactory made from the slot's downloader -- maptype's builder + LastResponseInfo,
    // u_TileDownloaderList.pas: 146-168; nullptr without a factory). Two slots never share a state object; it
    // lives as long as the slot: it survives the idle trim (threads only are dropped) and is destroyed on a
    // config change or shutdown. `stop` is the HARD cancel (worker generation / app closing); the job brings
    // its own soft cancel (per task) to RetryDownloader::run. A Job must not throw. If the
    // contract is broken, workerLoop catches the exception (std::exception or anything else), logs it
    // with qCCritical and serves the next job; the slot, its run lock and the queue stay intact. Legacy
    // ends that worker thread instead (D-87, docs/specs/net.md §2.15) but its `finally` still calls
    // SetFinished(nil) (u_TileRequestQueueProcessorThread.pas:118); the catch here cannot, the job is
    // opaque, so finishing the task on EVERY path (a null result when the body did not complete) is the
    // job's duty -- otherwise the task's waiter is never notified.
    using Job = std::function<void(RetryDownloader&, void* slotState, std::stop_token)>;
    using SlotState = std::shared_ptr<void>;
    // Called once per slot, under workersMutex_, with the slot's TtlDownloader -- the very object the slot's
    // RetryDownloader sends through (legacy: ONE TDownloaderHttpWithTTL per slot, given to both the
    // TTileDownloadRequestBuilderLazy and the TTileDownloaderSimple, u_TileDownloaderList.pas:143-168).
    // Never null. The state may keep it for the script's nested HTTP; the slot's thread uses it for the
    // build and then for the transfer, never both at once.
    using SlotStateFactory = std::function<SlotState(const IDownloaderPtr&)>;

    // `gc` is the application notifier (legacy AGCNotifier, u_TileRequestProcessorPool.pas:86); it
    // must outlive the queue. `clock` defaults to steady_clock::now; tests inject their own.
    DownloadQueue(std::shared_ptr<IDownloaderFactory> factory, cg::core::Notifier& gc,
                  TileDownloaderConfig& config, int capacity = kQueueCapacity, SteadyClockFn clock = {},
                  SlotStateFactory slotStateFactory = {});
    // Ports FGCNotifier.Add(FTTLListener) + FDownloaderList.ChangeNotifier.Add (:104-107).
    void subscribe();
    ~DownloadQueue(); // terminates workers, drains the queue
    DownloadQueue(const DownloadQueue&) = delete;
    DownloadQueue& operator=(const DownloadQueue&) = delete;

    // Blocks while the queue is full (legacy: a capacity semaphore + ring buffer,
    // u_InterfaceQueue.pas:81-96). Starts the workers on the first push (InitThreadsIfNeed) and
    // stamps the worker set's use-time on every push, whether or not this push created them. Once
    // shutdown() has fired, push() silently drops the job instead of blocking forever, mirroring
    // the legacy stop-event branch of Push's WaitForMultipleObjects.
    void push(Job job);

    [[nodiscard]] bool isEmpty() const;

    // Idempotent. Wakes every blocked push()/worker, tears the worker set down, and drains
    // whatever is still queued. Also run by the destructor.
    void shutdown();

    // Test seam (regression test of the stranded-job window): called by the idle trim while it holds
    // workersMutex_, after it decided the queue is empty and before it takes the workers away.
    void setAfterIdleCheckHookForTest(std::function<void()> hook) { afterIdleCheckHook_ = std::move(hook); }

private:
    void onTick(); // GC subscription: TListenerTTLCheck(OnTTLTrim, 60000),
                   // u_TileRequestProcessorPool.pas:106,185-192
    // One download slot (legacy TTileDownloaderList item): the retry loop over its TTL downloader
    // plus the opaque state. Threads hold shared_ptrs, so a slot outlives a worker still finishing
    // its job after the slot was dropped.
    struct Slot {
        std::shared_ptr<RetryDownloader> loop;
        SlotState state;
    };
    struct TakenWorkers {
        std::vector<std::thread> threads;
        std::vector<std::shared_ptr<Slot>>
            slotList; // only filled when the slots are dropped (config/shutdown)
    };
    // `dropSlots`: also hand over the slots. The idle trim passes false: legacy TerminateDownloaders
    // drops THREADS only (u_TileRequestProcessorPool.pas:194-217) and InitThreadsIfNeed gives the
    // same list items to the fresh threads (:141-150); the list is rebuilt only on a config/state
    // change (u_TileDownloaderList.pas:186-225).
    [[nodiscard]] TakenWorkers takeWorkersLocked(bool dropSlots); // caller holds workersMutex_
    static void joinWorkers(TakenWorkers& taken);
    void onConfigChange(); // TileDownloaderConfig::changed() subscription: OnDownloadersListChange
    void initWorkersIfNeeded();
    void terminateWorkers(bool dropSlots);
    void workerLoop(Slot& slot, const std::stop_token& stop);
    [[nodiscard]] std::optional<Job> pull(const std::stop_token& stop);

    std::shared_ptr<IDownloaderFactory> factory_;
    cg::core::Notifier& gc_; // the application tick; onTick() and every TtlDownloader subscribe to it
    TileDownloaderConfig& config_;
    const int capacity_;

    SteadyClockFn clock_;
    std::function<void()> afterIdleCheckHook_; // set before any tick; test only

    // The bounded ring buffer (ports TInterfaceQueue). One mutex covers both ends, unlike
    // legacy's separate FHeadCS/FTailCS -- this module has no throughput requirement that would
    // make a single lock a bottleneck. std::mutex/std::condition_variable, not QMutex/QWaitCondition
    // (see retry_downloader.h). notEmpty_ is condition_variable_any, not plain condition_variable:
    // libstdc++ only implements the stop_token-aware wait() overload pull() below needs on the
    // `_any` template (it takes any Lock type); notFull_ never needs a stop_token, so it stays the
    // plain, slightly cheaper condition_variable.
    mutable std::mutex queueMutex_;
    std::condition_variable notFull_;
    std::condition_variable_any notEmpty_;
    std::vector<Job> ring_;
    int head_ = 0;
    int tail_ = 0;
    int count_ = 0;
    bool shuttingDown_ = false;

    // The worker set (ports TTileRequestProcessorPool). Guarded by its own mutex, separate from
    // queueMutex_, so a slow worker-teardown never blocks push()/pull()'s hot path.
    std::mutex workersMutex_;
    bool workersInitialized_ = false;
    bool usedSinceLastTick_ = false; // legacy FUseFlag: stamped by every push, consumed by the next tick
    std::optional<std::chrono::steady_clock::time_point> lastUse_; // legacy FLastUseTime (0 = none)
    std::stop_source workersStop_;
    std::vector<std::thread> workerThreads_;
    std::vector<std::shared_ptr<Slot>> slots_; // survive the idle trim; dropped by config change/shutdown
    bool slotsBuilt_ = false;                  // slots_ may legitimately be empty (downloading disabled)
    SlotStateFactory slotStateFactory_;

    // Declared LAST deliberately (destructor-order siblings, per downloader_ttl.h): torn down
    // FIRST, before the state above, so a tick or config-change already in flight can never touch
    // a half-destroyed DownloadQueue.
    cg::core::Subscription gcSub_;
    cg::core::Subscription configSub_;
};

} // namespace cg::net
