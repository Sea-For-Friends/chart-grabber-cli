// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-010, FEAT-NET-015 -- docs/specs/net.md §2.15, §7.12.
//
// Test-count ruling (tech-lead-rulings.md, T-0011B dispatch): 5 cases, not the spec's 8 -- the two
// load-bearing cases the ticket names explicitly (WorkersTornDownAfterIdleTimeout,
// WorkersRecreatedOnDemand, both driven by an application cg::core::Notifier ticked by hand plus an injected
// clock, no real 60 s wait) plus three covering queue capacity 256, FIFO ordering, and shutdown with pending
// items, per the ticket's "at most ~4 more" allowance (one of the four went to
// retry_downloader_test.cpp instead).
//
// None of these tests exercise the wrapped IDownloader chain (RetryDownloader/TtlDownloader/the
// factory) at all -- the job closures never call RetryDownloader::run(), so NullFactory's
// output is never invoked. They only observe DownloadQueue's own queueing/threading behavior.
#include "cg/core/garbage_collector_thread.h"
#include "cg/core/notifier.h"
#include "cg/net/download_queue.h"
#include "cg/net/download_result_factory.h"
#include "cg/net/inet_config.h"
#include "cg/net/tile_downloader_config.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

using cg::net::DownloaderOptions;
using cg::net::DownloadQueue;
using cg::net::DownloadRequest;
using cg::net::DownloadResult;
using cg::net::IDownloader;
using cg::net::IDownloaderFactory;
using cg::net::IDownloaderPtr;
using cg::net::InetConfig;
using cg::net::TileDownloaderConfig;
using cg::net::TileDownloaderSettings;

namespace {

// Never actually built by these tests (see file comment), but a real, usable IDownloader in case
// that ever changes.
class NullDownloader final : public IDownloader {
public:
    DownloadResult doRequest(const DownloadRequest& /*request*/, std::stop_token /*stop*/) override {
        return {};
    }
};

// Always answers 200 with the head "HEAD" -- lets a job drive RetryDownloader::run() end to end.
class OkDownloader final : public IDownloader {
public:
    DownloadResult doRequest(const DownloadRequest& /*request*/, std::stop_token /*stop*/) override {
        return cg::net::result::ok(200, "HEAD", {}, {});
    }
};

class OkFactory final : public IDownloaderFactory {
public:
    IDownloaderPtr build(const DownloaderOptions& /*options*/) override {
        return std::make_shared<OkDownloader>();
    }
};

class NullFactory final : public IDownloaderFactory {
public:
    IDownloaderPtr build(const DownloaderOptions& /*options*/) override {
        return std::make_shared<NullDownloader>();
    }
};

std::unique_ptr<TileDownloaderConfig> makeConfig(unsigned maxConnect, bool enabled = true) {
    TileDownloaderSettings zmpDefaults;
    zmpDefaults.enabled = enabled;
    zmpDefaults.maxConnectToServerCount = maxConnect;
    return std::make_unique<TileDownloaderConfig>(std::make_shared<InetConfig>(), zmpDefaults);
}

// Injected time source: the tick handlers compare against it, never against the wall clock.
struct FakeClock {
    std::atomic<std::int64_t> ms{0};
    void advance(std::chrono::milliseconds d) { ms += d.count(); }
    [[nodiscard]] cg::net::SteadyClockFn fn() {
        return [this] { return std::chrono::steady_clock::time_point(std::chrono::milliseconds(ms.load())); };
    }
};

std::shared_ptr<DownloadQueue> makeQueue(const std::shared_ptr<IDownloaderFactory>& factory,
                                         cg::core::Notifier& gc, TileDownloaderConfig& config, int capacity,
                                         FakeClock& clock) {
    auto queue = std::make_shared<DownloadQueue>(factory, gc, config, capacity, clock.fn());
    queue->subscribe();
    return queue;
}

// Counts completed ticks (subscribe AFTER the object under test so that, within one notify(), the
// object's slot has finished before the counter moves). Waits are bounded and condition-based.
class TickCounter {
public:
    void attach(cg::core::Notifier& gc) {
        sub_ = gc.subscribe([this] {
            {
                const std::lock_guard lock(mutex_);
                ++count_;
            }
            cv_.notify_all();
        });
    }
    [[nodiscard]] int count() {
        const std::lock_guard lock(mutex_);
        return count_;
    }
    [[nodiscard]] bool waitFor(int target) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, std::chrono::seconds(30), [&] { return count_ >= target; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int count_ = 0;
    cg::core::Subscription sub_;
};

} // namespace

// u_TileRequestProcessorPool.pas:106,185-192 + u_ListenerTime.pas:90-108 (TListenerTTLCheck, TTL
// 60 s). A tick after a push only consumes the "used" flag and stamps the idle clock (postponing
// teardown, InitThreadsIfNeed's UpdateUseTime); a later tick, once 60 s have passed with no
// intervening push, is what actually tears the worker set down. Proven here by
// watching the worker generation's own stop_token actually fire -- not by comparing the identity
// (native thread id, or the id of the wrapper object standing in for it) of the thread that ran
// job A against the one that runs job B: both the OS (the native tid) and, when the worker was a
// QThread, the allocator (QThread's own heap-allocated storage) are free to, and were observed to
// reliably, hand the very next worker the exact same identity once the old one is freed and
// joined, which made an identity-comparison assertion here fail deterministically despite
// teardown+recreate actually happening (confirmed by direct instrumentation while diagnosing this
// test -- see download_queue.h's file comment on why the worker thread type changed as a result).
// request_stop() on std::stop_source invokes every std::stop_callback registered against one of
// its tokens synchronously, on the calling thread, before returning -- so by the time the second
// notify() (which runs terminateWorkers() synchronously) returns, this callback having fired is a
// deterministic proof that this generation's stop was actually requested, not an inference from
// incidental identity reuse.
TEST(DownloadQueueTest, WorkersTornDownAfterIdleTimeout) {
    cg::core::Notifier gc;
    FakeClock clock;
    const auto config = makeConfig(/*maxConnect=*/1);
    const auto factory = std::make_shared<NullFactory>();
    const auto queuePtr = makeQueue(factory, gc, *config, cg::net::kQueueCapacity, clock);
    DownloadQueue& queue = *queuePtr;

    std::promise<std::stop_token> firstToken;
    queue.push([&](cg::net::RetryDownloader&, void*, std::stop_token stop) { firstToken.set_value(stop); });
    const std::stop_token tokenA = firstToken.get_future().get();

    std::promise<void> generationStopped;
    std::future<void> generationStoppedFuture = generationStopped.get_future();
    const std::stop_callback watchTeardown(tokenA, [&] { generationStopped.set_value(); });

    gc.notify(); // consumes the flag stamped by the push above: idle clock starts, workers stay
    clock.advance(cg::net::kWorkerIdleTimeout - std::chrono::milliseconds(1));
    gc.notify(); // 59.999 s idle: not yet
    EXPECT_EQ(generationStoppedFuture.wait_for(std::chrono::seconds(0)), std::future_status::timeout);

    clock.advance(std::chrono::milliseconds(1));
    gc.notify(); // 60 s idle, no push since: workers actually torn down
    EXPECT_EQ(generationStoppedFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    std::promise<void> secondDone;
    queue.push([&](cg::net::RetryDownloader&, void*, std::stop_token) { secondDone.set_value(); });
    EXPECT_EQ(secondDone.get_future().wait_for(std::chrono::seconds(5)), std::future_status::ready);

    queue.shutdown();
}

// Same idle-timeout sequence, but proving the complementary half of the contract: a push after
// teardown is not left stuck with zero workers -- a fresh set is created on demand and the job
// actually completes.
TEST(DownloadQueueTest, WorkersRecreatedOnDemand) {
    cg::core::Notifier gc;
    FakeClock clock;
    const auto config = makeConfig(/*maxConnect=*/1);
    const auto factory = std::make_shared<NullFactory>();
    const auto queuePtr = makeQueue(factory, gc, *config, cg::net::kQueueCapacity, clock);
    DownloadQueue& queue = *queuePtr;

    std::promise<void> firstDone;
    queue.push([&](cg::net::RetryDownloader&, void*, std::stop_token) { firstDone.set_value(); });
    firstDone.get_future().wait();

    gc.notify(); // idle clock starts
    clock.advance(cg::net::kWorkerIdleTimeout);
    gc.notify(); // torn down

    std::promise<void> secondDone;
    queue.push([&](cg::net::RetryDownloader&, void*, std::stop_token) { secondDone.set_value(); });
    const auto status = secondDone.get_future().wait_for(std::chrono::seconds(5));

    EXPECT_EQ(status, std::future_status::ready);

    queue.shutdown();
}

// A single worker makes ordering deterministic: no two jobs ever run concurrently, so the pull
// order must equal the push order (the ring buffer, u_InterfaceQueue.pas:78-185).
TEST(DownloadQueueTest, PreservesPushOrderWithASingleWorker) {
    cg::core::Notifier gc;
    FakeClock clock;
    const auto config = makeConfig(/*maxConnect=*/1);
    const auto factory = std::make_shared<NullFactory>();
    const auto queuePtr = makeQueue(factory, gc, *config, cg::net::kQueueCapacity, clock);
    DownloadQueue& queue = *queuePtr;

    static constexpr int kJobCount = 5;
    std::mutex orderMutex;
    std::vector<int> order;
    std::promise<void> lastDone;

    for (int i = 0; i < kJobCount; ++i) {
        queue.push([&, i](cg::net::RetryDownloader&, void*, std::stop_token) {
            {
                const std::lock_guard lock(orderMutex);
                order.push_back(i);
            }
            if (i == kJobCount - 1) {
                lastDone.set_value();
            }
        });
    }
    lastDone.get_future().wait();

    const std::vector<int> expected{0, 1, 2, 3, 4};
    EXPECT_EQ(order, expected);

    queue.shutdown();
}

// u_TileDownloadSubsystem.pas:381: capacity 256, and push() blocks the caller while it is full.
// Zero workers (downloading disabled) keeps the queue genuinely, deterministically full -- nothing
// ever drains it -- so the 257th push is guaranteed still blocked after a generous headstart, and
// shutdown() is what finally unblocks it (dropping the job, per push()'s documented behavior).
TEST(DownloadQueueTest, PushBlocksWhenFullAtCapacity256) {
    cg::core::Notifier gc;
    FakeClock clock;
    const auto config = makeConfig(/*maxConnect=*/0, /*enabled=*/false);
    const auto factory = std::make_shared<NullFactory>();
    const auto queuePtr = makeQueue(factory, gc, *config, cg::net::kQueueCapacity, clock);
    DownloadQueue& queue = *queuePtr;

    for (int i = 0; i < cg::net::kQueueCapacity; ++i) {
        queue.push([](cg::net::RetryDownloader&, void*, std::stop_token) {
        }); // must not block: still within capacity
    }

    std::atomic<bool> pushed257{false};
    std::thread producer([&] {
        queue.push([](cg::net::RetryDownloader&, void*, std::stop_token) {});
        pushed257 = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // headstart, see curl_downloader_test.cpp
    EXPECT_FALSE(pushed257.load());

    queue.shutdown();
    producer.join();

    EXPECT_TRUE(pushed257.load());
}

// Shutdown with real pending work: an in-flight job blocked on its own stop_token, one job still
// sitting unconsumed in the ring buffer, and a producer blocked on a full (capacity 1) queue.
// shutdown() must unblock the producer and cancel the in-flight job; the still-queued and
// still-blocked jobs must never run (dropped, per pull()'s "termination wins" rule).
TEST(DownloadQueueTest, ShutdownUnblocksPendingPushAndCancelsInFlightJob) {
    cg::core::Notifier gc;
    FakeClock clock;
    const auto config = makeConfig(/*maxConnect=*/1);
    const auto factory = std::make_shared<NullFactory>();
    const auto queuePtr = makeQueue(factory, gc, *config, /*capacity=*/1, clock);
    DownloadQueue& queue = *queuePtr;

    std::promise<void> jobAStarted;
    std::atomic<bool> jobACanceled{false};
    queue.push([&](cg::net::RetryDownloader&, void*, std::stop_token stop) {
        jobAStarted.set_value();
        std::mutex localMutex;
        std::unique_lock localLock(localMutex);
        std::condition_variable localCv;
        const std::stop_callback wake(stop, [&] { localCv.notify_all(); });
        localCv.wait(localLock, [&] { return stop.stop_requested(); });
        jobACanceled = true;
    });
    jobAStarted.get_future().wait(); // job A is now pulled and blocked on its own stop_token

    std::atomic<int> laterJobRuns{0};
    queue.push([&](cg::net::RetryDownloader&, void*, std::stop_token) {
        ++laterJobRuns;
    }); // job B: fills the capacity-1 queue

    std::atomic<bool> pushedC{false};
    std::thread producer([&] {
        queue.push([&](cg::net::RetryDownloader&, void*, std::stop_token) {
            ++laterJobRuns;
        }); // job C: blocks, queue is full
        pushedC = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(pushedC.load());

    queue.shutdown();
    producer.join();

    EXPECT_TRUE(pushedC.load());       // the blocked push unblocked (job C dropped, not enqueued)
    EXPECT_TRUE(jobACanceled.load());  // the in-flight job's stop_token fired
    EXPECT_EQ(laterJobRuns.load(), 0); // B (still queued) and C (never enqueued) never ran
}

// Shared state between the GC-thread tick and the pushing thread (workersMutex_ / queueMutex_,
// u_TileRequestProcessorPool.pas FThreadArrayCS): the real collector thread ticks every millisecond
// while the owner pushes. Each iteration waits for ticks to overlap the use, so a worker generation
// is really torn down between two pushes: the job of iteration i must run on generation i (distinct
// stop states), and every job runs exactly once.
TEST(DownloadQueueTest, TickOnCollectorThreadTearsDownBetweenPushes) {
    cg::core::Notifier gc;
    FakeClock clock;
    const auto config = makeConfig(/*maxConnect=*/2);
    const auto factory = std::make_shared<NullFactory>();
    const auto queue = makeQueue(factory, gc, *config, cg::net::kQueueCapacity, clock);
    TickCounter ticks;
    ticks.attach(gc);
    cg::core::GarbageCollectorThread collector(gc, std::chrono::milliseconds(1));

    static constexpr int kPushes = 15;
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::stop_token> generations;
    std::vector<int> runs(kPushes, 0);
    for (int i = 0; i < kPushes; ++i) {
        queue->push([&, i](cg::net::RetryDownloader&, void*, std::stop_token stop) {
            {
                const std::lock_guard lock(mutex);
                ++runs[static_cast<std::size_t>(i)];
                generations.push_back(stop);
            }
            cv.notify_all();
        });
        {
            std::unique_lock lock(mutex);
            ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(30),
                                    [&] { return runs[static_cast<std::size_t>(i)] > 0; }));
        }
        ASSERT_TRUE(ticks.waitFor(ticks.count() + 2)); // a whole tick after the push: stamps the idle clock
        clock.advance(cg::net::kWorkerIdleTimeout);
        ASSERT_TRUE(ticks.waitFor(ticks.count() + 2)); // a whole tick after the jump: the trim ran
    }
    const std::lock_guard lock(mutex);
    for (const int n : runs) {
        EXPECT_EQ(n, 1);
    }
    for (std::size_t a = 0; a < generations.size(); ++a) {
        EXPECT_TRUE(generations[a].stop_requested()); // every generation was torn down
        for (std::size_t b = a + 1; b < generations.size(); ++b) {
            EXPECT_FALSE(generations[a] == generations[b]); // and each job ran on a fresh one
        }
    }
}

// L-169 / D-72 (T-0048): a push that lands between the idle trim's emptiness check and the hand-over
// of the workers must be served. The only worker is held busy by job A (so it cannot pull the late
// job), the hook runs at exactly that point (the trim holds workersMutex_) and a second thread
// pushes; the hook waits until the late job is in the ring. With the legacy ordering (check, release,
// terminate) the push would find the old worker set still registered and, once the trim stops the
// worker, the job would be stranded; here the push waits for the lock, finds the set gone and builds
// a new one.
TEST(DownloadQueueTest, PushDuringIdleTeardownIsServed) {
    cg::core::Notifier gc;
    FakeClock clock;
    const auto config = makeConfig(/*maxConnect=*/1);
    const auto factory = std::make_shared<NullFactory>();
    const auto queue = makeQueue(factory, gc, *config, cg::net::kQueueCapacity, clock);

    std::promise<void> aStarted;
    std::atomic<bool> aTimedOut{false};
    queue->push([&](cg::net::RetryDownloader&, void*,
                    std::stop_token stop) { // job A: busy until its generation is stopped
        aStarted.set_value();
        std::mutex localMutex;
        std::unique_lock localLock(localMutex);
        std::condition_variable_any localCv;
        // the stop-token wait cannot lose the wake-up; the deadline turns a regression into a failure
        if (!localCv.wait_for(localLock, stop, std::chrono::seconds(30), [&] { return false; }) &&
            !stop.stop_requested()) {
            aTimedOut = true;
        }
    });
    ASSERT_EQ(aStarted.get_future().wait_for(std::chrono::seconds(30)), std::future_status::ready);
    gc.notify(); // flag consumed, idle clock stamped
    clock.advance(cg::net::kWorkerIdleTimeout);

    std::promise<void> lateDone;
    std::future<void> lateFuture = lateDone.get_future();
    std::thread pusher;
    queue->setAfterIdleCheckHookForTest([&] {
        pusher = std::thread([&] {
            queue->push([&](cg::net::RetryDownloader&, void*, std::stop_token) { lateDone.set_value(); });
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (queue->isEmpty() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
    });
    gc.notify(); // the trim: empty at the check, the hook's push arrives before the workers are taken
    EXPECT_EQ(lateFuture.wait_for(std::chrono::seconds(30)), std::future_status::ready);
    if (pusher.joinable()) {
        pusher.join();
    }
    queue->shutdown();
    EXPECT_FALSE(aTimedOut.load());
}

namespace {

struct TestSlotState {
    explicit TestSlotState(std::atomic<int>& destroyedCounter) : destroyed(destroyedCounter) {}
    ~TestSlotState() { ++destroyed; }
    TestSlotState(const TestSlotState&) = delete;
    TestSlotState& operator=(const TestSlotState&) = delete;
    std::atomic<int>& destroyed;
    cg::net::LastResponseInfo info;
    int builds = 0;
};

// One job body: runs the slot's retry loop once (the slot's downloader answers 200 "HEAD").
cg::net::RetryOutcome runOnce(cg::net::RetryDownloader& loop, TestSlotState& state,
                              const std::stop_token& stop) {
    return loop.run(
        [&] {
            ++state.builds;
            return std::optional<DownloadRequest>(DownloadRequest{});
        },
        state.info, [](const DownloadResult&) {}, stop, stop);
}

} // namespace

// u_TileDownloaderList.pas:146-168 -- one state object per slot, handed to every job that slot runs,
// never shared between slots, destroyed with the slot. Two jobs rendezvous so they must be on two
// different workers; each drives the slot's retry loop and its own LastResponseInfo records the head.
TEST(DownloadQueueTest, EachSlotGetsItsOwnStateDestroyedWithTheSlot) {
    std::atomic<int> created{0};
    std::atomic<int> destroyed{0};

    cg::core::Notifier gc;
    FakeClock clock;
    const auto config = makeConfig(/*maxConnect=*/2);
    auto queue = std::make_shared<DownloadQueue>(std::make_shared<OkFactory>(), gc, *config, 8, clock.fn(),
                                                 [&](const IDownloaderPtr&) -> DownloadQueue::SlotState {
                                                     ++created;
                                                     return std::make_shared<TestSlotState>(destroyed);
                                                 });
    queue->subscribe();

    std::atomic<int> arrived{0};
    std::atomic<bool> timedOut{false};
    std::mutex mutex;
    std::vector<TestSlotState*> seen;
    std::vector<int> buildsSeen;
    std::atomic<int> done{0};
    const auto job = [&](cg::net::RetryDownloader& loop, void* raw, const std::stop_token& stop) {
        auto* state = static_cast<TestSlotState*>(raw);
        ++arrived;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (arrived.load() < 2) { // both jobs must be running at once: two distinct slots
            if (std::chrono::steady_clock::now() > deadline) {
                timedOut = true;
                break;
            }
            std::this_thread::yield();
        }
        const cg::net::RetryOutcome outcome = runOnce(loop, *state, stop);
        EXPECT_EQ(outcome.end, cg::net::RetryEnd::Sent);
        EXPECT_EQ(state->info.responseHead(), QByteArray("HEAD"));
        {
            const std::lock_guard lock(mutex);
            seen.push_back(state);
            buildsSeen.push_back(state->builds);
        }
        ++done;
    };
    queue->push(job);
    queue->push(job);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (done.load() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }

    ASSERT_EQ(done.load(), 2);
    EXPECT_FALSE(timedOut.load());
    EXPECT_EQ(created.load(), 2);
    ASSERT_EQ(seen.size(), 2U);
    EXPECT_NE(seen[0], nullptr);
    EXPECT_NE(seen[0], seen[1]);
    EXPECT_EQ(buildsSeen, (std::vector<int>{1, 1})); // each slot built exactly once, in its own state
    EXPECT_EQ(destroyed.load(), 0);                  // alive while the slots are
    queue->shutdown();
    EXPECT_EQ(destroyed.load(), 2); // destroyed with the slots
}

// u_TileRequestProcessorPool.pas:194-217 + :141-150, u_TileDownloaderList.pas:186-225 -- the idle
// trim drops THREADS only; the new threads get the same list items. The slot's state (here: the
// stored response head and the build counter) survives it, and is dropped only by a config change
// (which rebuilds the list) or shutdown.
TEST(DownloadQueueTest, SlotStateSurvivesIdleTrimAndIsRebuiltOnConfigChange) {
    std::atomic<int> created{0};
    std::atomic<int> destroyed{0};

    cg::core::Notifier gc;
    FakeClock clock;
    const auto config = makeConfig(/*maxConnect=*/1);
    auto queue = std::make_shared<DownloadQueue>(std::make_shared<OkFactory>(), gc, *config, 8, clock.fn(),
                                                 [&](const IDownloaderPtr&) -> DownloadQueue::SlotState {
                                                     ++created;
                                                     return std::make_shared<TestSlotState>(destroyed);
                                                 });
    queue->subscribe();

    struct Seen {
        TestSlotState* state = nullptr;
        QByteArray headAtStart;
        int buildsAtStart = -1;
    };
    const auto runJob = [&] {
        std::promise<Seen> promise;
        std::future<Seen> future = promise.get_future();
        queue->push([&](cg::net::RetryDownloader& loop, void* raw, const std::stop_token& stop) {
            auto* state = static_cast<TestSlotState*>(raw);
            Seen seen{state, state->info.responseHead(), state->builds};
            const auto outcome = runOnce(loop, *state, stop);
            EXPECT_EQ(outcome.end, cg::net::RetryEnd::Sent);
            promise.set_value(seen);
        });
        EXPECT_EQ(future.wait_for(std::chrono::seconds(30)), std::future_status::ready);
        return future.get();
    };

    const Seen first = runJob();
    gc.notify(); // idle clock starts
    clock.advance(cg::net::kWorkerIdleTimeout);
    gc.notify(); // the idle trim: threads gone
    const Seen second = runJob();

    EXPECT_EQ(created.load(), 1);
    EXPECT_EQ(destroyed.load(), 0);
    EXPECT_EQ(second.state, first.state); // same slot state after the trim
    EXPECT_EQ(first.headAtStart, QByteArray());
    EXPECT_EQ(second.headAtStart, QByteArray("HEAD")); // the stored head survived
    EXPECT_EQ(second.buildsAtStart, 1);

    config->setWaitIntervalMs(1); // any config change: legacy rebuilds the list
    const Seen third = runJob();
    EXPECT_EQ(created.load(), 2);
    EXPECT_EQ(destroyed.load(), 1); // the old slot went with the config generation
    EXPECT_EQ(third.headAtStart, QByteArray());
    EXPECT_EQ(third.buildsAtStart, 0);

    queue->shutdown();
    EXPECT_EQ(destroyed.load(), 2);
}

namespace {

// A downloader that answers 200 with the head "ID<n>": which inner downloader served a request is visible.
class TaggedDownloader final : public IDownloader {
public:
    explicit TaggedDownloader(int id) : head_("ID" + QByteArray::number(id)) {}
    DownloadResult doRequest(const DownloadRequest& /*request*/, std::stop_token /*stop*/) override {
        return cg::net::result::ok(200, head_, {}, {});
    }

private:
    QByteArray head_;
};

// One TaggedDownloader per build(): the number of builds is the number of inner downloaders made.
class CountingFactory final : public IDownloaderFactory {
public:
    IDownloaderPtr build(const DownloaderOptions& /*options*/) override {
        return std::make_shared<TaggedDownloader>(++builds);
    }
    std::atomic<int> builds{0};
};

// The state a slot gets: the downloader the factory was given, kept the way the maptype builder keeps it.
struct DownloaderSlotState final : TestSlotState {
    DownloaderSlotState(std::atomic<int>& destroyedCounter, IDownloaderPtr given)
        : TestSlotState(destroyedCounter), downloader(std::move(given)) {}
    IDownloaderPtr downloader;
};

// What one job saw on its slot: the downloader the factory gave, the head of a request sent straight through
// it (the script's nested HTTP) and the head the slot's retry loop got for the transfer.
struct DownloaderSeen {
    DownloaderSlotState* state = nullptr;
    IDownloader* given = nullptr;
    QByteArray nestedHead;
    QByteArray transferHead;
};

DownloaderSeen runNestedThenTransfer(cg::net::RetryDownloader& loop, void* raw, const std::stop_token& stop) {
    auto* state = static_cast<DownloaderSlotState*>(raw);
    DownloaderSeen seen;
    seen.state = state;
    seen.given = state->downloader.get();
    seen.nestedHead = state->downloader->doRequest(DownloadRequest{}, stop).rawResponseHeader;
    EXPECT_EQ(runOnce(loop, *state, stop).end, cg::net::RetryEnd::Sent);
    seen.transferHead = state->info.responseHead();
    return seen;
}

} // namespace

// u_TileDownloaderList.pas:143-168 -- ONE downloader per slot, given to the builder (the state) and to the
// transfer (the retry loop). The factory is called once per slot with a non-null downloader; two slots get
// two different ones; the downloader the factory saw is the one the slot's loop sends through: after a job
// that used both, the counting IDownloaderFactory built ONE inner downloader per slot, not two, and both
// requests of a slot were served by the same one (same head).
TEST(DownloadQueueTest, SlotStateFactoryGetsTheDownloaderTheSlotsJobSendsThrough) {
    std::atomic<int> destroyed{0};
    std::vector<IDownloaderPtr> given;
    std::mutex givenMutex;

    cg::core::Notifier gc;
    FakeClock clock;
    const auto config = makeConfig(/*maxConnect=*/2);
    const auto factory = std::make_shared<CountingFactory>();
    auto queue = std::make_shared<DownloadQueue>(
        factory, gc, *config, 8, clock.fn(),
        [&](const IDownloaderPtr& downloader) -> DownloadQueue::SlotState {
            const std::lock_guard lock(givenMutex);
            given.push_back(downloader);
            return std::make_shared<DownloaderSlotState>(destroyed, downloader);
        });
    queue->subscribe();

    std::latch together(2); // both jobs inside at once: two distinct slots
    std::mutex mutex;
    std::vector<DownloaderSeen> seen;
    std::latch done(2);
    const auto job = [&](cg::net::RetryDownloader& loop, void* raw, const std::stop_token& stop) {
        together.arrive_and_wait();
        const DownloaderSeen one = runNestedThenTransfer(loop, raw, stop);
        {
            const std::lock_guard lock(mutex);
            seen.push_back(one);
        }
        done.count_down();
    };
    queue->push(job);
    queue->push(job);
    done.wait();
    queue->shutdown(); // joins the workers; the vectors below are then read race-free
    const std::lock_guard readLock(
        mutex); // the worker wrote under these mutexes; TSan does not see latch ordering
    const std::lock_guard readGiven(givenMutex);

    EXPECT_EQ(factory->builds.load(), 2); // one per slot after jobs that used both: not 4
    ASSERT_EQ(given.size(), 2U);
    EXPECT_NE(given[0], nullptr);
    EXPECT_NE(given[1], nullptr);
    EXPECT_NE(given[0], given[1]);
    ASSERT_EQ(seen.size(), 2U);
    EXPECT_NE(seen[0].given, seen[1].given);
    EXPECT_NE(seen[0].nestedHead, seen[1].nestedHead); // two slots, two inner downloaders
    for (const DownloaderSeen& one : seen) {
        EXPECT_TRUE(one.given == given[0].get() || one.given == given[1].get());
        EXPECT_EQ(one.transferHead, one.nestedHead); // the transfer went through the builder's downloader
    }
    EXPECT_EQ((std::set<QByteArray>{seen[0].nestedHead, seen[1].nestedHead}),
              (std::set<QByteArray>{"ID1", "ID2"}));
}

// The slot keeps its state AND its downloader across the idle trim (threads only are dropped,
// u_TileRequestProcessorPool.pas:194-217): after the trim the next job sees the same downloader object and
// the same inner downloader (no new build). A config change rebuilds both, and the new state is made from the
// NEW downloader (not the old one) -- a new inner downloader answers.
TEST(DownloadQueueTest, SlotDownloaderSurvivesIdleTrimAndIsRebuiltWithTheStateOnConfigChange) {
    std::atomic<int> destroyed{0};
    std::vector<IDownloaderPtr> given;

    cg::core::Notifier gc;
    FakeClock clock;
    const auto config = makeConfig(/*maxConnect=*/1);
    const auto factory = std::make_shared<CountingFactory>();
    auto queue = std::make_shared<DownloadQueue>(
        factory, gc, *config, 8, clock.fn(),
        [&](const IDownloaderPtr& downloader) -> DownloadQueue::SlotState {
            given.push_back(downloader); // under workersMutex_, on the pushing thread (this test's)
            return std::make_shared<DownloaderSlotState>(destroyed, downloader);
        });
    queue->subscribe();

    const auto runJob = [&] {
        std::promise<DownloaderSeen> promise;
        std::future<DownloaderSeen> future = promise.get_future();
        queue->push([&](cg::net::RetryDownloader& loop, void* raw, const std::stop_token& stop) {
            promise.set_value(runNestedThenTransfer(loop, raw, stop));
        });
        EXPECT_EQ(future.wait_for(std::chrono::seconds(30)), std::future_status::ready);
        return future.get();
    };

    const DownloaderSeen first = runJob();
    gc.notify(); // idle clock starts
    clock.advance(cg::net::kWorkerIdleTimeout);
    gc.notify(); // the idle trim: threads gone
    const DownloaderSeen second = runJob();

    ASSERT_EQ(given.size(), 1U);
    EXPECT_EQ(second.state, first.state);
    EXPECT_EQ(second.given, first.given);
    EXPECT_EQ(second.given, given[0].get());
    EXPECT_EQ(first.nestedHead, QByteArray("ID1"));
    EXPECT_EQ(second.nestedHead, QByteArray("ID1")); // the same inner downloader: the trim did not rebuild it
    EXPECT_EQ(second.transferHead, QByteArray("ID1"));
    EXPECT_EQ(factory->builds.load(), 1);

    config->setWaitIntervalMs(1); // any config change: legacy rebuilds the list
    const DownloaderSeen third = runJob();
    ASSERT_EQ(given.size(), 2U);
    EXPECT_NE(given[1], given[0]);
    EXPECT_EQ(third.given, given[1].get()); // the new state was made from the NEW downloader
    EXPECT_NE(third.given, first.given);
    EXPECT_EQ(third.nestedHead, QByteArray("ID2"));
    EXPECT_EQ(third.transferHead, QByteArray("ID2"));
    EXPECT_EQ(factory->builds.load(), 2);
    queue->shutdown();
}

// Not a FEAT-bearing legacy port: hardening, docs/specs/net.md §2.15 (Tech Lead ruling 2026-10-13, D-87).
// Three jobs in a row throw FROM INSIDE RetryDownloader::run (the `build` callable), then a normal job
// runs the slot's retry loop on a one-slot queue: the process is alive (without the catch in
// workerLoop the exception leaves the std::thread function and std::terminate kills the test binary;
// checked by running the first test once with the try/catch removed), and the normal run returns Sent,
// which also proves the slot's run lock was released by unwinding after every throw (a leaked lock
// would block the normal run until the 30 s wait fails).
namespace {

void throwingJobsThenNormalRun(const std::function<void()>& thrower) {
    cg::core::Notifier gc;
    FakeClock clock;
    const auto config = makeConfig(/*maxConnect=*/1);
    const auto factory = std::make_shared<OkFactory>();
    const auto queue =
        std::make_shared<DownloadQueue>(factory, gc, *config, cg::net::kQueueCapacity, clock.fn());
    queue->subscribe();

    for (int i = 0; i < 3; ++i) {
        queue->push([&thrower](cg::net::RetryDownloader& loop, void*, const std::stop_token& stop) {
            cg::net::LastResponseInfo info;
            (void)loop.run(
                [&thrower]() -> std::optional<DownloadRequest> {
                    thrower();
                    return std::nullopt;
                },
                info, [](const DownloadResult&) {}, stop, stop);
        });
    }
    std::promise<cg::net::RetryEnd> secondDone;
    queue->push([&secondDone](cg::net::RetryDownloader& loop, void*, const std::stop_token& stop) {
        cg::net::LastResponseInfo info;
        secondDone.set_value(loop.run([] { return std::optional<DownloadRequest>(DownloadRequest{}); }, info,
                                      [](const DownloadResult&) {}, stop, stop)
                                 .end);
    });
    auto future = secondDone.get_future();
    ASSERT_EQ(future.wait_for(std::chrono::seconds(30)), std::future_status::ready);
    EXPECT_EQ(future.get(), cg::net::RetryEnd::Sent);
    queue->shutdown();
}

} // namespace

TEST(DownloadQueueTest, ThrowingJobDoesNotEndTheProcessAndTheWorkerServesTheNextJob) {
    throwingJobsThenNormalRun([] { throw std::runtime_error("boom"); });
}

TEST(DownloadQueueTest, JobThrowingANonStdExceptionDoesNotEndTheProcessEither) {
    throwingJobsThenNormalRun([] { throw 42; });
}

TEST(DownloadQueueTest, ThrowingJobRepeated1000TimesAlternatingBothKinds) {
    for (int i = 0; i < 1000; ++i) {
        throwingJobsThenNormalRun([i] {
            if (i % 2 == 0) {
                throw std::logic_error("boom");
            }
            throw i;
        });
        if (::testing::Test::HasFatalFailure()) {
            return;
        }
    }
}
