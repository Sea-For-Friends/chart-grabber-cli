// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-032. Ports i_TileDownloadSubsystem.pas, u_TileDownloadSubsystem.pas and the maptype side
// of u_TileDownloaderSimple.pas:152-274 (docs/specs/maptype.md 2.13.6, 3.1 R1-R4; the R-10 proof lists). Two
// rigs. JobRig drives detail::runTileDownloadJob through a real net::DownloadQueue with scripted builder,
// saver and downloader: one row per arm of the job's switch and of the save callable. SubsystemRig builds the
// whole TileDownloadSubsystem from a ZMP and talks to the local HTTP fixture server of the net tests.
// Expected values are literals from the Pascal (texts u_TileDownloaderSimple.pas:195, :205, :251;
// u_TileRequestResult.pas:258; u_TileDownloadSubsystemState.pas:88, :92;
// u_TileDownloadResultSaverStuped.pas:134, :283).
#include "cg/core/content_type.h"
#include "cg/core/notifier.h"
#include "cg/maptype/tile_download_subsystem.h"
#include "cg/maptype/tile_request.h"
#include "cg/net/curl_downloader.h"
#include "cg/net/download_checker.h"
#include "cg/net/download_queue.h"
#include "cg/net/download_result_factory.h"
#include "cg/net/global_download_config.h"
#include "cg/net/inet_config.h"
#include "cg/script/compiled_script.h"
#include "cg/storage/codecs/gm1_tile_name_codec.h"
#include "cg/storage/file_system_tile_storage.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <latch>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

#include "../net/http_test_server.h"
#include "maptype_test_util.h"
#include "request_builder_test_util.h"

using namespace Qt::StringLiterals;
using namespace cg;
using namespace cg::maptype;
using cg::net::ResultKind;
using cg::net::test::HttpTestServer;

namespace {

// docs/specs/net.md 7.0: no test may reach the real network (read once, on the first CurlDownloader call).
struct ForceNoExternalNetwork {
    ForceNoExternalNetwork() { qputenv("CG_NET_NO_EXTERNAL_NETWORK", "1"); }
};
const ForceNoExternalNetwork kForceNoExternalNetwork;

constexpr auto kWait = std::chrono::seconds(10);

// --- collecting finish notifications
// -----------------------------------------------------------------------------

struct Finish {
    std::mutex mutex;
    std::condition_variable cv;
    int count = 0;
    std::vector<ITileRequestResultPtr> results;
    std::vector<std::thread::id> threads;
    std::function<void()> inside; // runs inside the callback (e.g. throws), under no lock

    // True when `n` notifications have arrived.
    bool waitFor(int n) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, kWait, [&] { return count >= n; });
    }
    [[nodiscard]] int total() {
        const std::lock_guard lock(mutex);
        return count;
    }
    [[nodiscard]] ITileRequestResultPtr result(int index = 0) {
        const std::lock_guard lock(mutex);
        return results.at(static_cast<std::size_t>(index));
    }
    [[nodiscard]] ITileRequestTaskFinishNotifierPtr notifier() {
        return std::make_shared<TileRequestTaskFinishNotifier>(
            [this](const ITileRequestTask&, const ITileRequestResultPtr& r) {
                std::function<void()> hook;
                {
                    const std::lock_guard lock(mutex);
                    ++count;
                    results.push_back(r);
                    threads.push_back(std::this_thread::get_id());
                    hook = inside;
                    // Under the lock: a waiter that returns and destroys this Finish cannot overtake the
                    // broadcast (pthread_cond_destroy raced with pthread_cond_broadcast, T-0350).
                    cv.notify_all();
                }
                if (hook) {
                    hook();
                }
            });
    }
};

// What a result is, by its concrete class (the non-final ones by exact type).
std::string kindOf(const ITileRequestResultPtr& r) {
    if (!r) {
        return "null";
    }
    const std::type_info& t = typeid(*r);
    if (t == typeid(TileRequestResultOk)) {
        return "Ok";
    }
    if (t == typeid(TileRequestResultCanceledAfterDownloadRequest)) {
        return "CanceledAfterDownload";
    }
    if (t == typeid(TileRequestResultCanceledAfterBuildDownloadRequest)) {
        return "CanceledAfterBuild";
    }
    if (t == typeid(TileRequestResultCanceledBeforBuildDownloadRequest)) {
        return "CanceledBeforeBuild";
    }
    if (t == typeid(TileRequestResultDownloadError)) {
        return "DownloadError";
    }
    if (t == typeid(TileRequestResultErrorAfterDownloadRequest)) {
        return "ErrorAfterDownload";
    }
    if (t == typeid(TileRequestResultErrorBeforBuildDownloadRequest)) {
        return "ErrorBeforeBuild";
    }
    return "other";
}

QString textOf(const ITileRequestResultPtr& r) {
    const auto* error = dynamic_cast<const ITileRequestResultError*>(r.get());
    return error != nullptr ? error->errorText() : QString();
}

net::ResultKind downloadKindOf(const ITileRequestResultPtr& r) {
    const auto* with = dynamic_cast<const ITileRequestResultWithDownloadResult*>(r.get());
    return with != nullptr ? with->downloadResult().kind : ResultKind::UnknownError;
}

ITileRequestPtr tileRequest() {
    return std::make_shared<const TileRequest>(geo::PointI{3, 7}, quint8{5}, nullptr);
}

QPair<QByteArray, QByteArray> header(QByteArray name, QByteArray value) {
    return {std::move(name), std::move(value)};
}

// Waits for a stop request on `stop` for at most kWait. True when it came.
bool waitForStop(const std::stop_token& stop) {
    std::mutex m;
    std::condition_variable cv;
    bool fired = false;
    const std::stop_callback callback(stop, [&] {
        {
            const std::lock_guard lock(m);
            fired = true;
        }
        cv.notify_all();
    });
    std::unique_lock lock(m);
    return cv.wait_for(lock, kWait, [&] { return fired; });
}

// =================================================================================================================
// JobRig
// =================================================================================================================

class FnBuilder final : public ITileDownloadRequestBuilder {
public:
    using Fn = std::function<std::optional<net::DownloadRequest>(
        const ITileRequestPtr&, const std::shared_ptr<net::LastResponseInfo>&, const std::stop_token&)>;
    std::optional<net::DownloadRequest> buildRequest(const ITileRequestPtr& source,
                                                     const std::shared_ptr<net::LastResponseInfo>& last,
                                                     const std::stop_token& cancel) override {
        return fn(source, last, cancel);
    }
    Fn fn;
};

class FnSaver final : public ITileDownloadResultSaver {
public:
    using Fn = std::function<ITileRequestResultPtr(const net::DownloadRequest&, const net::DownloadResult&)>;
    std::shared_ptr<ITileDownloaderStateChangeable> state() const override { return nullptr; }
    ITileRequestResultPtr saveDownloadResult(const net::DownloadRequest& request,
                                             const net::DownloadResult& result) override {
        return fn(request, result);
    }
    Fn fn;
};

class FnDownloader final : public net::IDownloader {
public:
    net::DownloadResult doRequest(const net::DownloadRequest& request, std::stop_token stop) override {
        return fn(request, std::move(stop));
    }
    std::function<net::DownloadResult(const net::DownloadRequest&, std::stop_token)> fn;
};

// Hands out one fake transport for every build() and records the allowCookies of each one.
class FnDownloaderFactory final : public net::IDownloaderFactory {
public:
    explicit FnDownloaderFactory(std::shared_ptr<net::IDownloader> d) : downloader_(std::move(d)) {}
    net::IDownloaderPtr build(const net::DownloaderOptions& options) override {
        const std::lock_guard lock(mutex_);
        cookies_.push_back(options.allowCookies);
        return downloader_;
    }
    [[nodiscard]] std::vector<bool> cookies() const {
        const std::lock_guard lock(mutex_);
        return cookies_;
    }

private:
    std::shared_ptr<net::IDownloader> downloader_;
    mutable std::mutex mutex_;
    std::vector<bool> cookies_;
};

// Two jobs in flight at once: each call to hold() counts the phase's `arrived` latch down and waits for
// the phase's `release`. A phase is two consecutive calls; the test thread waits for `arrived` (both jobs are
// inside the transport at the same time, so they run on two slots) and then releases. If the two never
// overlap the test hangs (ctest timeout) instead of passing.
class TwoAtATime {
public:
    struct Phase {
        std::latch arrived{2};
        std::latch release{1};
    };
    // Returns the 1-based number of this call.
    int hold() {
        const int n = calls_++;
        Phase& phase = phases_.at(static_cast<std::size_t>(n / 2));
        phase.arrived.count_down();
        phase.release.wait();
        return n + 1;
    }
    void passPhase(std::size_t index) {
        phases_.at(index).arrived.wait();
        phases_.at(index).release.count_down();
    }

private:
    std::atomic<int> calls_{0};
    std::array<Phase, 2> phases_;
};

net::DownloadRequest requestWithSource() {
    net::DownloadRequest r;
    r.url = "http://h/t";
    r.inet = std::make_shared<const net::InetSettings>();
    r.source = tileRequest();
    return r;
}

const QByteArray kBody = "tile-bytes";

class JobRig {
public:
    std::atomic<int> builds{0};
    std::atomic<int> sends{0};
    std::atomic<int> saves{0};
    std::shared_ptr<FnBuilder> builder = std::make_shared<FnBuilder>();
    std::shared_ptr<FnSaver> saver = std::make_shared<FnSaver>();
    std::atomic<int> slotStates{0}; // calls of the slot-state factory
    std::shared_ptr<FnDownloader> downloader = std::make_shared<FnDownloader>();
    std::shared_ptr<net::DownloadQueue> queue;

    explicit JobRig(int slotCount = 1) {
        builder->fn = [this](const ITileRequestPtr&, const auto&, const std::stop_token&) {
            ++builds;
            return std::optional<net::DownloadRequest>(requestWithSource());
        };
        saver->fn = [this](const net::DownloadRequest& request, const net::DownloadResult& result) {
            ++saves;
            return ITileRequestResultPtr(std::make_shared<const TileRequestResultOk>(request, result));
        };
        downloader->fn = [this](const net::DownloadRequest&, std::stop_token) {
            ++sends;
            return net::result::ok(200, "HTTP/1.1 200 OK\r\n", "image/png", kBody);
        };
        auto inet = std::make_shared<net::InetConfig>();
        inet->setSleepOnResetConnectionMs(0); // a connect error is retried without the 30 s pause
        net::TileDownloaderSettings settings;
        settings.maxConnectToServerCount =
            slotCount; // default one slot: the second task runs on the same slot
        config_ = std::make_shared<net::TileDownloaderConfig>(inet, settings);
        queue = std::make_shared<net::DownloadQueue>(
            std::make_shared<FnDownloaderFactory>(downloader), gc_, *config_, net::kQueueCapacity,
            net::SteadyClockFn{}, [this](const net::IDownloaderPtr&) -> net::DownloadQueue::SlotState {
                ++slotStates;
                auto slot = std::make_shared<detail::SlotState>();
                slot->builder = builder;
                slot->lastResponse = std::make_shared<net::LastResponseInfo>();
                return slot;
            });
        queue->subscribe();
    }

    std::shared_ptr<ITileRequestTaskInternal> task(Finish& finish, std::stop_token soft = {},
                                                   std::stop_token cancel = {}) {
        return std::make_shared<TileRequestTask>(tileRequest(), std::move(soft), std::move(cancel),
                                                 finish.notifier());
    }
    void push(const std::shared_ptr<ITileRequestTaskInternal>& task) {
        queue->push([task, saver = saver](net::RetryDownloader& loop, void* slot, std::stop_token stop) {
            detail::runTileDownloadJob(task, *saver, loop, slot, stop);
        });
    }

private:
    cg::core::Notifier gc_;
    std::shared_ptr<net::TileDownloaderConfig> config_;
};

} // namespace

// u_TileDownloaderSimple.pas:152-274 and the finishing around it
// (u_TileRequestQueueProcessorThread.pas:104-120): one row per arm of the job's switch and of the save
// callable. Every row also proves the task is finished exactly once (the queue is destroyed, which joins its
// worker, before the count is read).
TEST(TileDownloadJob, EveryArmClassifiesAndFinishesTheTaskOnce) {
    struct Row {
        const char* name;
        std::function<void(JobRig&, std::stop_source& soft, std::stop_source& hard)> arrange;
        // Runs on the test thread once the transfer has started (rows with a transfer in flight only).
        std::function<void(JobRig&, std::stop_source& soft, std::stop_source& hard)> duringTransfer;
        const char* kind;
        const char16_t* text; // null = not checked
        int builds;
        int sends;
        int saves;
    };
    std::shared_ptr<std::latch> started;
    const auto blockInTransfer = [&started](JobRig& rig) {
        rig.downloader->fn = [&rig, &started](const net::DownloadRequest&, std::stop_token stop) {
            ++rig.sends;
            started->count_down();
            return waitForStop(stop) ? net::result::canceled()
                                     : net::result::ok(200, "HTTP/1.1 200 OK\r\n", "image/png", "late");
        };
    };
    const std::vector<Row> rows = {
        {"builder throws: the exception text",
         [](JobRig& r, auto&, auto&) {
             r.builder->fn = [&r](const auto&, const auto&,
                                  const auto&) -> std::optional<net::DownloadRequest> {
                 ++r.builds;
                 throw std::runtime_error("Город");
             };
         },
         nullptr, "ErrorBeforeBuild", u"Город", 1, 0, 0},
        {"builder returns nothing: Tile does not exist",
         [](JobRig& r, auto&, auto&) {
             r.builder->fn = [&r](const auto&, const auto&,
                                  const auto&) -> std::optional<net::DownloadRequest> {
                 ++r.builds;
                 return std::nullopt;
             };
         },
         nullptr, "ErrorBeforeBuild", u"Tile does not exist", 1, 0, 0},
        {"soft cancel before build", [](JobRig&, auto& soft, auto&) { soft.request_stop(); }, nullptr,
         "CanceledBeforeBuild", nullptr, 0, 0, 0},
        {"soft cancel after build",
         [](JobRig& r, auto& soft, auto&) {
             r.builder->fn = [&r, &soft](const auto&, const auto&, const auto&) {
                 ++r.builds;
                 soft.request_stop();
                 return std::optional<net::DownloadRequest>(requestWithSource());
             };
         },
         nullptr, "CanceledAfterBuild", nullptr, 1, 0, 0},
        {"hard cancel of the task during the transfer", [&](JobRig& r, auto&, auto&) { blockInTransfer(r); },
         [&](JobRig&, auto&, auto& hard) {
             started->wait();
             hard.request_stop();
         },
         "CanceledAfterDownload", nullptr, 1, 1, 0},
        {"saved OK", [](JobRig&, auto&, auto&) {}, nullptr, "Ok", nullptr, 1, 1, 1},
        {"saver throws: the exception text",
         [](JobRig& r, auto&, auto&) {
             r.saver->fn = [&r](const auto&, const auto&) -> ITileRequestResultPtr {
                 ++r.saves;
                 throw std::runtime_error("диск");
             };
         },
         nullptr, "ErrorAfterDownload", u"диск", 1, 1, 1},
        {"saver returns null for an error kind: Download error",
         [](JobRig& r, auto&, auto&) {
             r.downloader->fn = [&r](const auto&, std::stop_token) {
                 ++r.sends;
                 net::DownloadResult e;
                 e.kind = ResultKind::LoadError;
                 e.text = u"Город"_s;
                 return e;
             };
             r.saver->fn = [&r](const auto&, const auto&) {
                 ++r.saves;
                 return ITileRequestResultPtr();
             };
         },
         nullptr, "DownloadError", u"Download error: Город", 1, 1, 1},
        {"saver returns null for a non-error kind: Unknown error",
         [](JobRig& r, auto&, auto&) {
             r.downloader->fn = [&r](const auto&, std::stop_token) {
                 ++r.sends;
                 net::DownloadResult n;
                 n.kind = ResultKind::NotNecessary;
                 return n;
             };
             r.saver->fn = [&r](const auto&, const auto&) {
                 ++r.saves;
                 return ITileRequestResultPtr();
             };
         },
         nullptr, "ErrorAfterDownload", u"Unknown error", 1, 1, 1},
        {"connect error with a saver that throws: still retried (build, send, save twice)",
         [](JobRig& r, auto&, auto&) {
             r.downloader->fn = [&r](const auto&, std::stop_token) {
                 ++r.sends;
                 return net::result::noConnectToServer(7);
             };
             r.saver->fn = [&r](const auto&, const auto&) -> ITileRequestResultPtr {
                 ++r.saves;
                 throw std::runtime_error("диск");
             };
         },
         nullptr, "ErrorAfterDownload", u"диск", 2, 2, 2},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        started = std::make_shared<std::latch>(1);
        JobRig rig;
        std::stop_source soft;
        std::stop_source hard;
        row.arrange(rig, soft, hard);
        Finish finish;
        rig.push(rig.task(finish, soft.get_token(), hard.get_token()));
        if (row.duringTransfer) {
            row.duringTransfer(rig, soft, hard);
        }
        ASSERT_TRUE(finish.waitFor(1));
        rig.queue.reset(); // joins the worker: a second notification would have happened by now
        EXPECT_EQ(finish.total(), 1);
        EXPECT_EQ(kindOf(finish.result()), row.kind);
        if (row.text != nullptr) {
            EXPECT_EQ(textOf(finish.result()), QString::fromUtf16(row.text));
        }
        EXPECT_EQ(rig.builds.load(), row.builds);
        EXPECT_EQ(rig.sends.load(), row.sends);
        EXPECT_EQ(rig.saves.load(), row.saves);
    }
}

// The queue's stop is the other half of the hard token: destroying the queue while a transfer is in flight
// aborts it, and the job still finishes its task (R1: the job holds neither the queue nor anything that owns
// it).
TEST(TileDownloadJob, DestroyingTheQueueDuringATransferAbortsItAndFinishesTheTaskOnce) {
    JobRig rig;
    std::latch started(1);
    rig.downloader->fn = [&](const net::DownloadRequest&, std::stop_token stop) {
        ++rig.sends;
        started.count_down();
        return waitForStop(stop) ? net::result::canceled()
                                 : net::result::ok(200, "HTTP/1.1 200 OK\r\n", "image/png", "late");
    };
    Finish finish;
    rig.push(rig.task(finish));
    started.wait();
    rig.queue.reset();
    EXPECT_EQ(finish.total(), 1);
    EXPECT_EQ(kindOf(finish.result()), "CanceledAfterDownload");
    EXPECT_EQ(rig.saves.load(), 0);
}

// A hard cancel aborts that one transfer; the queue stays up and the next task of the slot completes.
TEST(TileDownloadJob, HardCancelOfOneTaskLeavesTheQueueServingTheNext) {
    JobRig rig;
    std::latch started(1);
    std::stop_source hard;
    std::atomic<int> calls{0};
    rig.downloader->fn = [&](const net::DownloadRequest&, std::stop_token stop) {
        ++rig.sends;
        if (calls++ == 0) {
            started.count_down();
            return waitForStop(stop) ? net::result::canceled()
                                     : net::result::ok(200, "HTTP/1.1 200 OK\r\n", "image/png", "late");
        }
        return net::result::ok(200, "HTTP/1.1 200 OK\r\n", "image/png", kBody);
    };
    Finish first;
    Finish second;
    rig.push(rig.task(first, {}, hard.get_token()));
    started.wait();
    hard.request_stop();
    ASSERT_TRUE(first.waitFor(1));
    rig.push(rig.task(second));
    ASSERT_TRUE(second.waitFor(1));
    rig.queue.reset();
    EXPECT_EQ(kindOf(first.result()), "CanceledAfterDownload");
    EXPECT_EQ(kindOf(second.result()), "Ok");
    EXPECT_EQ(downloadKindOf(second.result()), ResultKind::Ok);
    EXPECT_EQ(first.total(), 1);
    EXPECT_EQ(second.total(), 1);
}

// Two slots: the queue makes one slot state per slot (twice), the two jobs are in flight together (the latch
// of two inside the fake transport), and every job builds and records its response head against the
// LastResponseInfo of ITS slot (u_TileDownloaderList.pas:146-168: a slot's builder sees the previous answer
// of that slot only). Phase 1: two jobs, both see no head. Phase 2: two more jobs held together again, so
// they are on the two different slots; they see the two heads of phase 1, one each.
TEST(TileDownloadJob, TwoSlotsRunTwoJobsAtOnceEachWithItsOwnLastResponse) {
    JobRig rig(2);
    TwoAtATime together;
    std::mutex mutex;
    std::vector<std::pair<const net::LastResponseInfo*, QByteArray>> seen; // in build order
    rig.builder->fn = [&](const ITileRequestPtr&, const auto& last, const std::stop_token&) {
        const std::lock_guard lock(mutex);
        seen.emplace_back(last.get(), last->responseHead());
        return std::optional<net::DownloadRequest>(requestWithSource());
    };
    rig.downloader->fn = [&](const net::DownloadRequest&, std::stop_token) {
        const int n = together.hold();
        return net::result::badContentType("text/html", 200, "H" + QByteArray::number(n));
    };
    Finish finish;
    rig.push(rig.task(finish));
    rig.push(rig.task(finish));
    together.passPhase(0);
    ASSERT_TRUE(finish.waitFor(2));
    rig.push(rig.task(finish));
    rig.push(rig.task(finish));
    together.passPhase(1);
    ASSERT_TRUE(finish.waitFor(4));
    rig.queue.reset();

    EXPECT_EQ(rig.slotStates.load(), 2);
    ASSERT_EQ(seen.size(), 4u);
    EXPECT_NE(seen[0].first, seen[1].first);
    EXPECT_EQ(seen[0].second, QByteArray());
    EXPECT_EQ(seen[1].second, QByteArray());
    // Phase 2 ran on the same two states, each seeing the head its own slot got (H1 and H2 in some order).
    EXPECT_NE(seen[2].first, seen[3].first);
    EXPECT_TRUE((seen[2].first == seen[0].first && seen[3].first == seen[1].first) ||
                (seen[2].first == seen[1].first && seen[3].first == seen[0].first));
    const std::set<QByteArray> heads{seen[2].second, seen[3].second};
    EXPECT_EQ(heads, (std::set<QByteArray>{"H1", "H2"}));
}

// Rule 6: anything else that escapes the body finishes the task with null and is rethrown to the worker loop
// (D-87: logged, the worker goes on). Here the saver's error result cannot be built (a request without a tile
// source), and the next task of the same slot still runs.
TEST(TileDownloadJob, ExceptionOutsideTheBuilderAndSaverFinishesWithNullAndTheWorkerGoesOn) {
    JobRig rig;
    std::atomic<int> calls{0};
    rig.builder->fn = [&](const auto&, const auto&, const auto&) {
        ++rig.builds;
        net::DownloadRequest r = requestWithSource();
        if (calls++ == 0) {
            r.source.reset(); // ErrorAfterDownloadRequest throws for a request that is not a tile request
        }
        return std::optional<net::DownloadRequest>(std::move(r));
    };
    rig.saver->fn = [&](const auto&, const auto&) -> ITileRequestResultPtr {
        ++rig.saves;
        throw std::runtime_error("диск");
    };
    Finish first;
    Finish second;
    rig.push(rig.task(first));
    ASSERT_TRUE(first.waitFor(1));
    rig.saver->fn = [&](const net::DownloadRequest& request, const net::DownloadResult& result) {
        ++rig.saves;
        return ITileRequestResultPtr(std::make_shared<const TileRequestResultOk>(request, result));
    };
    rig.push(rig.task(second));
    ASSERT_TRUE(second.waitFor(1));
    rig.queue.reset();
    EXPECT_EQ(kindOf(first.result()), "null");
    EXPECT_EQ(kindOf(second.result()), "Ok") << textOf(second.result()).toStdString();
    EXPECT_EQ(first.total(), 1);
}

// A finish subscriber that throws: the exception is not answered by a second notification (the final
// setFinished is outside the try), it reaches the worker loop, which logs it; the process and the worker
// continue.
TEST(TileDownloadJob, ThrowingFinishSubscriberIsCalledOnceAndDoesNotEndTheWorker) {
    JobRig rig;
    Finish first;
    Finish second;
    first.inside = [] { throw std::runtime_error("subscriber"); };
    rig.push(rig.task(first));
    ASSERT_TRUE(first.waitFor(1));
    rig.push(rig.task(second));
    ASSERT_TRUE(second.waitFor(1));
    rig.queue.reset();
    EXPECT_EQ(first.total(), 1);
    EXPECT_EQ(kindOf(first.result()), "Ok");
    EXPECT_EQ(second.total(), 1);
    EXPECT_EQ(kindOf(second.result()), "Ok");
}

namespace {

class ThrowingChecker final : public net::IDownloadChecker {
public:
    std::optional<net::DownloadResult> beforeRequest(const net::DownloadRequest&) const override {
        throw std::runtime_error("checker boom Город");
    }
    std::optional<net::DownloadResult> afterReceiveData(const net::DownloadRequest&, const QByteArray&,
                                                        quint32&, QByteArray&, QByteArray&) const override {
        return std::nullopt;
    }
};

} // namespace

// A checker that throws inside the transport (T-0116: the exception becomes a load error result): the job's
// save callable gets that result, the task ends in DownloadError carrying the message, and the slot serves
// the next task.
TEST(TileDownloadJob, ThrowingCheckerEndsInDownloadErrorAndTheSlotServesTheNextTask) {
    HttpTestServer server;
    ASSERT_TRUE(server.start());
    HttpTestServer::Reply reply;
    reply.headers.append(header("Content-Type", "image/png"));
    reply.body = kBody;
    server.addReply("/ok", reply);

    JobRig rig;
    // The real transport, one CurlDownloader per call: a CurlSession belongs to the thread that made it.
    rig.downloader->fn = [](const net::DownloadRequest& request, std::stop_token stop) {
        net::CurlDownloader curl({}, core::ContentTypeRegistry::standard(), nullptr, nullptr);
        return curl.doRequest(request, std::move(stop));
    };
    std::atomic<bool> throwing{true};
    rig.builder->fn = [&](const auto&, const auto&, const auto&) {
        ++rig.builds;
        net::DownloadRequest r = requestWithSource();
        r.url = server.url("/ok");
        auto inet = std::make_shared<net::InetSettings>();
        inet->timeoutMs = 5000;
        r.inet = inet;
        if (throwing) {
            r.checker = std::make_shared<ThrowingChecker>();
        }
        return std::optional<net::DownloadRequest>(std::move(r));
    };
    rig.saver->fn = [&](const net::DownloadRequest& request, const net::DownloadResult& result) {
        if (result.kind != ResultKind::Ok) {
            return ITileRequestResultPtr();
        }
        return ITileRequestResultPtr(std::make_shared<const TileRequestResultOk>(request, result));
    };
    Finish first;
    Finish second;
    rig.push(rig.task(first));
    ASSERT_TRUE(first.waitFor(1));
    throwing = false;
    rig.push(rig.task(second));
    ASSERT_TRUE(second.waitFor(1));
    rig.queue.reset();
    EXPECT_EQ(kindOf(first.result()), "DownloadError");
    EXPECT_EQ(textOf(first.result()), u"Download error: checker boom Город"_s);
    // The load error of a failed checker is a TDownloadResultUnknownError, IsServerExists = False
    // (u_DownloadResult.pas:410-413): a connect error for the retry loop, so the first task built twice.
    EXPECT_EQ(rig.builds.load(), 3); // two attempts of the first task, one of the second
    EXPECT_EQ(kindOf(second.result()), "Ok") << textOf(second.result()).toStdString();
    EXPECT_EQ(first.total(), 1);
    EXPECT_EQ(second.total(), 1);
}

// =================================================================================================================
// SubsystemRig
// =================================================================================================================

namespace {

// The real transport, one CurlDownloader per call (a CurlSession belongs to the thread that made it, and the
// slots' downloaders are made on one worker and dropped on another), telling the test about every request
// first.
class HookDownloader final : public net::IDownloader {
public:
    HookDownloader(net::DownloaderOptions options, std::function<void()> hook)
        : options_(std::move(options)), hook_(std::move(hook)) {}
    net::DownloadResult doRequest(const net::DownloadRequest& request, std::stop_token stop) override {
        if (hook_) {
            hook_();
        }
        net::CurlDownloader curl(options_, core::ContentTypeRegistry::standard(), nullptr, nullptr);
        return curl.doRequest(request, std::move(stop));
    }

private:
    net::DownloaderOptions options_;
    std::function<void()> hook_;
};

class HookFactory final : public net::IDownloaderFactory {
public:
    explicit HookFactory(std::function<void()> hook) : hook_(std::move(hook)) {}
    net::IDownloaderPtr build(const net::DownloaderOptions& options) override {
        return std::make_shared<HookDownloader>(options, hook_);
    }

private:
    std::function<void()> hook_;
};

struct ZmpSpec {
    QByteArray params; // extra [PARAMS] lines
    QByteArray script; // GetUrlScript.txt; empty = template only
    std::vector<QByteArray> emptySamples;
    std::vector<QByteArray> banSamples;
};

ZmpSpec withParams(QByteArray params) {
    ZmpSpec spec;
    spec.params = std::move(params);
    return spec;
}

ZmpSpec withScript(QByteArray script) {
    ZmpSpec spec;
    spec.script = std::move(script);
    return spec;
}

void writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
}

class SubsystemRig {
public:
    HttpTestServer server;
    maptype::test::ZmpEnv env;
    QTemporaryDir root;
    cg::core::Notifier gc;
    std::stop_source appClosing;
    std::shared_ptr<net::InetConfig> inet = std::make_shared<net::InetConfig>();
    std::shared_ptr<net::TileDownloaderConfig> downloaderConfig;
    std::shared_ptr<TileDownloadRequestBuilderConfig> builderConfig;
    std::shared_ptr<MapAbilitiesConfig> abilities;
    std::shared_ptr<net::GlobalDownloadConfig> globalConfig = std::make_shared<net::GlobalDownloadConfig>();
    std::shared_ptr<storage::FileSystemTileStorage> storage;
    std::function<void()> onRequest;
    // A fake transport instead of curl (set before build()): the subsystem's slots, provider and anti-ban
    // fetch all take their downloaders from it.
    std::shared_ptr<net::IDownloaderFactory> transport;
    IZmpInfoPtr zmp;
    std::unique_ptr<TileDownloadSubsystem> subsystem;

    SubsystemRig() {
        started_ = server.start();
        inet->proxy().setUseSystemSettings(false);
        inet->setTimeoutMs(5000);
        inet->setSleepOnResetConnectionMs(0);
    }

    [[nodiscard]] bool serverStarted() const { return started_; }

    // A throw-away template-only (or scripted) ZMP whose URL template points at the fixture server.
    IZmpInfoPtr tempZmp(const ZmpSpec& spec) {
        const QString dir = root.filePath(u"maps/t.zmp"_s);
        QByteArray params =
            "[PARAMS]\r\nGUID={D643C5A1-3333-4387-9BE2-DDDF185C1111}\r\nname=T\r\nNameInCache=cache\r\n"
            "DefURLBase=" +
            server.url("/{z}/{x}/{y}.png") +
            "\r\nContentType=image/png\r\nExt=.png\r\n"
            "projection=1\r\nsradiusa=6378137\r\nsradiusb=6356752\r\n" +
            spec.params;
        writeFile(dir + u"/params.txt"_s, params);
        if (!spec.script.isEmpty()) {
            writeFile(dir + u"/GetUrlScript.txt"_s, spec.script);
        }
        int n = 0;
        for (const QByteArray& s : spec.emptySamples) {
            writeFile(dir + u"/EmptyTiles/e"_s + QString::number(n++) + u".bin"_s, s);
        }
        for (const QByteArray& s : spec.banSamples) {
            writeFile(dir + u"/BanTiles/b"_s + QString::number(n++) + u".bin"_s, s);
        }
        return load(u"t.zmp"_s, root.filePath(u"maps"_s));
    }

    // T-0225: a throw-away *.source.json source with a JavaScript `script`; its `url` is the fixture server's
    // template.
    IZmpInfoPtr tempSourceJson(const QByteArray& code) {
        QJsonObject o;
        o.insert(u"format"_s, 1);
        o.insert(u"guid"_s, u"{D643C5A1-3333-4387-9BE2-DDDF185C1112}"_s);
        o.insert(u"name"_s, u"TJ"_s);
        o.insert(u"url"_s, QString::fromUtf8(server.url("/{z}/{x}/{y}.png")));
        o.insert(u"contentType"_s, u"image/png"_s);
        o.insert(u"ext"_s, u".png"_s);
        o.insert(u"nameInCache"_s, u"cache"_s);
        o.insert(u"script"_s, QJsonObject{{u"code"_s, QString::fromUtf8(code)}});
        writeFile(root.filePath(u"maps/t.source.json"_s), QJsonDocument(o).toJson());
        return load(u"t.source.json"_s, root.filePath(u"maps"_s));
    }

    IZmpInfoPtr stockZmp(const QString& relative) {
        return load(relative, maptype::test::fixtureDir() + u"/zmp/sas.maps"_s);
    }

    // Builds the subsystem for `zmp`. `urlBase` replaces the builder config's template (stock ZMPs point at
    // real hosts).
    void build(IZmpInfoPtr map, std::optional<QByteArray> urlBase = std::nullopt,
               storage::TileStorageAbilities storageAbilities = storage::TileStorageAbilities::readWrite()) {
        zmp = std::move(map);
        auto builderStatic = std::make_shared<TileDownloadRequestBuilderConfigStatic>(
            *zmp->tileDownloadRequestBuilderConfig());
        if (urlBase) {
            builderStatic->urlBase = *urlBase;
        }
        builderConfig = std::make_shared<TileDownloadRequestBuilderConfig>(builderStatic);
        downloaderConfig = std::make_shared<net::TileDownloaderConfig>(inet, *zmp->tileDownloaderConfig());
        abilities = std::make_shared<MapAbilitiesConfig>(zmp->abilities());
        const core::ContentTypeInfo png{"image/png", ".png", core::ContentKind::Bitmap};
        storage = std::make_shared<storage::FileSystemTileStorage>(
            storage::tileStorageTypeAbilities(storage::TileStorageKind::FolderSimple), storageAbilities,
            std::make_shared<storage::MapVersionFactory>(), zmp->projectionSet(),
            std::make_shared<storage::TilePyramidNotifier>(), cacheDir(), png,
            std::make_shared<storage::Gm1TileNameCodec>(), nullptr);
        subsystem = std::make_unique<TileDownloadSubsystem>(
            zmp, gc, appClosing.get_token(), std::make_shared<test::FakeLanguageManager>(), globalConfig,
            transport ? transport : std::make_shared<HookFactory>([this] {
                if (onRequest) {
                    onRequest();
                }
            }),
            std::make_shared<storage::MapVersionFactory>(), downloaderConfig, builderConfig,
            std::shared_ptr<const core::ContentTypeRegistry>(std::shared_ptr<void>(),
                                                             &core::ContentTypeRegistry::standard()),
            test::testContentConverters(), png, abilities, std::make_shared<test::CountingProjFactory>(),
            storage, 7, root.filePath(u"logs"_s));
    }

    [[nodiscard]] QString cacheDir() const { return root.filePath(u"cache"_s) + u"/"_s; }
    [[nodiscard]] std::optional<QByteArray> file(const QString& relative) const {
        QFile f(cacheDir() + relative);
        return f.open(QIODevice::ReadOnly) ? std::optional<QByteArray>(f.readAll()) : std::nullopt;
    }
    [[nodiscard]] QStringList files() const {
        QStringList out;
        QDirIterator it(cacheDir(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            out.append(QDir(cacheDir()).relativeFilePath(it.next()));
        }
        out.sort();
        return out;
    }
    // The request targets the server saw, in order.
    [[nodiscard]] QList<QByteArray> paths() const {
        QList<QByteArray> out;
        for (const QByteArray& raw : server.requestLog()) {
            const QList<QByteArray> parts = raw.left(raw.indexOf("\r\n")).split(' ');
            out.append(parts.size() > 1 ? parts[1] : QByteArray());
        }
        return out;
    }
    void reply(const QByteArray& path, const QByteArray& type, const QByteArray& body, int delayMs = 0) {
        HttpTestServer::Reply r;
        r.headers.append(header("Content-Type", type));
        r.body = body;
        r.delayMs = delayMs;
        server.addReply(path, std::move(r));
    }

    // Asks for the task of tile (x, y) at zoom 5 and downloads it; the notification goes to `finish`.
    ITileRequestTaskPtr download(Finish& finish, int x = 3, int y = 7, bool checkSize = false) {
        const ITileRequestTaskPtr task =
            subsystem->getRequestTask({}, {}, finish.notifier(), geo::PointI{x, y}, 5, nullptr, checkSize);
        EXPECT_NE(task, nullptr);
        subsystem->download(task);
        return task;
    }

private:
    IZmpInfoPtr load(const QString& name, const QString& rootDir) {
        QStringList errors;
        set_ = env.load(rootDir, {name}, errors);
        EXPECT_TRUE(errors.isEmpty()) << errors.join(u'\n').toStdString();
        const auto guids = set_->guids();
        return guids.empty() ? nullptr : set_->zmpByGuid(guids.front());
    }

    bool started_ = false;
    std::unique_ptr<ZmpInfoSet> set_;
};

const QByteArray kPng = "\x89PNG-tile-\xd0\x93\xd0\xbe";

} // namespace

// A stock template-only ZMP (4umaps.eu, no GetUrlScript.txt) downloads one tile through the whole subsystem
// into a temp storage: URL from the template, transport, checker, saver, the map's content type.
TEST(TileDownloadSubsystem, StockTemplateOnlyZmpDownloadsATileIntoTheStorage) {
    CG_SKIP_IF_NO_CORPUS();
    SubsystemRig rig;
    ASSERT_TRUE(rig.serverStarted());
    rig.reply("/5/3/7.png", "image/png", kPng);
    rig.build(rig.stockZmp(u"_osm/4umaps.eu.zmp"_s), rig.server.url("/{z}/{x}/{y}.png"));
    ASSERT_TRUE(rig.subsystem->state()->getStatic()->enabled);

    Finish finish;
    (void)rig.download(finish);
    ASSERT_TRUE(finish.waitFor(1));
    rig.subsystem.reset(); // joins the worker: a second notification would have happened by now
    EXPECT_EQ(finish.total(), 1);
    EXPECT_EQ(kindOf(finish.result()), "Ok");
    EXPECT_NE(finish.threads.front(), std::this_thread::get_id()); // finished on a worker
    EXPECT_EQ(rig.paths(), QList<QByteArray>{"/5/3/7.png"});
    ASSERT_TRUE(rig.file(u"z5/7/3.png"_s).has_value());
    EXPECT_EQ(*rig.file(u"z5/7/3.png"_s), kPng);
    EXPECT_EQ(rig.files(), QStringList{u"z5/7/3.png"_s});
}

// ExpectedMIMETypes / IgnoreMIMEType through the subsystem: a body of another type is rejected with the
// BadContentType kind and nothing is stored; with IgnoreMIMEType the same answer is stored under the ZMP's
// DefaultMIMEType.
TEST(TileDownloadSubsystem, MimeValidationRejectsOrIgnoresThroughTheWholeSubsystem) {
    struct Row {
        const char* name;
        const char* params;
        const char* expectKind;
        ResultKind expectDownloadKind;
        QStringList expectFiles;
    };
    const Row rows[] = {
        {"another type: rejected", "", "DownloadError", ResultKind::BadContentType, {}},
        {"IgnoreContentType: stored",
         "IgnoreContentType=1\r\nDefaultContentType=image/png\r\n",
         "Ok",
         ResultKind::Ok,
         {u"z5/7/3.png"_s}},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        SubsystemRig rig;
        ASSERT_TRUE(rig.serverStarted());
        rig.reply("/5/3/7.png", "text/html", kPng);
        rig.build(rig.tempZmp(withParams(row.params)));
        Finish finish;
        (void)rig.download(finish);
        ASSERT_TRUE(finish.waitFor(1));
        rig.subsystem.reset();
        EXPECT_EQ(finish.total(), 1);
        EXPECT_EQ(kindOf(finish.result()), row.expectKind);
        EXPECT_EQ(downloadKindOf(finish.result()), row.expectDownloadKind);
        EXPECT_EQ(rig.files(), row.expectFiles);
    }
}

// EmptyTiles / BanTiles samples through the subsystem (u_DownloadCheckerStuped.pas:184-200, saver :273-285).
TEST(TileDownloadSubsystem, SampleListsGiveEmptyAndBannedTilesThroughTheWholeSubsystem) {
    const QByteArray sample = "EMPTY-\xd0\x93\xd0\xbe\xd1\x80\xd0\xbe\xd0\xb4";
    struct Row {
        const char* name;
        bool empty; // the sample goes to EmptyTiles (else BanTiles)
        bool saveTne;
        const char* expectKind;
        ResultKind expectDownloadKind;
        const char16_t* expectDownloadText;
        QStringList expectFiles;
    };
    const Row rows[] = {
        {"empty sample, TNE option off",
         true,
         false,
         "Ok",
         ResultKind::DataNotExists,
         u"Tile is recognized as empty",
         {}},
        {"empty sample, TNE option on",
         true,
         true,
         "Ok",
         ResultKind::DataNotExists,
         u"Tile is recognized as empty",
         {u"z5/7/3.tne"_s}},
        {"ban sample", false, true, "DownloadError", ResultKind::Banned, nullptr, {}},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        SubsystemRig rig;
        ASSERT_TRUE(rig.serverStarted());
        rig.reply("/5/3/7.png", "image/png", sample);
        rig.globalConfig->setSaveTileNotExists(row.saveTne);
        ZmpSpec spec;
        (row.empty ? spec.emptySamples : spec.banSamples).push_back(sample);
        rig.build(rig.tempZmp(spec));
        Finish finish;
        (void)rig.download(finish);
        ASSERT_TRUE(finish.waitFor(1));
        rig.subsystem.reset();
        EXPECT_EQ(finish.total(), 1);
        EXPECT_EQ(kindOf(finish.result()), row.expectKind);
        EXPECT_EQ(downloadKindOf(finish.result()), row.expectDownloadKind);
        if (row.expectDownloadText != nullptr) {
            const auto* with =
                dynamic_cast<const ITileRequestResultWithDownloadResult*>(finish.result().get());
            ASSERT_NE(with, nullptr);
            EXPECT_EQ(with->downloadResult().text, QString::fromUtf16(row.expectDownloadText));
        }
        EXPECT_EQ(rig.files(), row.expectFiles);
    }
}

// UsePreloadPage / PreloadPage from params.txt reach the request path: the preload page is requested from the
// server before the tile that triggers it (u_AntiBanStuped.pas:123-139: 1 = once, on the first tile; N > 1 =
// every Nth).
TEST(TileDownloadSubsystem, PreloadPageIsRequestedBeforeTheTileThatTriggersIt) {
    struct Row {
        const char* name;
        const char* usePreloadPage;
        QList<QByteArray> expectPaths;
    };
    const Row rows[] = {
        {"1: once, before the first tile", "1", {"/preload", "/5/3/7.png", "/5/3/8.png"}},
        {"2: before every second tile", "2", {"/5/3/7.png", "/preload", "/5/3/8.png"}},
        {"0: never", "0", {"/5/3/7.png", "/5/3/8.png"}},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        SubsystemRig rig;
        ASSERT_TRUE(rig.serverStarted());
        rig.reply("/preload", "text/html", "page");
        rig.reply("/5/3/7.png", "image/png", kPng);
        rig.reply("/5/3/8.png", "image/png", kPng);
        rig.build(rig.tempZmp(withParams(QByteArray("UsePreloadPage=") + row.usePreloadPage +
                                         "\r\nPreloadPage=" + rig.server.url("/preload") + "\r\n")));
        Finish first;
        Finish second;
        (void)rig.download(first, 3, 7);
        ASSERT_TRUE(first.waitFor(1));
        (void)rig.download(second, 3, 8);
        ASSERT_TRUE(second.waitFor(1));
        rig.subsystem.reset();
        EXPECT_EQ(rig.paths(), row.expectPaths);
        EXPECT_EQ(kindOf(first.result()), "Ok");
        EXPECT_EQ(kindOf(second.result()), "Ok");
    }
}

// The three disabled states, their texts, and what a disabled subsystem answers
// (u_TileDownloadSubsystemState.pas :80-108; u_TileDownloadSubsystem.pas:429-437, :496-497).
TEST(TileDownloadSubsystem, DisabledStatesGiveTheirTextsAndANullAnswerEverywhere) {
    struct Row {
        const char* name;
        const char* params;
        bool abilitiesOff;
        bool readOnlyStorage;
        const char16_t* expectReason;
    };
    const Row rows[] = {
        {"by the ZMP", "UseDwn=0\r\n", false, false, u"Disabled by Zmp"},
        {"by the map's abilities", "", true, false, u"Disabled by map params"},
        {"by the saver: read-only storage", "", false, true, u"No write access to tile storage"},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        SubsystemRig rig;
        ASSERT_TRUE(rig.serverStarted());
        rig.build(rig.tempZmp(withParams(row.params)), std::nullopt,
                  row.readOnlyStorage ? storage::TileStorageAbilities::readOnly()
                                      : storage::TileStorageAbilities::readWrite());
        if (row.abilitiesOff) {
            rig.abilities->setUseDownload(false);
        }
        const auto state = rig.subsystem->state()->getStatic();
        EXPECT_FALSE(state->enabled);
        EXPECT_EQ(state->disableReason, QString::fromUtf16(row.expectReason));
        EXPECT_EQ(rig.subsystem->getRequestTask({}, {}, nullptr, geo::PointI{3, 7}, 5, nullptr, false),
                  nullptr);

        // download() of a task made elsewhere finishes it with null at once, on this thread.
        Finish finish;
        const auto task = std::make_shared<TileRequestTask>(tileRequest(), std::stop_token{},
                                                            std::stop_token{}, finish.notifier());
        rig.subsystem->download(task);
        EXPECT_EQ(finish.total(), 1);
        EXPECT_EQ(kindOf(finish.result()), "null");
        EXPECT_EQ(finish.threads.front(), std::this_thread::get_id());
        EXPECT_TRUE(rig.paths().isEmpty());
    }
}

// getRequest asks the request-builder factory's state, not the subsystem's: a map whose download is switched
// off in its abilities still returns its URL (u_TileDownloadSubsystem.pas:453-454); a ZMP with downloading
// off does not.
TEST(TileDownloadSubsystem, GetRequestOnADownloadDisabledMapStillReturnsTheUrl) {
    {
        SubsystemRig rig;
        ASSERT_TRUE(rig.serverStarted());
        rig.build(rig.tempZmp({}));
        rig.abilities->setUseDownload(false);
        ASSERT_FALSE(rig.subsystem->state()->getStatic()->enabled);
        const auto request = rig.subsystem->getRequest(geo::PointI{3, 7}, 5, nullptr);
        ASSERT_TRUE(request.has_value());
        EXPECT_EQ(request->url, rig.server.url("/5/3/7.png"));
        EXPECT_EQ(request->method, net::Method::Get);
        EXPECT_NE(request->checker,
                  nullptr); // the checker rides on every request the factory's builders make
        ASSERT_NE(tileRequestFromSource(request->source), nullptr);
        EXPECT_EQ(tileRequestFromSource(request->source)->tile(), (geo::PointI{3, 7}));
        // A config change swaps the provider's pair (R4: only its own lock); the next call still works. That
        // the new pair is made with the new options is pinned by
        // ConfigChangeRebuildsTheProvidersDownloader....
        rig.downloaderConfig->setAllowUseCookie(true);
        EXPECT_EQ(rig.subsystem->getRequest(geo::PointI{4, 8}, 5, nullptr)->url,
                  rig.server.url("/5/4/8.png"));
        EXPECT_TRUE(rig.paths().isEmpty()); // nothing was fetched
    }
    {
        SubsystemRig rig;
        ASSERT_TRUE(rig.serverStarted());
        rig.build(rig.tempZmp(withParams("UseDwn=0\r\n")));
        EXPECT_FALSE(rig.subsystem->getRequest(geo::PointI{3, 7}, 5, nullptr).has_value());
    }
}

// The destroy token: once the app-closing token fires, builds of getRequest are cancelled (the lazy builder
// answers nothing); a token that is already stopped at construction acts at once (the stop_callback runs
// inline).
TEST(TileDownloadSubsystem, AppClosingCancelsGetRequestBuilds) {
    SubsystemRig rig;
    ASSERT_TRUE(rig.serverStarted());
    rig.build(rig.tempZmp({}));
    EXPECT_TRUE(rig.subsystem->getRequest(geo::PointI{3, 7}, 5, nullptr).has_value());
    rig.appClosing.request_stop();
    EXPECT_FALSE(rig.subsystem->getRequest(geo::PointI{3, 7}, 5, nullptr).has_value());

    SubsystemRig closed;
    ASSERT_TRUE(closed.serverStarted());
    closed.appClosing.request_stop();
    closed.build(closed.tempZmp({}));
    EXPECT_FALSE(closed.subsystem->getRequest(geo::PointI{3, 7}, 5, nullptr).has_value());
}

// A script that does not compile: the first call that compiles still gets a template-only builder (LD-18), so
// the first tile downloads from the template; the state is then disabled with the compiler's text, and from
// then on download() finishes with null at once and getRequestTask answers null.
TEST(TileDownloadSubsystem, CompileErrorDisablesTheSubsystemAfterTheFirstTile) {
    const QByteArray broken = "var A: string;\nbegin\n  A := 'unterminated1\n  A := 'unterminated2\nend.";
    SubsystemRig rig;
    ASSERT_TRUE(rig.serverStarted());
    rig.reply("/5/3/7.png", "image/png", kPng);
    rig.build(rig.tempZmp(withScript(broken)));
    ASSERT_TRUE(rig.subsystem->state()->getStatic()->enabled);
    Finish first;
    (void)rig.download(first);
    ASSERT_TRUE(first.waitFor(1));
    EXPECT_EQ(kindOf(first.result()), "Ok");

    const auto state = rig.subsystem->state()->getStatic();
    EXPECT_FALSE(state->enabled);
    EXPECT_TRUE(state->disableReason.startsWith(u"Compilation error in script \r\n[Line 3"_s))
        << state->disableReason.toStdString();
    EXPECT_EQ(rig.subsystem->getRequestTask({}, {}, nullptr, geo::PointI{3, 7}, 5, nullptr, false), nullptr);
    Finish second;
    const auto task = std::make_shared<TileRequestTask>(tileRequest(), std::stop_token{}, std::stop_token{},
                                                        second.notifier());
    rig.subsystem->download(task);
    EXPECT_EQ(second.total(), 1);
    EXPECT_EQ(kindOf(second.result()), "null");
    rig.subsystem.reset();
    EXPECT_EQ(first.total(), 1);
}

// A builder that throws and a builder that returns nothing, with the real script builder behind the
// subsystem: the exception's message / "Tile does not exist" is the text of the result, nothing is requested,
// the state stays enabled.
TEST(TileDownloadSubsystem, ScriptBuilderThatThrowsOrReturnsNothingGivesAnErrorBeforeBuild) {
    struct Row {
        const char* name;
        const char* script;
        const char16_t* expectText; // exact; null = contains "Division by zero"
    };
    const Row rows[] = {
        {"script raises", "begin ResultURL := inttostr(10 div (getx - getx)); end.", nullptr},
        {"script gives an empty URL", "begin ResultURL := ''; end.", u"Tile does not exist"},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        SubsystemRig rig;
        ASSERT_TRUE(rig.serverStarted());
        rig.build(rig.tempZmp(withScript(row.script)));
        Finish finish;
        (void)rig.download(finish);
        ASSERT_TRUE(finish.waitFor(1));
        rig.subsystem.reset();
        EXPECT_EQ(finish.total(), 1);
        EXPECT_EQ(kindOf(finish.result()), "ErrorBeforeBuild");
        if (row.expectText != nullptr) {
            EXPECT_EQ(textOf(finish.result()), QString::fromUtf16(row.expectText));
        } else {
            EXPECT_TRUE(textOf(finish.result()).contains(u"Division by zero"_s))
                << textOf(finish.result()).toStdString();
        }
        EXPECT_TRUE(rig.paths().isEmpty());
        EXPECT_TRUE(rig.files().isEmpty());
    }
}

TEST(TileDownloadSubsystem, GetRequestTaskChecksTheTileGridAndPicksTheRequestKind) {
    SubsystemRig rig;
    ASSERT_TRUE(rig.serverStarted());
    rig.build(rig.tempZmp({}));
    auto& s = *rig.subsystem;
    const auto notifier = Finish().notifier();
    // Zoom 5 has 32 x 32 tiles.
    EXPECT_NE(s.getRequestTask({}, {}, notifier, geo::PointI{31, 31}, 5, nullptr, false), nullptr);
    EXPECT_EQ(s.getRequestTask({}, {}, notifier, geo::PointI{32, 0}, 5, nullptr, false), nullptr);
    EXPECT_EQ(s.getRequestTask({}, {}, notifier, geo::PointI{0, -1}, 5, nullptr, false), nullptr);
    const auto plain = s.getRequestTask({}, {}, notifier, geo::PointI{3, 7}, 5, nullptr, false);
    const auto sized = s.getRequestTask({}, {}, notifier, geo::PointI{3, 7}, 5, nullptr, true);
    ASSERT_NE(plain, nullptr);
    ASSERT_NE(sized, nullptr);
    EXPECT_EQ(dynamic_cast<const ITileRequestWithSizeCheck*>(plain->tileRequest().get()), nullptr);
    EXPECT_NE(dynamic_cast<const ITileRequestWithSizeCheck*>(sized->tileRequest().get()), nullptr);
    EXPECT_EQ(sized->tileRequest()->tile(), (geo::PointI{3, 7}));
    EXPECT_EQ(sized->tileRequest()->zoom(), 5);
    // A zoom the projection set does not have raises, as legacy's list index does.
    EXPECT_THROW((void)s.getRequestTask({}, {}, notifier, geo::PointI{0, 0}, 40, nullptr, false),
                 std::out_of_range);
}

// R1 under the sanitizers: the subsystem is destroyed while a transfer is in flight. The fake transport holds
// the transfer until its stop token fires, so the stop can only come from the destructor: the queue's stop
// reaches the job's hard token, the transfer ends Canceled, the job classifies it CanceledAfterDownload
// (u_TileDownloaderSimple.pas:222-225, unsaved), the task is finished exactly once, nothing is stored, and
// the saver has unsubscribed from the storage state by then.
TEST(TileDownloadSubsystem, DestructionWhileAJobIsInFlightFinishesTheTaskOnce) {
    SubsystemRig rig;
    ASSERT_TRUE(rig.serverStarted());
    std::latch inFlight(1);
    auto fake = std::make_shared<FnDownloader>();
    fake->fn = [&](const net::DownloadRequest&, std::stop_token stop) {
        inFlight.count_down();
        return waitForStop(stop) ? net::result::canceled()
                                 : net::result::ok(200, "HTTP/1.1 200 OK\r\n", "image/png", kPng);
    };
    rig.transport = std::make_shared<FnDownloaderFactory>(fake);
    rig.build(rig.tempZmp({}));
    EXPECT_EQ(rig.storage->stateChangeable().changed().subscriberCount(), 1u); // the saver listens

    Finish finish;
    (void)rig.download(finish);
    inFlight.wait();
    rig.subsystem.reset();
    EXPECT_EQ(finish.total(), 1);
    EXPECT_EQ(kindOf(finish.result()), "CanceledAfterDownload");
    EXPECT_TRUE(rig.files().isEmpty());
    EXPECT_EQ(rig.storage->stateChangeable().changed().subscriberCount(), 0u); // the saver has unsubscribed
}

// The subsystem's own slot-state factory (u_TileDownloadSubsystem.pas:365-382 -> u_TileDownloaderList.pas:
// 146-168), not a test copy: with two connections, two jobs are held together inside the fake transport
// (so they run on two slots), and a script builder that prints ResponseHead as its URL shows, in the second
// pair, the two different heads the two slots got from the first pair. One shared slot state would show the
// same head twice. The fake answers BadContentType, which the saver turns into a DownloadError without
// touching the storage.
TEST(TileDownloadSubsystem, EachSlotKeepsItsOwnLastResponseHeadForItsScript) {
    SubsystemRig rig;
    ASSERT_TRUE(rig.serverStarted());
    TwoAtATime together;
    std::mutex mutex;
    std::vector<QByteArray> urls; // in the order of arrival at the transport
    auto fake = std::make_shared<FnDownloader>();
    fake->fn = [&](const net::DownloadRequest& request, std::stop_token) {
        {
            const std::lock_guard lock(mutex);
            urls.push_back(request.url);
        }
        const int n = together.hold();
        return net::result::badContentType("text/html", 200, "H" + QByteArray::number(n));
    };
    rig.transport = std::make_shared<FnDownloaderFactory>(fake);
    ZmpSpec spec = withScript("begin ResultURL := 'http://h/' + ResponseHead; end.");
    spec.params = "MaxConnectToServerCount=2\r\n";
    rig.build(rig.tempZmp(spec));

    Finish finish;
    (void)rig.download(finish, 0, 7);
    (void)rig.download(finish, 1, 7);
    together.passPhase(0);
    ASSERT_TRUE(finish.waitFor(2));
    (void)rig.download(finish, 2, 7);
    (void)rig.download(finish, 3, 7);
    together.passPhase(1);
    ASSERT_TRUE(finish.waitFor(4));
    rig.subsystem.reset();

    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(kindOf(finish.result(i)), "DownloadError");
    }
    ASSERT_EQ(urls.size(), 4u);
    EXPECT_EQ(urls[0], "http://h/");
    EXPECT_EQ(urls[1], "http://h/");
    EXPECT_EQ((std::set<QByteArray>{urls[2], urls[3]}), (std::set<QByteArray>{"http://h/H1", "http://h/H2"}));
}

namespace {

// One instance per build(), with its own cookie jar when the options allow cookies (the observable difference
// of a shared downloader, FEAT-NET-009). Every request is logged with the id of the instance, the jar it
// presented and, for a request to the nested URL, the cookie that instance then stores (Set-Cookie of the
// answer).
class JarDownloader final : public net::IDownloader {
public:
    struct Entry {
        int id;
        QByteArray url;
        std::set<QByteArray> presented;
    };
    struct Log {
        std::mutex mutex;
        std::vector<Entry> entries;
        int builds = 0;
        std::function<void()> afterNested; // runs on the slot's thread inside the nested request
        std::function<void()> afterTile;   // same, inside the tile request
    };
    JarDownloader(int id, bool cookies, std::shared_ptr<Log> log)
        : id_(id), cookies_(cookies), log_(std::move(log)) {}
    net::DownloadResult doRequest(const net::DownloadRequest& request, std::stop_token /*stop*/) override {
        const bool nested = request.url == "http://d/";
        {
            const std::lock_guard lock(log_->mutex);
            log_->entries.push_back({id_, request.url, jar_});
        }
        if (nested) {
            if (log_->afterNested) {
                log_->afterNested();
            }
            if (cookies_) {
                jar_.insert("c" + QByteArray::number(id_)); // only this instance's jar
            }
            return net::result::ok(200, "HTTP/1.1 200 OK\r\n", "text/html", "x");
        }
        if (log_->afterTile) {
            log_->afterTile();
        }
        return net::result::badContentType("text/html", 200, "H");
    }

private:
    int id_;
    bool cookies_;
    std::shared_ptr<Log> log_;
    std::set<QByteArray> jar_;
};

class JarFactory final : public net::IDownloaderFactory {
public:
    std::shared_ptr<JarDownloader::Log> log = std::make_shared<JarDownloader::Log>();
    net::IDownloaderPtr build(const net::DownloaderOptions& options) override {
        const std::lock_guard lock(log->mutex);
        return std::make_shared<JarDownloader>(++log->builds, options.allowCookies, log);
    }
};

const char* const kNestedScript = "var h, d: AnsiString; begin "
                                  "if Downloader.DoHttpRequest('http://d/', '', '', h, d) = 200 then "
                                  "ResultURL := 'http://t/'; end.";

} // namespace

// u_TileDownloaderList.pas:143-168 through the subsystem's own slot-state factory (D-111 removed by T-0143):
// ONE downloader per slot serves the script's nested HTTP and the tile transfer. One slot, a script with a
// nested request, then the transfer: the counting factory built ONE downloader (it was two while the builder
// had its own), and the nested and the tile request were served by the same instance.
TEST(TileDownloadSubsystem, TheSlotsBuilderAndTransferShareOneDownloader) {
    SubsystemRig rig;
    ASSERT_TRUE(rig.serverStarted());
    auto factory = std::make_shared<JarFactory>();
    rig.transport = factory;
    ZmpSpec spec = withScript(kNestedScript);
    spec.params = "IsUseDownloaderInScript=1\r\nMaxConnectToServerCount=1\r\n";
    rig.build(rig.tempZmp(spec));

    Finish finish;
    (void)rig.download(finish);
    ASSERT_TRUE(finish.waitFor(1));
    rig.subsystem.reset();

    EXPECT_EQ(kindOf(finish.result()), "DownloadError"); // BadContentType from the fake
    const auto& log = *factory->log;
    ASSERT_EQ(log.entries.size(), 2U);
    EXPECT_EQ(log.entries[0].url, "http://d/");
    EXPECT_EQ(log.entries[1].url, "http://t/");
    EXPECT_EQ(log.entries[0].id, log.entries[1].id);
    EXPECT_EQ(log.builds, 1);
}

// The observable difference (AllowUseCookie=1): a cookie set by the nested request of a slot is presented by
// the tile request of the SAME slot and not by another slot's. Two slots build and transfer at once: both are
// inside their nested request together (phase 0), then both inside their tile request together (phase 1), so
// the overlap of builder and transfer on two slots is asserted, not hoped for (the TSan run of the sharing).
TEST(TileDownloadSubsystem, ACookieOfTheNestedRequestIsPresentedByTheTileRequestOfTheSameSlotOnly) {
    SubsystemRig rig;
    ASSERT_TRUE(rig.serverStarted());
    TwoAtATime together;
    auto factory = std::make_shared<JarFactory>();
    factory->log->afterNested = [&] { (void)together.hold(); };
    factory->log->afterTile = [&] { (void)together.hold(); };
    rig.transport = factory;
    ZmpSpec spec = withScript(kNestedScript);
    spec.params = "IsUseDownloaderInScript=1\r\nMaxConnectToServerCount=2\r\nAllowUseCookie=1\r\n";
    rig.build(rig.tempZmp(spec));

    Finish finish;
    (void)rig.download(finish, 0, 7);
    (void)rig.download(finish, 1, 7);
    together.passPhase(0);
    together.passPhase(1);
    ASSERT_TRUE(finish.waitFor(2));
    rig.subsystem.reset();

    const auto& log = *factory->log;
    EXPECT_EQ(log.builds, 2);
    ASSERT_EQ(log.entries.size(), 4U);
    std::set<int> tileIds;
    for (const auto& entry : log.entries) {
        if (entry.url == "http://d/") {
            EXPECT_TRUE(entry.presented.empty());
        } else {
            EXPECT_EQ(entry.url, "http://t/");
            // exactly the cookie its own instance got from its own nested request
            EXPECT_EQ(entry.presented, (std::set<QByteArray>{"c" + QByteArray::number(entry.id)}));
            tileIds.insert(entry.id);
        }
    }
    EXPECT_EQ(tileIds, (std::set<int>{1, 2}));
}

// u_TileDownloadSubsystem.pas:212-215: a change of the tile-downloader config rebuilds the provider's pair,
// so the script's nested HTTP of getRequest goes through a downloader made with the NEW AllowUseCookie. Seen
// from the fake transport factory: the options of every downloader it was asked for. (No task runs here, so
// no slot downloader is made; every build() is the provider's TtlDownloader.)
TEST(TileDownloadSubsystem, ConfigChangeRebuildsTheProvidersDownloaderWithTheNewOptions) {
    SubsystemRig rig;
    ASSERT_TRUE(rig.serverStarted());
    auto fake = std::make_shared<FnDownloader>();
    fake->fn = [](const net::DownloadRequest&, std::stop_token) {
        return net::result::ok(200, "HTTP/1.1 200 OK\r\n", "text/html", "x");
    };
    auto transport = std::make_shared<FnDownloaderFactory>(fake);
    rig.transport = transport;
    ZmpSpec spec =
        withScript("var h, d: AnsiString; begin "
                   "ResultURL := 'c' + inttostr(Downloader.DoHttpRequest('http://d/', '', '', h, d)); "
                   "end.");
    spec.params = "IsUseDownloaderInScript=1\r\n";
    rig.build(rig.tempZmp(spec));

    const auto first = rig.subsystem->getRequest(geo::PointI{3, 7}, 5, nullptr);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->url, "c200");
    EXPECT_EQ(transport->cookies(), (std::vector<bool>{false}));

    rig.downloaderConfig->setAllowUseCookie(true);
    const auto second = rig.subsystem->getRequest(geo::PointI{4, 8}, 5, nullptr);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->url, "c200");
    EXPECT_EQ(transport->cookies(), (std::vector<bool>{false, true}));
}

// Region downloads push many tasks at once: they all finish once, in one burst through the 256-place queue.
// Three connections: the workers save at once and read the wall clock together
// (QDateTime::currentDateTime() in storage::tileDateNow()); the glibc time-zone report that this raised under
// ThreadSanitizer is suppressed in tools/tsan_suppressions.txt (T-0195).
TEST(TileDownloadSubsystem, ManyTasksAtOnceAllFinishExactlyOnce) {
    SubsystemRig rig;
    ASSERT_TRUE(rig.serverStarted());
    constexpr int kTiles = 12;
    for (int x = 0; x < kTiles; ++x) {
        rig.reply("/5/" + QByteArray::number(x) + "/7.png", "image/png", kPng + QByteArray::number(x));
    }
    rig.build(rig.tempZmp(withParams("MaxConnectToServerCount=3\r\n")));
    Finish finish;
    for (int x = 0; x < kTiles; ++x) {
        (void)rig.download(finish, x, 7);
    }
    ASSERT_TRUE(finish.waitFor(kTiles));
    rig.subsystem.reset();
    EXPECT_EQ(finish.total(), kTiles);
    int ok = 0;
    for (int i = 0; i < kTiles; ++i) {
        ok += kindOf(finish.result(i)) == "Ok" ? 1 : 0;
    }
    EXPECT_EQ(ok, kTiles);
    EXPECT_EQ(rig.files().size(), kTiles);
    EXPECT_EQ(*rig.file(u"z5/7/4.png"_s), kPng + "4");
}

// T-0225 selection (spec map-source-json.md section 7): a source whose container offers GetUrlScript.js gets
// the JavaScript factory, through the whole subsystem; its URL comes from getUrl, its errors are those of
// getUrl.
TEST(TileDownloadSubsystem, JavaScriptSourceDownloadsATileThroughTheJsFactory) {
    cg::maptype::test::ensureGuiApp();
    SubsystemRig rig;
    ASSERT_TRUE(rig.serverStarted());
    rig.reply("/5/3/7.png", "image/png", kPng);
    rig.build(rig.tempSourceJson("function getUrl(tile, ctx) { return template(ctx.urlBase); }"));
    ASSERT_TRUE(rig.subsystem->state()->getStatic()->enabled);
    Finish finish;
    (void)rig.download(finish);
    ASSERT_TRUE(finish.waitFor(1));
    rig.subsystem.reset();
    EXPECT_EQ(finish.total(), 1);
    EXPECT_EQ(kindOf(finish.result()), "Ok");
    EXPECT_EQ(rig.paths(), (QList<QByteArray>{"/5/3/7.png"}));
    EXPECT_EQ(rig.files(), QStringList{u"z5/7/3.png"_s});
}

TEST(TileDownloadSubsystem, JavaScriptSourceThatThrowsOrFailsItsCheckGivesTheScriptsText) {
    cg::maptype::test::ensureGuiApp();
    {
        SubsystemRig rig;
        ASSERT_TRUE(rig.serverStarted());
        rig.build(rig.tempSourceJson("function getUrl(tile, ctx) {\n  throw Error('no luck');\n}"));
        Finish finish;
        (void)rig.download(finish);
        ASSERT_TRUE(finish.waitFor(1));
        rig.subsystem.reset();
        EXPECT_EQ(kindOf(finish.result()), "ErrorBeforeBuild");
        EXPECT_EQ(textOf(finish.result()), u"no luck at line 2"_s);
        EXPECT_TRUE(rig.paths().isEmpty());
    }
    SubsystemRig rig;
    ASSERT_TRUE(rig.serverStarted());
    rig.build(rig.tempSourceJson("var notAFunction = 1;"));
    Finish first;
    (void)rig.download(first);
    ASSERT_TRUE(first.waitFor(1));
    const auto state = rig.subsystem->state()->getStatic();
    EXPECT_FALSE(state->enabled);
    EXPECT_EQ(state->disableReason, u"function getUrl(tile, ctx) is not defined"_s);
    EXPECT_TRUE(rig.paths().isEmpty());
}
