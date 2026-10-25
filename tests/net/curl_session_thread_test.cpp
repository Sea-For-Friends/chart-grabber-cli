// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// T-0144: CurlSession belongs to ONE THREAD AT A TIME, not to one thread for life -- docs/specs/net.md
// §2.8 rule 2 / §3 as corrected. Legacy rule: mutual exclusion, not affinity (u_DownloaderHttpWithTTL.pas
// :102-138 builds, uses and frees the TCurlHttpClient under FCS by whichever thread calls; the handle itself:
// u_CurlHttpClient.pas:258-262 Destroy -> :292-298 CurlClose, :430 easy_init inside DoRequest).
#include "cg/core/content_type.h"
#include "cg/core/notifier.h"
#include "cg/net/curl_downloader.h"
#include "cg/net/curl_session.h"
#include "cg/net/curl_share.h"
#include "cg/net/download_request.h"
#include "cg/net/download_result.h"
#include "cg/net/downloader_ttl.h"
#include "cg/net/inet_config.h"

#include <QByteArray>
#include <QCoreApplication>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <latch>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>

#include "fixture_path.h"
#include "http_test_server.h"

using cg::net::CurlDownloader;
using cg::net::CurlDownloaderFactory;
using cg::net::CurlOptions;
using cg::net::CurlRequest;
using cg::net::CurlResponse;
using cg::net::CurlSession;
using cg::net::CurlShare;
using cg::net::DownloaderOptions;
using cg::net::DownloadRequest;
using cg::net::DownloadResult;
using cg::net::IDownloaderFactory;
using cg::net::IDownloaderPtr;
using cg::net::InetSettings;
using cg::net::Method;
using cg::net::ResultKind;
using cg::net::TtlDownloader;
using cg::net::test::HttpTestServer;

namespace {

// Same rule as curl_downloader_test.cpp: set before anything can make the first call.
struct ForceNoExternalNetwork {
    ForceNoExternalNetwork() {
        qputenv("CG_NET_NO_EXTERNAL_NETWORK", "1");
        // The death tests match Q_ASSERT_X text on the child's stderr; without this Qt logs to journald
        // when stderr is a pipe (binary run outside tools/gate_env.sh). The threadsafe child re-runs this.
        qputenv("QT_LOGGING_TO_CONSOLE", "1");
    }
};
const ForceNoExternalNetwork kForceNoExternalNetwork;

DownloadRequest makeRequest(const HttpTestServer& server, const QByteArray& path) {
    DownloadRequest req;
    req.url = server.url(path);
    req.method = Method::Get;
    auto inet = std::make_shared<InetSettings>();
    inet->timeoutMs = 10000;
    req.inet = std::move(inet);
    return req;
}

// Runs `fn` on a fresh thread, started through a latch, and joins it.
void onThread(const std::function<void()>& fn) {
    std::latch go(1);
    std::thread t([&] {
        go.wait();
        fn();
    });
    go.count_down();
    t.join();
}

// Counts builds and delegates to the REAL factory.
class CountingCurlFactory final : public IDownloaderFactory {
public:
    IDownloaderPtr build(const DownloaderOptions& options) override {
        ++builds;
        return inner_.build(options);
    }
    std::atomic<int> builds{0};

private:
    CurlDownloaderFactory inner_{cg::core::ContentTypeRegistry::standard(), nullptr};
};

class CurlSessionThreadTest : public ::testing::Test {
protected:
    void SetUp() override {
        cgEnsureCoreApplication();
        ASSERT_TRUE(server_.start());
    }
    HttpTestServer server_;
};

HttpTestServer::Reply body(const QByteArray& text) {
    HttpTestServer::Reply reply;
    reply.body = text;
    return reply;
}

} // namespace

// Proof 1: built and used on thread A (joined), used again on thread B (joined), destroyed on this thread.
TEST_F(CurlSessionThreadTest, DownloaderBuiltOnOneThreadUsedOnAnotherDestroyedOnThird) {
    server_.addReply("/first", body("alpha"));
    server_.addReply("/second", body("bravo-Город"));

    std::unique_ptr<CurlDownloader> downloader;
    DownloadResult first;
    DownloadResult second;
    onThread([&] {
        downloader = std::make_unique<CurlDownloader>(
            DownloaderOptions{}, cg::core::ContentTypeRegistry::standard(), nullptr, nullptr);
        first = downloader->doRequest(makeRequest(server_, "/first"), std::stop_token{});
    });
    onThread([&] { second = downloader->doRequest(makeRequest(server_, "/second"), std::stop_token{}); });
    downloader.reset();

    EXPECT_EQ(first.kind, ResultKind::Ok);
    EXPECT_EQ(first.data, QByteArrayLiteral("alpha"));
    EXPECT_EQ(second.kind, ResultKind::Ok);
    EXPECT_EQ(second.data, QByteArray("bravo-Город"));
}

// Proof 2: TtlDownloader over the real factory. Request on thread A, the clock moved past the TTL, the tick
// delivered from thread B drops (destroys) the handle, a request on thread C rebuilds it and succeeds.
TEST_F(CurlSessionThreadTest, TtlTrimFromAnotherThreadThenRebuildOnAThirdThread) {
    server_.addReply("/ttl", body("one"));
    server_.addReply("/ttl", body("two"));

    cg::core::Notifier gc;
    std::atomic<std::int64_t> ms{0};
    const auto factory = std::make_shared<CountingCurlFactory>();
    const auto ttl = std::make_shared<TtlDownloader>(factory, DownloaderOptions{}, [&ms] {
        return std::chrono::steady_clock::time_point(std::chrono::milliseconds(ms.load()));
    });
    ttl->subscribeGcTick(gc);

    DownloadResult first;
    DownloadResult second;
    onThread([&] { first = ttl->doRequest(makeRequest(server_, "/ttl"), std::stop_token{}); });
    ms += cg::net::kHttpClientTtl.count();
    onThread([&] { gc.notify(); });
    EXPECT_EQ(factory->builds.load(), 1);
    onThread([&] { second = ttl->doRequest(makeRequest(server_, "/ttl"), std::stop_token{}); });

    EXPECT_EQ(factory->builds.load(), 2); // the tick really dropped it
    EXPECT_EQ(first.kind, ResultKind::Ok);
    EXPECT_EQ(first.data, QByteArrayLiteral("one"));
    EXPECT_EQ(second.kind, ResultKind::Ok);
    EXPECT_EQ(second.data, QByteArrayLiteral("two"));
}

// Proof 3: two threads inside the session at once are still caught (debug builds only: the marker does not
// exist in a release build). The first thread is held INSIDE perform() by the server's delayed response; it
// is known to be inside because its progress callback (which runs on the performing thread, inside perform())
// has fired. Death tests re-execute the binary (threadsafe style), so the server and the threads are built in
// the child.
#ifndef NDEBUG
namespace {

enum class SecondUse { Perform, AttachShare, Destroy };

void runConcurrentUse(HttpTestServer& server, SecondUse second) {
    HttpTestServer::Reply slow = body("slow");
    slow.delayMs = 3000;
    server.addReply("/hold", slow);

    std::promise<void> inside;
    std::future<void> insideFuture = inside.get_future();
    std::atomic<bool> signalled{false};
    auto session = std::make_unique<CurlSession>(QByteArray(), [&](qint64, qint64) {
        if (!signalled.exchange(true)) {
            inside.set_value();
        }
    });
    CurlShare share;

    std::thread holder([&] {
        CurlRequest req;
        req.url = server.url("/hold");
        CurlOptions options;
        options.timeoutMs = 10000;
        req.options = &options;
        CurlResponse response;
        (void)session->perform(req, response);
    });
    // A bounded wait: if the first transfer never reports progress the child exits normally and the death
    // test fails, instead of hanging.
    if (insideFuture.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
        (void)session.release();
        holder.detach();
        return;
    }
    switch (second) {
    case SecondUse::Perform: {
        CurlRequest req;
        req.url = server.url("/hold");
        CurlResponse response;
        (void)session->perform(req, response);
        break;
    }
    case SecondUse::AttachShare:
        session->attachShare(share);
        break;
    case SecondUse::Destroy:
        session.reset();
        break;
    }
    // Reached only when the second use was NOT caught. The session is leaked on purpose: destroying it here
    // would trip the destructor's own check and mask a missing check in the call under test.
    (void)session.release();
    holder.detach();
}

// Restores the global death-test style so it does not leak to later tests of this binary.
struct ScopedThreadsafeDeathStyle {
    std::string saved = GTEST_FLAG_GET(death_test_style);
    ScopedThreadsafeDeathStyle() { GTEST_FLAG_SET(death_test_style, "threadsafe"); }
    ~ScopedThreadsafeDeathStyle() { GTEST_FLAG_SET(death_test_style, saved); }
};

} // namespace

TEST_F(CurlSessionThreadTest, SecondThreadInsidePerformIsCaught) {
    const ScopedThreadsafeDeathStyle style;
    EXPECT_DEBUG_DEATH(runConcurrentUse(server_, SecondUse::Perform), "CurlSession in use");
}

TEST_F(CurlSessionThreadTest, AttachShareWhileAnotherThreadIsInsideIsCaught) {
    const ScopedThreadsafeDeathStyle style;
    EXPECT_DEBUG_DEATH(runConcurrentUse(server_, SecondUse::AttachShare), "CurlSession in use");
}

TEST_F(CurlSessionThreadTest, DestroyWhileAnotherThreadIsInsideIsCaught) {
    const ScopedThreadsafeDeathStyle style;
    EXPECT_DEBUG_DEATH(runConcurrentUse(server_, SecondUse::Destroy), "CurlSession in use");
}
#endif
