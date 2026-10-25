// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Network/u_TileDownloaderWithQueue.pas:64-94, Src/u_InterfaceQueue.pas:78-185,
// Src/Network/u_TileRequestProcessorPool.pas:95-190, Src/Network/u_TileDownloaderList.pas:186-215.
// docs/specs/net.md §2.15. FEAT-NET-010, FEAT-NET-015.
#include "cg/net/download_queue.h"

#include "cg/core/log.h"
#include "cg/net/downloader_ttl.h"
#include "cg/net/retry_downloader.h"

#include <exception>
#include <thread>
#include <utility>

namespace cg::net {

DownloadQueue::DownloadQueue(std::shared_ptr<IDownloaderFactory> factory, cg::core::Notifier& gc,
                             TileDownloaderConfig& config, int capacity, SteadyClockFn clock,
                             SlotStateFactory slotStateFactory)
    : factory_(std::move(factory)), gc_(gc), config_(config), capacity_(capacity), clock_(std::move(clock)),
      ring_(static_cast<std::size_t>(capacity)), slotStateFactory_(std::move(slotStateFactory)) {
    if (!clock_) {
        clock_ = [] { return std::chrono::steady_clock::now(); };
    }
}

void DownloadQueue::subscribe() {
    gcSub_ = gc_.subscribe([weak = weak_from_this()] {
        if (const auto self = weak.lock()) {
            self->onTick();
        }
    });
    configSub_ = config_.changed().subscribe([weak = weak_from_this()] {
        if (const auto self = weak.lock()) {
            self->onConfigChange();
        }
    });
}

DownloadQueue::~DownloadQueue() {
    shutdown();
    // "drains the queue": drop whatever is still sitting in the ring buffer now instead of
    // leaving it to ring_'s own member destruction, mirroring TInterfaceQueue.Destroy releasing
    // every remaining FRequestArray entry.
    const std::lock_guard locker(queueMutex_);
    for (auto& job : ring_) {
        job = Job{};
    }
    head_ = tail_ = count_ = 0;
}

void DownloadQueue::push(Job job) {
    {
        std::unique_lock lock(queueMutex_);
        notFull_.wait(lock, [this] { return count_ < capacity_ || shuttingDown_; });
        if (shuttingDown_) {
            return; // legacy: Push's WaitForMultipleObjects falls through on the stop event, no-op
        }
        ring_[static_cast<std::size_t>(tail_)] = std::move(job);
        tail_ = (tail_ + 1) % capacity_;
        ++count_;
        notEmpty_.notify_one();
    }
    initWorkersIfNeeded();
}

bool DownloadQueue::isEmpty() const {
    const std::lock_guard locker(queueMutex_);
    return count_ == 0;
}

void DownloadQueue::shutdown() {
    {
        const std::lock_guard locker(queueMutex_);
        shuttingDown_ = true;
    }
    notFull_.notify_all();
    terminateWorkers(
        /*dropSlots=*/true); // also wakes every worker blocked in pull(), via its shared stop_source
}

std::optional<DownloadQueue::Job> DownloadQueue::pull(const std::stop_token& stop) {
    std::unique_lock lock(queueMutex_);
    // condition_variable's stop_token overload (C++20) registers/unregisters its own
    // std::stop_callback around the wait, so a worker-generation teardown (terminateWorkers()'s
    // stop_source::request_stop()) wakes this promptly without hand-rolled wake-on-cancel plumbing.
    notEmpty_.wait(lock, stop, [this, &stop] { return count_ > 0 || stop.stop_requested(); });
    if (stop.stop_requested()) {
        // Termination wins over any remaining backlog, deterministically: legacy's own Pull is
        // actually racy here (WaitForMultipleObjects on [ReadyRequestSemaphore, StopEvent] with no
        // priority guarantee when both fire), so there is no legacy contract to preserve either
        // way. Whatever is still queued when a worker generation stops is left for
        // ~DownloadQueue()/the next generation, never silently run past a stop request.
        return std::nullopt;
    }
    Job job = std::move(ring_[static_cast<std::size_t>(head_)]);
    ring_[static_cast<std::size_t>(head_)] = Job{};
    head_ = (head_ + 1) % capacity_;
    --count_;
    notFull_.notify_one();
    return job;
}

void DownloadQueue::workerLoop(Slot& slot, const std::stop_token& stop) {
    for (;;) {
        std::optional<Job> job = pull(stop);
        if (!job) {
            return;
        }
        // Legacy lets the exception end this thread (u_TileRequestQueueProcessorThread.pas:111-119,
        // `finally SetFinished(nil)`, no `except`; L-200) and a map whose workers all died stalls.
        // The port logs and goes on with the next job (D-87, docs/specs/net.md §2.15). The job is already
        // out of the ring (pull() did the bookkeeping), the slot's run lock is a scoped guard inside
        // RetryDownloader::run and is released by unwinding, so nothing is half-updated here.
        try {
            (*job)(*slot.loop, slot.state.get(), stop);
        } catch (const std::exception& e) {
            qCCritical(cg::core::logNet) << "DownloadQueue: a job threw, worker continues:" << e.what();
        } catch (...) {
            qCCritical(cg::core::logNet) << "DownloadQueue: a job threw, worker continues: unknown exception";
        }
    }
}

void DownloadQueue::initWorkersIfNeeded() {
    const std::lock_guard locker(workersMutex_);
    usedSinceLastTick_ = true; // InitThreadsIfNeed stamps use-time unconditionally, every push
    if (workersInitialized_) {
        return;
    }

    const std::shared_ptr<const TileDownloaderSettings> settings = config_.settings();
    const unsigned count = settings->enabled ? settings->maxConnectToServerCount : 0;

    if (!slotsBuilt_) {
        // Built completely BEFORE any thread starts: a throwing factory leaves nothing running.
        std::vector<std::shared_ptr<Slot>> slotList;
        slotList.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            DownloaderOptions options;
            options.allowCookies = settings->allowUseCookie;
            options.tryDetectContentType = settings->detectMimeType;
            // Ports TTileDownloaderList.CreateDownloader (u_TileDownloaderList.pas:143-163): each
            // slot gets its own TDownloaderHttpWithTTL + TTileDownloaderSimple chain. The config
            // values are the "static snapshot" RetryDownloader keeps for its whole lifetime
            // (mantis #3029) -- a config change drops the slots (onConfigChange), so a fresh
            // snapshot is taken only when the list is rebuilt, never mid-lifetime.
            auto ttl = std::make_shared<TtlDownloader>(factory_, options, clock_);
            ttl->subscribeGcTick(gc_);
            auto slot = std::make_shared<Slot>();
            slot->loop = std::make_shared<RetryDownloader>(ttl, settings->inet->downloadTryCount,
                                                           settings->inet->sleepOnResetConnectionMs,
                                                           settings->waitIntervalMs);
            // One state object per slot (u_TileDownloaderList.pas:146-168), never shared; made from the
            // same downloader the slot's retry loop sends through (:143-163: one VDownloader for both).
            slot->state = slotStateFactory_ ? slotStateFactory_(ttl) : SlotState{};
            slotList.push_back(std::move(slot));
        }
        slots_ = std::move(slotList);
        slotsBuilt_ = true;
    }

    workersStop_ = std::stop_source{};
    const std::stop_token token = workersStop_.get_token();
    workerThreads_.reserve(slots_.size());
    for (const std::shared_ptr<Slot>& slot : slots_) {
        // std::thread, not QThread: see download_queue.h's file comment -- QThread::wait() does
        // not give ThreadSanitizer a real join on this system's (non-instrumented) Qt build, which
        // deterministically corrupts TSan's thread registry on this exact teardown+recreate cycle.
        workerThreads_.emplace_back([this, slot, token] { workerLoop(*slot, token); });
    }
    workersInitialized_ = true;
}

DownloadQueue::TakenWorkers DownloadQueue::takeWorkersLocked(bool dropSlots) {
    TakenWorkers taken;
    if (dropSlots) {
        taken.slotList = std::move(slots_);
        slots_.clear();
        slotsBuilt_ = false;
    }
    if (!workersInitialized_) {
        return taken;
    }
    workersStop_.request_stop(); // wakes every pull() blocked on this generation's token
    taken.threads = std::move(workerThreads_);
    workerThreads_.clear();
    workersInitialized_ = false;
    usedSinceLastTick_ = false;
    lastUse_.reset();
    return taken;
}

void DownloadQueue::joinWorkers(TakenWorkers& taken) {
    // Joined outside the lock: a slow (but cooperatively cancelling) in-flight request must not
    // stall a concurrent push()'s initWorkersIfNeeded() from reading workersInitialized_. Legacy
    // only flags the threads (Terminate) and never waits: D-71 (T-0048).
    for (auto& thread : taken.threads) {
        thread.join();
    }
}

void DownloadQueue::terminateWorkers(bool dropSlots) {
    TakenWorkers taken;
    {
        const std::lock_guard locker(workersMutex_);
        taken = takeWorkersLocked(dropSlots);
    }
    joinWorkers(taken);
}

void DownloadQueue::onTick() {
    // Ports TListenerTTLCheck.Notification (u_ListenerTime.pas:90-108) with TTL 60 s, driving
    // OnTTLTrim (u_TileRequestProcessorPool.pas:185-192). Runs on the GC thread.
    std::unique_lock lock(workersMutex_);
    if (usedSinceLastTick_) {
        // A push (InitThreadsIfNeed's UpdateUseTime) happened since the last tick: the idle clock
        // restarts at this tick (FUseFlag.CheckFlagAndReset -> FLastUseTime := ANow).
        usedSinceLastTick_ = false;
        lastUse_ = clock_();
        return;
    }
    if (!lastUse_ || clock_() < *lastUse_ + kWorkerIdleTimeout) {
        return;
    }
    lastUse_.reset(); // legacy: FLastUseTime := 0 after OnTrimByTTL
    bool empty = false;
    {
        const std::lock_guard queueLocker(queueMutex_);
        empty = count_ == 0;
    }
    if (!empty) {
        // OnTTLTrim: "if not FTileRequestQueue.IsEmpty then UpdateUseTime else TerminateDownloaders".
        usedSinceLastTick_ = true;
        return;
    }
    if (afterIdleCheckHook_) {
        afterIdleCheckHook_();
    }
    // workersMutex_ stays held from the emptiness check to the hand-over of the workers: a push that
    // lands in between either was seen above, or finds the worker set gone in initWorkersIfNeeded()
    // and builds a new one. Legacy releases the window (IsEmpty, then TerminateDownloaders) and can
    // strand the job: L-169 / D-72 (T-0048).
    TakenWorkers taken = takeWorkersLocked(/*dropSlots=*/false); // threads only, as legacy
    lock.unlock();
    joinWorkers(taken);
}

void DownloadQueue::onConfigChange() {
    terminateWorkers(
        /*dropSlots=*/true); // ports OnDownloadersListChange -> TerminateDownloaders, unconditional
}

} // namespace cg::net
