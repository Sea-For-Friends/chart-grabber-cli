// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-009 -- docs/specs/net.md §2.10 (u_DownloaderHttpWithTTL.pas). The idle trim is
// driven by ticking an application cg::core::Notifier by hand with an injected clock; no sleeps.
#include "cg/core/garbage_collector_thread.h"
#include "cg/core/notifier.h"
#include "cg/net/downloader_ttl.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

using cg::net::DownloaderOptions;
using cg::net::DownloadRequest;
using cg::net::DownloadResult;
using cg::net::IDownloader;
using cg::net::IDownloaderFactory;
using cg::net::IDownloaderPtr;
using cg::net::TtlDownloader;

namespace {

class OkDownloader final : public IDownloader {
public:
    DownloadResult doRequest(const DownloadRequest& /*request*/, std::stop_token /*stop*/) override {
        return {};
    }
};

class CountingFactory final : public IDownloaderFactory {
public:
    IDownloaderPtr build(const DownloaderOptions& /*options*/) override {
        ++builds;
        return std::make_shared<OkDownloader>();
    }
    std::atomic<int> builds{0};
};

struct FakeClock {
    std::atomic<std::int64_t> ms{0};
    [[nodiscard]] cg::net::SteadyClockFn fn() {
        return [this] { return std::chrono::steady_clock::time_point(std::chrono::milliseconds(ms.load())); };
    }
};

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

// Inner downloader whose request can be held open until released; otherwise it works for a moment.
class GateState {
public:
    std::mutex mutex;
    std::condition_variable cv;
    bool blockNext = false;
    bool entered = false;
    bool released = false;
};

class GateDownloader final : public IDownloader {
public:
    explicit GateDownloader(GateState& gate) : gate_(gate) {}
    DownloadResult doRequest(const DownloadRequest& /*request*/, std::stop_token /*stop*/) override {
        std::unique_lock lock(gate_.mutex);
        if (gate_.blockNext) {
            gate_.blockNext = false;
            gate_.entered = true;
            gate_.cv.notify_all();
            gate_.cv.wait_for(lock, std::chrono::seconds(30), [&] { return gate_.released; });
        } else {
            lock.unlock();
            const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(200);
            while (std::chrono::steady_clock::now() < until) { // a request that overlaps ticks
            }
        }
        return {};
    }

private:
    GateState& gate_;
};

class GateFactory final : public IDownloaderFactory {
public:
    IDownloaderPtr build(const DownloaderOptions& /*options*/) override {
        ++builds;
        return std::make_shared<GateDownloader>(gate);
    }
    std::atomic<int> builds{0};
    GateState gate;
};

} // namespace

// u_DownloaderHttpWithTTL.pas:79,126-134: the inner downloader is dropped after kHttpClientTtl of
// idleness and rebuilt on demand.
TEST(TtlDownloaderTest, TrimsAfterTtlAndRebuildsOnDemand) {
    cg::core::Notifier gc;
    FakeClock clock;
    const auto factory = std::make_shared<CountingFactory>();
    const auto ttl = std::make_shared<TtlDownloader>(factory, DownloaderOptions{}, clock.fn());
    ttl->subscribeGcTick(gc);

    (void)ttl->doRequest({}, {});
    EXPECT_EQ(factory->builds.load(), 1);

    clock.ms += cg::net::kHttpClientTtl.count() - 1;
    gc.notify();
    (void)ttl->doRequest({}, {});
    EXPECT_EQ(factory->builds.load(), 1); // 4:59.999 idle: kept

    clock.ms += cg::net::kHttpClientTtl.count();
    gc.notify();
    (void)ttl->doRequest({}, {});
    EXPECT_EQ(factory->builds.load(), 2); // trimmed, rebuilt
}

// The tick slot holds only a weak reference: a tick after the last owner is gone is a no-op, and the
// subscription is gone with the object.
TEST(TtlDownloaderTest, TickAfterDestructionIsHarmless) {
    cg::core::Notifier gc;
    FakeClock clock;
    {
        const auto ttl = std::make_shared<TtlDownloader>(std::make_shared<CountingFactory>(),
                                                         DownloaderOptions{}, clock.fn());
        ttl->subscribeGcTick(gc);
        EXPECT_EQ(gc.subscriberCount(), 1U);
    }
    EXPECT_EQ(gc.subscriberCount(), 0U);
    gc.notify();
}

// State shared between the owner's doRequest() and the collector thread's tick (mutex_): a real
// GarbageCollectorThread ticks every millisecond while requests run (each takes a moment, so ticks
// land inside requests). After every request the clock jumps past the TTL and the owner waits for
// whole ticks after the jump, so each request meets a trimmed client: builds == requests.
TEST(TtlDownloaderTest, TickOnCollectorThreadTrimsBetweenRequests) {
    cg::core::Notifier gc;
    FakeClock clock;
    const auto factory = std::make_shared<GateFactory>();
    const auto ttl = std::make_shared<TtlDownloader>(factory, DownloaderOptions{}, clock.fn());
    ttl->subscribeGcTick(gc);
    TickCounter ticks;
    ticks.attach(gc);
    cg::core::GarbageCollectorThread collector(gc, std::chrono::milliseconds(1));

    static constexpr int kRequests = 20;
    for (int i = 0; i < kRequests; ++i) {
        (void)ttl->doRequest({}, {});
        clock.ms += cg::net::kHttpClientTtl.count();
        ASSERT_TRUE(ticks.waitFor(ticks.count() + 2));
    }
    EXPECT_EQ(factory->builds.load(), kRequests);
}

// The tryLock skip (D-70, T-0048): a request in flight while the clock passes the TTL. The tick must
// not block (the collector keeps ticking), the client survives that request, and the trim happens
// on a later tick once the request has ended and the idle time is measured from its end.
TEST(TtlDownloaderTest, TickSkipsWhileARequestIsInFlight) {
    cg::core::Notifier gc;
    FakeClock clock;
    const auto factory = std::make_shared<GateFactory>();
    const auto ttl = std::make_shared<TtlDownloader>(factory, DownloaderOptions{}, clock.fn());
    ttl->subscribeGcTick(gc);
    TickCounter ticks;
    ticks.attach(gc);
    cg::core::GarbageCollectorThread collector(gc, std::chrono::milliseconds(1));

    (void)ttl->doRequest({}, {}); // builds the client
    ASSERT_EQ(factory->builds.load(), 1);
    {
        const std::lock_guard lock(factory->gate.mutex);
        factory->gate.blockNext = true;
    }
    std::thread requester([&] { (void)ttl->doRequest({}, {}); });
    {
        std::unique_lock lock(factory->gate.mutex);
        ASSERT_TRUE(
            factory->gate.cv.wait_for(lock, std::chrono::seconds(30), [&] { return factory->gate.entered; }));
    }
    clock.ms += 2 * cg::net::kHttpClientTtl.count();
    EXPECT_TRUE(ticks.waitFor(ticks.count() + 3)); // ticks keep coming while the request is blocked
    {
        const std::lock_guard lock(factory->gate.mutex);
        factory->gate.released = true;
    }
    factory->gate.cv.notify_all();
    requester.join();

    ASSERT_TRUE(ticks.waitFor(ticks.count() + 2)); // idle time restarts at the end of the request
    (void)ttl->doRequest({}, {});
    EXPECT_EQ(factory->builds.load(), 1); // survived: not dropped right after the long request
    clock.ms += cg::net::kHttpClientTtl.count();
    ASSERT_TRUE(ticks.waitFor(ticks.count() + 2));
    (void)ttl->doRequest({}, {});
    EXPECT_EQ(factory->builds.load(), 2); // trimmed on a later tick
}
