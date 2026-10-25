// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-010 (T-0079: the loop is build/send/save, u_TileDownloaderSimple.pas:152-274) --
// docs/specs/net.md §2.15, §7.11.
//
// Test-count ruling (tech-lead-rulings.md, T-0011B dispatch): the ticket's "17 cases" figure is
// not a quota; the cases below cover the loop (T-0079: build/send/save, two cancels) and the pacing.
//
// §7.11 mentions "an injectable clock"; RetryDownloader's constructor has no clock parameter
// (net.md :1161-1167, quoted verbatim by the ticket) so none was added here (see
// docs/handoffs/T-0011B.md). Timing tests instead use small, real millisecond-scale intervals with
// generous tolerances -- deterministic in the sense that matters (which thread does what, and
// std::thread::join()/QElapsedTimer bounds, never a sleep-then-poll race), not in the sense of
// avoiding all wall-clock time, which the fixed API does not allow.
#include "cg/net/download_result_factory.h"
#include "cg/net/inet_config.h"
#include "cg/net/retry_downloader.h"

#include <QElapsedTimer>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

using cg::net::BuildFn;
using cg::net::DownloadRequest;
using cg::net::DownloadResult;
using cg::net::IDownloader;
using cg::net::InetSettings;
using cg::net::LastResponseInfo;
using cg::net::ResultKind;
using cg::net::RetryDownloader;
using cg::net::RetryEnd;
using cg::net::RetryOutcome;

namespace {

DownloadRequest makeRequest() {
    DownloadRequest req;
    req.url = "http://example.invalid/tile";
    req.inet = std::make_shared<const InetSettings>();
    return req;
}

// A scripted IDownloader double: returns each entry of `results` in order (repeating the last one
// past the end), counts how many times it was called and keeps every request it was sent.
class FakeDownloader final : public IDownloader {
public:
    explicit FakeDownloader(std::vector<DownloadResult> results) : results_(std::move(results)) {}

    DownloadResult doRequest(const DownloadRequest& request, std::stop_token stop) override {
        if (onSend) {
            onSend();
        }
        if (stop.stop_requested()) { // like the transport: the hard cancel aborts the transfer
            ++callCount_;
            return cg::net::result::canceled();
        }
        const std::size_t index = std::min(static_cast<std::size_t>(callCount_), results_.size() - 1);
        ++callCount_;
        urls_.push_back(request.url);
        return results_[index];
    }

    std::function<void()> onSend; // runs inside the transfer
    int callCount_ = 0;
    std::vector<QByteArray> urls_;

private:
    std::vector<DownloadResult> results_;
};

// The job side of the loop: a counting builder (each build yields a distinct url and notes the
// response head the slot held at that moment) and a saver that records what it was handed.
struct Job {
    LastResponseInfo info; // the slot's state
    int builds = 0;
    int saves = 0;
    std::vector<QByteArray> headsSeenByBuild;
    std::vector<ResultKind> saved;
    bool buildReturnsNothing = false;

    RetryOutcome run(RetryDownloader& retry, std::stop_token soft = {}, std::stop_token hard = {}) {
        return retry.run(
            [this]() -> std::optional<DownloadRequest> {
                ++builds;
                headsSeenByBuild.push_back(info.responseHead());
                if (buildReturnsNothing) {
                    return std::nullopt;
                }
                DownloadRequest req = makeRequest();
                req.url += "/" + QByteArray::number(builds);
                return req;
            },
            info,
            [this](const DownloadResult& r) {
                ++saves;
                saved.push_back(r.kind);
            },
            soft, hard);
    }
};

} // namespace

// Banned has isServerExists() == true (download_result.h), so "until (!wasConnectError) or
// (count >= tryCount)" is satisfied after one attempt -- a ban never retries or backs off.
TEST(RetryDownloaderTest, DoesNotRetryOnBanned) {
    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::banned(200, {})});
    RetryDownloader retry(send, /*tryCount=*/5, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/0);
    Job job;

    const RetryOutcome outcome = job.run(retry);

    EXPECT_EQ(outcome.end, RetryEnd::Sent);
    EXPECT_EQ(outcome.result.kind, ResultKind::Banned);
    EXPECT_EQ(send->callCount_, 1);
    EXPECT_EQ(job.builds, 1);
}

// :258 -- keeps going while isServerExists() is false, stops at tryCount (or earlier on success).
TEST(RetryDownloaderTest, RetriesOnConnectErrorUpToTryCount) {
    {
        auto send = std::make_shared<FakeDownloader>(
            std::vector{cg::net::result::noConnectToServer(0), cg::net::result::ok(200, {}, {}, {})});
        RetryDownloader retry(send, /*tryCount=*/3, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/0);
        Job job;

        const RetryOutcome outcome = job.run(retry);

        EXPECT_EQ(outcome.result.kind, ResultKind::Ok);
        EXPECT_EQ(send->callCount_, 2);
        EXPECT_EQ(job.saved,
                  (std::vector{ResultKind::NoConnectToServer, ResultKind::Ok})); // every attempt saved
    }
    {
        auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::noConnectToServer(0)});
        RetryDownloader retry(send, /*tryCount=*/2, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/0);
        Job job;

        const RetryOutcome outcome = job.run(retry);

        EXPECT_EQ(outcome.result.kind, ResultKind::NoConnectToServer); // tryCount caps it
        EXPECT_EQ(send->callCount_, 2);
    }
}

// :183-190 -- the request is rebuilt on EVERY attempt (the script runs again), and the rebuilt
// request, not the first one, is what gets sent.
TEST(RetryDownloaderTest, RetriedRequestIsBuiltTwice) {
    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::noConnectToServer(0)});
    RetryDownloader retry(send, /*tryCount=*/2, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/0);
    Job job;

    (void)job.run(retry);

    EXPECT_EQ(job.builds, 2);
    ASSERT_EQ(send->urls_.size(), 2U);
    EXPECT_EQ(send->urls_[0], "http://example.invalid/tile/1");
    EXPECT_EQ(send->urls_[1], "http://example.invalid/tile/2");
}

// :226-230 -- a server answer stores its raw head in the slot's LastResponseInfo BEFORE the next
// build and before save; a connect error does not touch it. So a later build (a retry after a
// connect error, or the next job on the slot) sees the last answered head.
TEST(RetryDownloaderTest, NextBuildSeesTheStoredResponseHead) {
    auto send = std::make_shared<FakeDownloader>(
        std::vector{cg::net::result::ok(200, "H1", {}, {}), cg::net::result::noConnectToServer(0),
                    cg::net::result::noConnectToServer(0), cg::net::result::banned(200, "H2")});
    RetryDownloader retry(send, /*tryCount=*/3, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/0);
    Job job;

    (void)job.run(retry); // build #1 sees nothing; Ok(H1) -> stored after the send
    (void)job.run(retry); // builds #2, #3: connect errors keep H1; build #4 still sees H1, then Banned(H2)

    EXPECT_EQ(job.headsSeenByBuild, (std::vector<QByteArray>{"", "H1", "H1", "H1"}));
    EXPECT_EQ(job.info.responseHead(), QByteArray("H2"));
}

// :226-230 again, the other direction: the head is already stored when `save` runs, and a Banned
// answer (server exists, hasServerRespond) replaces it.
TEST(RetryDownloaderTest, HeadIsStoredBeforeSave) {
    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::banned(200, "H2")});
    RetryDownloader retry(send, /*tryCount=*/2, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/0);
    LastResponseInfo info;
    QByteArray seenBySave;

    const RetryOutcome outcome = retry.run([] { return std::optional<DownloadRequest>(makeRequest()); }, info,
                                           [&](const DownloadResult&) { seenBySave = info.responseHead(); },
                                           std::stop_token{}, std::stop_token{});

    EXPECT_EQ(outcome.end, RetryEnd::Sent);
    EXPECT_EQ(seenBySave, QByteArray("H2"));
}

// :201-207 -- build returned nothing: the job ends, nothing is sent, nothing is saved.
TEST(RetryDownloaderTest, BuildReturningNothingEndsWithoutSending) {
    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::ok(200, {}, {}, {})});
    RetryDownloader retry(send, /*tryCount=*/3, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/0);
    Job job;
    job.buildReturnsNothing = true;

    const RetryOutcome outcome = job.run(retry);

    EXPECT_EQ(outcome.end, RetryEnd::NoRequest);
    EXPECT_EQ(job.builds, 1);
    EXPECT_EQ(send->callCount_, 0);
    EXPECT_EQ(job.saves, 0);
}

// :174-181 and :208-211 -- the soft cancel is checked before build and again between build and send.
TEST(RetryDownloaderTest, CancelBeforeBuildAndBetweenBuildAndSend) {
    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::ok(200, {}, {}, {})});
    RetryDownloader retry(send, /*tryCount=*/3, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/0);
    {
        Job job;
        std::stop_source source;
        source.request_stop();
        const RetryOutcome outcome = job.run(retry, source.get_token());
        EXPECT_EQ(outcome.end, RetryEnd::CanceledBeforeBuild);
        EXPECT_EQ(job.builds, 0);
    }
    {
        std::stop_source source;
        LastResponseInfo info;
        int builds = 0;
        const RetryOutcome outcome = retry.run(
            [&] {
                ++builds;
                source.request_stop(); // the cancel arrives while the request is being built
                return std::optional<DownloadRequest>(makeRequest());
            },
            info, [](const DownloadResult&) {}, source.get_token(), std::stop_token{});
        EXPECT_EQ(outcome.end, RetryEnd::CanceledAfterBuild);
        EXPECT_EQ(builds, 1);
    }
    EXPECT_EQ(send->callCount_, 0);
}

// Cancel between attempts (single-threaded: the cancel is raised from inside `save`): attempt 1 is a connect
// error, the cancel arrives during `save`; the loop must not build or send a second time. :178-181 after the
// first iteration.
TEST(RetryDownloaderTest, CancelBetweenAttempts) {
    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::noConnectToServer(0)});
    RetryDownloader retry(send, /*tryCount=*/5, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/0);
    Job job;
    std::stop_source source;
    int builds = 0;

    const RetryOutcome outcome = retry.run(
        [&] {
            ++builds;
            return std::optional<DownloadRequest>(makeRequest());
        },
        job.info, [&](const DownloadResult&) { source.request_stop(); }, source.get_token(),
        std::stop_token{});

    EXPECT_EQ(outcome.end, RetryEnd::CanceledBeforeBuild);
    EXPECT_EQ(builds, 1);
    EXPECT_EQ(send->callCount_, 1);
}

// u_TileDownloaderSimple.pas:179/:209 vs :188/:216 -- the SOFT cancel does not touch the transfer
// in flight: it is finished and SAVED, the loop then stops at its next soft check (here, before the
// retry's build). The attempt is a connect error, so without the cancel the loop would go on.
TEST(RetryDownloaderTest, SoftCancelDuringTransferStillSavesThenStops) {
    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::noConnectToServer(0)});
    RetryDownloader retry(send, /*tryCount=*/5, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/0);
    Job job;
    std::stop_source soft;
    send->onSend = [&] { soft.request_stop(); };

    const RetryOutcome outcome = job.run(retry, soft.get_token());

    EXPECT_EQ(outcome.end, RetryEnd::CanceledBeforeBuild);
    EXPECT_EQ(send->callCount_, 1);
    EXPECT_EQ(job.saves, 1); // the in-flight tile was saved
    EXPECT_EQ(job.builds, 1);
}

// The HARD cancel aborts the transfer: the Canceled answer is returned and is not saved.
TEST(RetryDownloaderTest, HardCancelAbortsTheTransfer) {
    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::ok(200, {}, {}, {})});
    RetryDownloader retry(send, /*tryCount=*/5, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/0);
    Job job;
    std::stop_source hard;
    send->onSend = [&] { hard.request_stop(); };

    const RetryOutcome outcome = job.run(retry, {}, hard.get_token());

    EXPECT_EQ(outcome.end, RetryEnd::Sent);
    EXPECT_EQ(outcome.result.kind, ResultKind::Canceled);
    EXPECT_EQ(job.saves, 0);
    EXPECT_EQ(job.builds, 1);
}

// A Canceled answer from the transport is returned as is and is NOT saved (:222-225).
TEST(RetryDownloaderTest, CanceledResultIsNotSaved) {
    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::canceled()});
    RetryDownloader retry(send, /*tryCount=*/3, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/0);
    Job job;

    const RetryOutcome outcome = job.run(retry);

    EXPECT_EQ(outcome.end, RetryEnd::Sent);
    EXPECT_EQ(outcome.result.kind, ResultKind::Canceled);
    EXPECT_EQ(job.saves, 0);
    EXPECT_EQ(send->callCount_, 1);
}

// u_TileDownloaderSimple.pas:293-318: sleep only the remainder since the last download.
// T-0093 part 2: seconds-scale and one-sided around a MEASURED origin instead of 140 ms <= x < 280 ms
// around a nominal 200 ms. The second call must wake at (stamp + interval), where the stamp was taken
// during the first call, i.e. after `startedAt`. So, measured from `startedAt`:
//   * lower bound: finished >= interval (a call that does not sleep, or sleeps too little, ends near
//     the head start, ~2.5 s, far below the 4 s interval);
//   * upper bound: finished < interval + headStart/2 (a call that sleeps the full interval after the
//     head start ends near 6.5 s; the correct remainder ends near 4 s). Tolerance 1.25 s, one-sided
//     scheduling delay only; a head start that overshoots does not move the wake-up time.
TEST(RetryDownloaderTest, SleepsOnlyTheRemainderSinceLastDownload) {
    static constexpr int kWaitIntervalMs = 4000;
    static constexpr int kHeadStartMs = 2500;

    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::ok(200, {}, {}, {})});
    RetryDownloader retry(send, /*tryCount=*/1, /*sleepOnResetMs=*/0, kWaitIntervalMs);
    Job job;

    QElapsedTimer startedAt;
    startedAt.start();

    // First call: lastDownloadTime_ reads as "ages ago", no sleep; it only stamps the time.
    ASSERT_EQ(job.run(retry).result.kind, ResultKind::Ok);
    ASSERT_LT(startedAt.elapsed(), kHeadStartMs / 2) << "first call must not sleep";

    std::this_thread::sleep_for(std::chrono::milliseconds(kHeadStartMs));

    ASSERT_EQ(job.run(retry).result.kind, ResultKind::Ok);
    const qint64 finishedMs = startedAt.elapsed();

    EXPECT_GE(finishedMs, kWaitIntervalMs);
    EXPECT_LT(finishedMs, kWaitIntervalMs + kHeadStartMs / 2);
}

// The sleep must wake on cancellation instead of waiting out the interval; nothing is built.
TEST(RetryDownloaderTest, SleepIsInterruptedByCancel) {
    static constexpr int kWaitIntervalMs = 5000; // would time out the test if not interrupted

    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::ok(200, {}, {}, {})});
    RetryDownloader retry(send, /*tryCount=*/1, /*sleepOnResetMs=*/0, kWaitIntervalMs);
    Job job;
    ASSERT_EQ(job.run(retry).result.kind, ResultKind::Ok); // stamps lastDownloadTime_
    ASSERT_EQ(job.builds, 1);

    std::stop_source source;
    std::atomic<bool> started{false};
    RetryOutcome outcome;
    std::thread worker([&] {
        started = true;
        outcome = job.run(retry, source.get_token());
    });

    while (!started) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // let the worker reach the sleep
    QElapsedTimer timer;
    timer.start();
    source.request_stop();
    worker.join();

    EXPECT_LT(timer.elapsed(), 1000); // well under the 5s interval it would otherwise wait out
    EXPECT_EQ(outcome.end, RetryEnd::CanceledBeforeBuild);
    EXPECT_EQ(job.builds, 1); // the second build never happened
    EXPECT_EQ(send->callCount_, 1);
}

// The hard cancel (app closing) also wakes the pacing sleep, :276-279; the soft token stays quiet.
TEST(RetryDownloaderTest, SleepIsInterruptedByHardCancel) {
    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::ok(200, {}, {}, {})});
    RetryDownloader retry(send, /*tryCount=*/1, /*sleepOnResetMs=*/0, /*waitIntervalMs=*/5000);
    Job job;
    ASSERT_EQ(job.run(retry).result.kind, ResultKind::Ok); // stamps lastDownloadTime_

    std::stop_source hard;
    std::atomic<bool> started{false};
    RetryOutcome outcome;
    std::thread worker([&] {
        started = true;
        outcome = job.run(retry, {}, hard.get_token());
    });
    while (!started) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    QElapsedTimer timer;
    timer.start();
    hard.request_stop();
    worker.join();

    EXPECT_LT(timer.elapsed(), 1000);
    // Legacy goes on after the sleep (only the soft cancel is checked at :179): the build runs,
    // the transfer is aborted by the hard cancel.
    EXPECT_EQ(outcome.end, RetryEnd::Sent);
    EXPECT_EQ(outcome.result.kind, ResultKind::Canceled);
}

// T-0329: why a failed tile takes ~35 s with the legacy defaults (TimeOut 40000, DownloadTryCount 2,
// SleepOnResetConnection 30000, u_InetConfig.pas:99-101): after a connect error the loop pauses
// SleepOnResetConnection before EACH further try -- tryCount-1 pauses in all -- and a try that
// answers does not pause. Scaled down: tryCount 3, pause 300 ms.
TEST(RetryDownloaderTest, ConnectErrorPausesSleepOnResetBeforeEachFurtherTryOnly) {
    static constexpr int kSleepMs = 300;
    auto send = std::make_shared<FakeDownloader>(std::vector{cg::net::result::noConnectToServer(0)});
    RetryDownloader retry(send, /*tryCount=*/3, kSleepMs, /*waitIntervalMs=*/0);
    Job job;
    QElapsedTimer timer;
    timer.start();

    const RetryOutcome outcome = job.run(retry);

    const qint64 elapsedMs = timer.elapsed();
    EXPECT_EQ(outcome.result.kind, ResultKind::NoConnectToServer);
    EXPECT_EQ(send->callCount_, 3);
    EXPECT_GE(elapsedMs, 2 * kSleepMs - 30); // two pauses, none before the first try
    EXPECT_LT(elapsedMs, 3 * kSleepMs);      // and not one per try

    // An answering server: no pause at all.
    auto ok = std::make_shared<FakeDownloader>(std::vector{cg::net::result::ok(200, {}, {}, {})});
    RetryDownloader quick(ok, /*tryCount=*/3, kSleepMs, /*waitIntervalMs=*/0);
    Job okJob;
    timer.restart();
    EXPECT_EQ(okJob.run(quick).result.kind, ResultKind::Ok);
    EXPECT_LT(timer.elapsed(), kSleepMs);
}

namespace {
DownloadResult resetByPeer() {
    DownloadResult r;
    r.kind = ResultKind::UnknownError;
    return r;
}
} // namespace

// T-0329: any other connection error keeps the legacy sleep (here 300 ms between the two attempts).
TEST(RetryDownloaderTest, OtherConnectionErrorKeepsTheResetConnectionSleep) {
    auto send = std::make_shared<FakeDownloader>(std::vector{resetByPeer()});
    RetryDownloader retry(send, /*tryCount=*/2, /*sleepOnResetMs=*/300, /*waitIntervalMs=*/0);
    Job job;
    QElapsedTimer timer;
    timer.start();
    (void)job.run(retry);
    EXPECT_EQ(send->callCount_, 2);
    EXPECT_GE(timer.elapsed(), 250);
}
