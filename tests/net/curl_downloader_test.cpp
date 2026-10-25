// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-009 -- docs/specs/net.md §2.8-§2.10, §7.0, §7.10.
#include "cg/core/content_type.h"
#include "cg/net/curl_downloader.h"
#include "cg/net/download_checker.h"
#include "cg/net/download_request.h"
#include "cg/net/download_result.h"
#include "cg/net/inet_config.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QPair>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <zlib.h>

#include "fixture_path.h"
#include "http_test_server.h"

using cg::net::CurlDownloader;
using cg::net::CurlProxy;
using cg::net::DownloaderOptions;
using cg::net::DownloadRequest;
using cg::net::DownloadResult;
using cg::net::InetSettings;
using cg::net::IProxyResolver;
using cg::net::Method;
using cg::net::ProxySettings;
using cg::net::ProxyType;
using cg::net::ResultKind;
using cg::net::test::HttpTestServer;

namespace {

// T-0010B rework finding 4/5: a minimal IProxyResolver double for the 407-retry-once path.
// resolve() is never exercised here -- the test drives the request through
// ProxySettings::useProxy, which builds CurlProxy directly (curl_downloader.cpp) without
// consulting the resolver -- only credentialsFor() (the 407 credential prompt) is a real call.
class FakeProxyResolver final : public IProxyResolver {
public:
    bool resolve(QByteArrayView /*url*/, CurlProxy& /*out*/) override { return false; }
    void setCredentialPrompt(CredentialFn /*fn*/) override {}
    bool credentialsFor(QByteArrayView /*proxyAddress*/, const QString& description, QByteArray& user,
                        QByteArray& pass) override {
        lastDescription = description;
        ++calls;
        user = grantedUser;
        pass = "proxypass";
        return grant;
    }

    QString lastDescription;
    int calls = 0;
    bool grant = true;                    // false: the credential prompt is cancelled
    QByteArray grantedUser = "proxyuser"; // empty: accepted with an empty name
};

// docs/specs/net.md §7.0: no test may reach the real network. CurlDownloader reads
// CG_NET_NO_EXTERNAL_NETWORK once, lazily, on its first call -- set it before anything in this
// binary can make that first call (a global's constructor, which runs before main()/RUN_ALL_TESTS).
struct ForceNoExternalNetwork {
    ForceNoExternalNetwork() { qputenv("CG_NET_NO_EXTERNAL_NETWORK", "1"); }
};
const ForceNoExternalNetwork kForceNoExternalNetwork;

// QList<QPair<QByteArray,QByteArray>>::append({"a","b"}) is ambiguous in Qt6 (QPair == std::pair,
// and the braced list matches more than one append() overload); spell the pair out explicitly.
QPair<QByteArray, QByteArray> hdr(QByteArray name, QByteArray value) {
    return {std::move(name), std::move(value)};
}

QByteArray gzipCompress(const QByteArray& input) {
    z_stream strm{};
    EXPECT_EQ(deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY), Z_OK);
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    strm.avail_in = static_cast<uInt>(input.size());
    QByteArray out(static_cast<qsizetype>(deflateBound(&strm, static_cast<uLong>(input.size()))),
                   Qt::Uninitialized);
    strm.next_out = reinterpret_cast<Bytef*>(out.data());
    strm.avail_out = static_cast<uInt>(out.size());
    EXPECT_EQ(deflate(&strm, Z_FINISH), Z_STREAM_END);
    out.resize(out.size() - static_cast<qsizetype>(strm.avail_out));
    deflateEnd(&strm);
    return out;
}

// A checker that throws from one stage (the other passes), for the guarded-region proof.
class ThrowingChecker final : public cg::net::IDownloadChecker {
public:
    ThrowingChecker(bool throwBefore, bool throwAfter) : before_(throwBefore), after_(throwAfter) {}
    [[nodiscard]] std::optional<DownloadResult>
    beforeRequest(const DownloadRequest& /*request*/) const override {
        if (before_) {
            throw std::runtime_error("before-stage boom Город");
        }
        return std::nullopt;
    }
    [[nodiscard]] std::optional<DownloadResult>
    afterReceiveData(const DownloadRequest& /*request*/, const QByteArray& /*body*/, quint32& /*statusCode*/,
                     QByteArray& /*contentType*/, QByteArray& /*responseHead*/) const override {
        if (after_) {
            throw std::runtime_error("after-stage boom город");
        }
        return std::nullopt;
    }

private:
    bool before_;
    bool after_;
};

} // namespace

class CurlDownloaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        cgEnsureCoreApplication();
        ASSERT_TRUE(server_.start());
    }

    static std::shared_ptr<InetSettings> makeInet(quint32 timeoutMs = 5000) {
        auto inet = std::make_shared<InetSettings>();
        inet->timeoutMs = timeoutMs;
        return inet;
    }

    [[nodiscard]] DownloadRequest makeRequest(const QByteArray& path, Method method = Method::Get,
                                              QByteArray headers = {},
                                              std::shared_ptr<const InetSettings> inet = nullptr) const {
        DownloadRequest req;
        req.url = server_.url(path);
        req.method = method;
        req.requestHeader = std::move(headers);
        req.inet = inet ? std::move(inet) : makeInet();
        return req;
    }

    static std::unique_ptr<CurlDownloader> makeDownloader(DownloaderOptions options = {}) {
        return std::make_unique<CurlDownloader>(options, cg::core::ContentTypeRegistry::standard(), nullptr,
                                                nullptr);
    }

    HttpTestServer server_;
};

TEST_F(CurlDownloaderTest, Get200ReturnsOk) {
    HttpTestServer::Reply reply;
    reply.body = "hello";
    server_.addReply("/get", reply);

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(makeRequest("/get"), std::stop_token{});
    EXPECT_EQ(result.kind, ResultKind::Ok);
    EXPECT_EQ(result.statusCode, 200u);
    EXPECT_EQ(result.data, QByteArrayLiteral("hello"));
}

TEST_F(CurlDownloaderTest, RawHeaderBlobIncludesStatusLineAndTrailingBlankLine) {
    HttpTestServer::Reply reply;
    reply.body = "x";
    reply.headers.append(hdr("X-Test", "1"));
    server_.addReply("/hdr", reply);

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(makeRequest("/hdr"), std::stop_token{});
    ASSERT_EQ(result.kind, ResultKind::Ok);
    EXPECT_TRUE(result.rawResponseHeader.startsWith("HTTP/1.1 200"));
    EXPECT_TRUE(result.rawResponseHeader.endsWith("\r\n\r\n"));
}

TEST_F(CurlDownloaderTest, RawHeaderBlobKeepsOnlyLastResponseAfterRedirect) {
    HttpTestServer::Reply first;
    first.status = 302;
    first.reason = "Found";
    first.headers.append(hdr("Location", server_.url("/second")));
    first.headers.append(hdr("X-First-Hop-Only", "yes"));
    server_.addReply("/first", first);

    HttpTestServer::Reply second;
    second.body = "final";
    server_.addReply("/second", second);

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(makeRequest("/first"), std::stop_token{});
    ASSERT_EQ(result.kind, ResultKind::Ok);
    EXPECT_TRUE(result.rawResponseHeader.startsWith("HTTP/1.1 200"));
    EXPECT_FALSE(result.rawResponseHeader.contains("X-First-Hop-Only"));
    EXPECT_EQ(result.data, QByteArrayLiteral("final"));
}

TEST_F(CurlDownloaderTest, RedirectsDisabledGive302AsUnknownStatus) {
    HttpTestServer::Reply reply;
    reply.status = 302;
    reply.reason = "Found";
    reply.headers.append(hdr("Location", server_.url("/nope")));
    server_.addReply("/redir", reply);

    DownloaderOptions options;
    options.allowRedirect = false;
    const auto downloader = makeDownloader(options);
    const DownloadResult result = downloader->doRequest(makeRequest("/redir"), std::stop_token{});
    EXPECT_EQ(result.kind, ResultKind::LoadError);
    EXPECT_EQ(result.statusCode, 302u);
}

TEST_F(CurlDownloaderTest, MaxRedirsBoundsALoop) {
    HttpTestServer::Reply loop;
    loop.status = 302;
    loop.reason = "Found";
    loop.headers.append(hdr("Location", server_.url("/loop")));
    server_.addReply("/loop", loop);

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(makeRequest("/loop"), std::stop_token{});
    EXPECT_EQ(result.kind,
              ResultKind::UnknownError); // CURLE_TOO_MANY_REDIRECTS -> every curl failure is UnknownError
}

TEST_F(CurlDownloaderTest, HeadSendsNoBody) {
    HttpTestServer::Reply reply; // empty body -> Content-Length: 0, matches the 0 bytes HEAD delivers
    server_.addReply("/head", reply);

    const auto downloader = makeDownloader();
    const DownloadResult result =
        downloader->doRequest(makeRequest("/head", Method::Head), std::stop_token{});
    EXPECT_TRUE(result.data.isEmpty());
    const QList<QByteArray> log = server_.requestLog();
    ASSERT_FALSE(log.isEmpty());
    EXPECT_TRUE(log.last().startsWith("HEAD "));
}

TEST_F(CurlDownloaderTest, PostSendsBodyAndSuppressesExpect) {
    HttpTestServer::Reply reply;
    reply.body = "posted-ok";
    server_.addReply("/post", reply);

    DownloadRequest req = makeRequest("/post", Method::Post);
    req.postData = QByteArrayLiteral("payload=1&more=2");

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(req, std::stop_token{});
    ASSERT_EQ(result.kind, ResultKind::Ok);
    EXPECT_EQ(result.data, QByteArrayLiteral("posted-ok"));

    const QList<QByteArray> log = server_.requestLog();
    ASSERT_FALSE(log.isEmpty());
    EXPECT_TRUE(log.last().startsWith("POST "));
    EXPECT_FALSE(log.last().toUpper().contains("EXPECT:"));
}

TEST_F(CurlDownloaderTest, UserAgentInjectedWhenAbsent) {
    HttpTestServer::Reply reply;
    reply.body = "x";
    server_.addReply("/ua", reply);

    auto inet = makeInet();
    inet->userAgent = "MySpecialAgent/1.0";
    const DownloadRequest req = makeRequest("/ua", Method::Get, {}, inet);

    const auto downloader = makeDownloader();
    [[maybe_unused]] const DownloadResult result = downloader->doRequest(req, std::stop_token{});
    const QByteArray log = server_.requestLog().last();
    EXPECT_TRUE(log.toUpper().contains("USER-AGENT: MYSPECIALAGENT/1.0"));
}

TEST_F(CurlDownloaderTest, UserAgentFromRequestHeaderWins) {
    HttpTestServer::Reply reply;
    reply.body = "x";
    server_.addReply("/ua2", reply);

    auto inet = makeInet();
    inet->userAgent = "DefaultAgent";
    const DownloadRequest req =
        makeRequest("/ua2", Method::Get, QByteArrayLiteral("User-Agent: CustomAgent\r\n"), inet);

    const auto downloader = makeDownloader();
    [[maybe_unused]] const DownloadResult result = downloader->doRequest(req, std::stop_token{});
    const QByteArray log = server_.requestLog().last();
    EXPECT_TRUE(log.contains("CustomAgent"));
    EXPECT_FALSE(log.contains("DefaultAgent"));
}

TEST_F(CurlDownloaderTest, RequestHeaderRefererIsPassedThrough) {
    HttpTestServer::Reply reply;
    reply.body = "x";
    server_.addReply("/ref", reply);

    const DownloadRequest req =
        makeRequest("/ref", Method::Get, QByteArrayLiteral("Referer: http://example.test/\r\n"));
    const auto downloader = makeDownloader();
    [[maybe_unused]] const DownloadResult result = downloader->doRequest(req, std::stop_token{});
    const QByteArray log = server_.requestLog().last();
    EXPECT_TRUE(log.contains("Referer: http://example.test/"));
}

TEST_F(CurlDownloaderTest, AcceptEncodingAddedWhenAbsent) {
    HttpTestServer::Reply reply;
    reply.body = "x";
    server_.addReply("/ae", reply);

    const auto downloader = makeDownloader(); // DownloaderOptions::acceptEncoding defaults true
    [[maybe_unused]] const DownloadResult result =
        downloader->doRequest(makeRequest("/ae"), std::stop_token{});
    const QByteArray log = server_.requestLog().last().toUpper();
    EXPECT_TRUE(log.contains("ACCEPT-ENCODING:"));
}

TEST_F(CurlDownloaderTest, AcceptEncodingFilteredWhenPresent) {
    HttpTestServer::Reply reply;
    reply.body = "x";
    server_.addReply("/ae2", reply);

    const DownloadRequest req =
        makeRequest("/ae2", Method::Get, QByteArrayLiteral("Accept-Encoding: gzip, bogus\r\n"));
    const auto downloader = makeDownloader();
    [[maybe_unused]] const DownloadResult result = downloader->doRequest(req, std::stop_token{});
    const QByteArray log = server_.requestLog().last();
    EXPECT_TRUE(log.contains("gzip"));
    EXPECT_FALSE(log.toLower().contains("bogus"));
}

TEST_F(CurlDownloaderTest, GzipBodyIsDecodedAndHeaderRemoved) {
    const QByteArray plain = "hello compressed world";
    HttpTestServer::Reply reply;
    reply.body = gzipCompress(plain);
    reply.headers.append(hdr("Content-Encoding", "gzip"));
    server_.addReply("/gz", reply);

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(makeRequest("/gz"), std::stop_token{});
    ASSERT_EQ(result.kind, ResultKind::Ok);
    EXPECT_EQ(result.data, plain);
    EXPECT_FALSE(result.rawResponseHeader.toUpper().contains("CONTENT-ENCODING"));
}

TEST_F(CurlDownloaderTest, BadContentEncodingKeepsRawBodyAndHeader) {
    HttpTestServer::Reply reply;
    reply.body = "not-really-compressed";
    reply.headers.append(hdr("Content-Encoding", "bogus-encoding"));
    server_.addReply("/badenc", reply);

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(makeRequest("/badenc"), std::stop_token{});
    ASSERT_EQ(result.kind, ResultKind::BadContentEncoding);
    EXPECT_EQ(result.data, QByteArrayLiteral("not-really-compressed"));
    EXPECT_TRUE(result.rawResponseHeader.toUpper().contains("CONTENT-ENCODING"));
}

TEST_F(CurlDownloaderTest, ContentLengthMismatchGivesBadContentLength) {
    // A HEAD response legitimately carries the Content-Length the equivalent GET would have had,
    // while CURLOPT_NOBODY means curl delivers 0 actual body bytes -- a real, spec-compliant
    // mismatch, not a malformed one (declared > actual + connection close is a curl-level error
    // before onAfterResponse ever runs; declared < actual is silently truncated by curl to match --
    // neither of those reaches this app-level check with real HTTP framing).
    // Corollary this test actually pins down (review deviation (2)): EVERY HEAD against a server
    // that sends a Content-Length yields BadContentLength -- there is no HEAD exemption, in legacy
    // (u_DownloaderHttpBase.pas:145-152) or here, so this is not a bug to "fix" later.
    HttpTestServer::Reply reply;
    reply.body = "twelve-bytes"; // 12 bytes -> Content-Length: 12
    server_.addReply("/clmis", reply);

    const auto downloader = makeDownloader();
    const DownloadResult result =
        downloader->doRequest(makeRequest("/clmis", Method::Head), std::stop_token{});
    EXPECT_EQ(result.kind, ResultKind::BadContentLength);
}

TEST_F(CurlDownloaderTest, ContentLengthAbsentSkipsTheCheck) {
    HttpTestServer::Reply reply;
    reply.body = "hello";
    reply.omitContentLength = true;
    server_.addReply("/clabs", reply);

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(makeRequest("/clabs"), std::stop_token{});
    EXPECT_EQ(result.kind, ResultKind::Ok);
    EXPECT_EQ(result.data, QByteArrayLiteral("hello"));
}

TEST_F(CurlDownloaderTest, ContentLengthCheckedBeforeDecoding) {
    const QByteArray plain = "some plaintext payload, compressed for transport";
    HttpTestServer::Reply reply;
    reply.body = gzipCompress(plain); // Content-Length auto-computed from the COMPRESSED size
    reply.headers.append(hdr("Content-Encoding", "gzip"));
    server_.addReply("/clgz", reply);

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(makeRequest("/clgz"), std::stop_token{});
    ASSERT_EQ(result.kind, ResultKind::Ok);
    EXPECT_EQ(result.data, plain);
}

TEST_F(CurlDownloaderTest, ZeroSizeBodyGivesDataNotExistsZeroSize) {
    HttpTestServer::Reply reply; // empty body
    server_.addReply("/zero", reply);

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(makeRequest("/zero"), std::stop_token{});
    EXPECT_EQ(result.kind, ResultKind::DataNotExists);
    EXPECT_EQ(result.statusCode, 200u);
}

TEST_F(CurlDownloaderTest, Status404GivesDataNotExistsByStatusCode) {
    HttpTestServer::Reply reply;
    reply.status = 404;
    reply.reason = "Not Found";
    server_.addReply("/404", reply);

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(makeRequest("/404"), std::stop_token{});
    EXPECT_EQ(result.kind, ResultKind::DataNotExists);
    EXPECT_EQ(result.statusCode, 404u);
}

TEST_F(CurlDownloaderTest, Status500GivesLoadErrorByStatusCode) {
    HttpTestServer::Reply reply;
    reply.status = 500;
    reply.reason = "Internal Server Error";
    server_.addReply("/500", reply);

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(makeRequest("/500"), std::stop_token{});
    EXPECT_EQ(result.kind, ResultKind::LoadError);
    EXPECT_EQ(result.statusCode, 500u);
    EXPECT_TRUE(result.text.contains(QStringLiteral("Download error")));
}

TEST_F(CurlDownloaderTest, Status403GivesLoadErrorByUnknownStatusCode) {
    HttpTestServer::Reply reply;
    reply.status = 403;
    reply.reason = "Forbidden";
    server_.addReply("/403", reply);

    const auto downloader = makeDownloader();
    const DownloadResult result = downloader->doRequest(makeRequest("/403"), std::stop_token{});
    EXPECT_EQ(result.kind, ResultKind::LoadError);
    EXPECT_EQ(result.statusCode, 403u);
    EXPECT_TRUE(result.text.contains(QStringLiteral("Unknown status code")));
}

TEST_F(CurlDownloaderTest, DetectContentTypeRewritesHeader) {
    HttpTestServer::Reply reply;
    reply.body = QByteArray::fromHex("89504e470d0a1a0a") + "restofpngdata"; // PNG magic + padding
    reply.headers.append(hdr("Content-Type", "application/octet-stream"));
    server_.addReply("/detect", reply);

    DownloaderOptions options;
    options.tryDetectContentType = true;
    const auto downloader = makeDownloader(options);
    const DownloadResult result = downloader->doRequest(makeRequest("/detect"), std::stop_token{});
    ASSERT_EQ(result.kind, ResultKind::Ok);
    EXPECT_EQ(result.contentType, QByteArrayLiteral("image/png"));
    EXPECT_TRUE(result.rawResponseHeader.contains("image/png"));
    EXPECT_FALSE(result.rawResponseHeader.contains("application/octet-stream"));
}

// Deviation (legacy wins, protocol rule 5): the spec's test name USED TO imply ResultKind::Canceled
// (renamed by the T-0010B rework, deviation ruling 3), but legacy's own mid-transfer abort path
// (TCurlHttpClient.Disconnect -> FDoDisconnect, then DoRequest's "not VResult" branch,
// u_DownloaderHttpByCurl.pas:298-300) always builds TDownloadResultUnknownError via
// BuildLoadErrorByUnknownReason(..., 'Disconnected by user') -- there is no legacy path from a
// mid-flight cancel to a Canceled result; only the two pre-flight
// ACancelNotifier.IsOperationCanceled() checks (before the network call starts, :222-235) produce
// BuildCanceled. Ported faithfully; do NOT special-case this into result::canceled() -- see
// docs/handoffs/T-0010B.md and docs/handoffs/T-0010B-review.md deviation (3).
//
// No DownloaderOptions::onProgress here (T-0010B rework finding 2): this is the PRODUCTION
// configuration -- TtlDownloader always builds with a null onProgress (u_DownloaderHttpWithTTL.pas
// :114) -- and the abort heartbeat (CURLOPT_XFERINFOFUNCTION/PROGRESSFUNCTION) is now installed by
// curlSetup() unconditionally, so disconnect() must still cut this transfer short without one.
TEST_F(CurlDownloaderTest, CancelDuringTransferGivesUnknownErrorDisconnectedByUser) {
    HttpTestServer::Reply reply;
    reply.body = "slow";
    // T-0093: the server sleeps before writing anything. Long enough that "cut short" is
    // unambiguous even when libcurl's abort heartbeat (the progress callback, ticking about once a
    // second while the transfer idles) is late under CPU load: a transfer that was NOT cancelled
    // would take ~4 s, a cancelled one ~1 s at worst plus scheduling noise.
    reply.delayMs = 4000;
    server_.addReply("/slow", reply);

    const DownloadRequest req = makeRequest("/slow", Method::Get, {}, makeInet(30000));

    std::stop_source source;
    DownloadResult result;
    std::thread worker([&] {
        const auto downloader = makeDownloader(); // built ON the worker thread: CurlSession thread affinity
        result = downloader->doRequest(req, source.get_token());
    });

    // T-0093: cancel only once the server has the request, i.e. the transfer is really in flight
    // (a fixed 200 ms sleep raced the worker's start-up under parallel ctest: a late worker saw
    // the stop token pre-flight and returned Canceled instead of UnknownError). Polling with a
    // generous deadline; the assertion below fails if it is ever hit.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (server_.requestLog().isEmpty() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_FALSE(server_.requestLog().isEmpty()) << "the request never reached the server";
    const auto cancelledAt = std::chrono::steady_clock::now();
    source.request_stop();
    worker.join();
    const auto elapsed = std::chrono::steady_clock::now() - cancelledAt;

    EXPECT_EQ(result.kind, ResultKind::UnknownError);
    EXPECT_TRUE(result.text.contains(QStringLiteral("Disconnected by user")));
    // Well under the 4 s server delay (an un-cancelled transfer takes ~4 s) and the 30 s timeout;
    // 2.5 s leaves ~1.5 s for the libcurl heartbeat tick plus load-induced scheduling delay.
    EXPECT_LT(elapsed, std::chrono::milliseconds(2500));
}

// T-0010B rework finding 1 ("Test it"): CurlSession::onProgress_ must be invoked with
// (dlnow, dltotal), forwarded from curlXferInfoCallback/curlProgressCallback exactly as legacy's
// CurlProgressCallBack (u_CurlHttpClient.pas:159-175) forwards to FCurlProgressCallBack.
TEST_F(CurlDownloaderTest, ProgressCallbackForwardsDownloadCounters) {
    HttpTestServer::Reply reply;
    reply.body = QByteArray(65536, 'x'); // large enough that libcurl ticks the callback more than once
    server_.addReply("/progress", reply);

    QList<QPair<qint64, qint64>> ticks;
    DownloaderOptions options;
    options.onProgress = [&ticks](qint64 dlnow, qint64 dltotal) { ticks.append({dlnow, dltotal}); };

    const auto downloader = makeDownloader(options);
    const DownloadResult result = downloader->doRequest(makeRequest("/progress"), std::stop_token{});
    ASSERT_EQ(result.kind, ResultKind::Ok);

    ASSERT_FALSE(ticks.isEmpty());
    // The final tick always reports the full transfer: dlnow caught up to the bytes actually
    // received. dltotal is asserted separately since libcurl may report it as 0 until the
    // Content-Length header has been parsed (irrelevant to finding 1 -- forwarding, not totals).
    EXPECT_EQ(ticks.last().first, static_cast<qint64>(reply.body.size()));
}

// T-0010B rework findings 3, 4, 5 -- the 407-retry-once path, using the fixture's setProxyMode().
TEST_F(CurlDownloaderTest, ProxyAuthRetriesOnceWithCredentialsAndRealm) {
    server_.setProxyMode(true);
    HttpTestServer::Reply reply;
    reply.body = "via-proxy";
    server_.addReply("/proxied", reply);

    auto proxySettings = std::make_shared<ProxySettings>();
    proxySettings->useProxy = true;
    proxySettings->useSystemSettings = false;
    proxySettings->type = ProxyType::Http;
    proxySettings->host = "127.0.0.1:" + QByteArray::number(server_.port());
    proxySettings->useLogin = false; // no credentials up front -> forces the 407 round trip

    auto inet = std::make_shared<InetSettings>();
    inet->timeoutMs = 5000;
    inet->proxy = proxySettings;

    const auto fakeResolver = std::make_shared<FakeProxyResolver>();
    CurlDownloader downloader(DownloaderOptions{}, cg::core::ContentTypeRegistry::standard(), nullptr,
                              fakeResolver);

    DownloadRequest req = makeRequest("/proxied", Method::Get, {}, inet);
    const DownloadResult result = downloader.doRequest(req, std::stop_token{});
    ASSERT_EQ(result.kind, ResultKind::Ok);
    EXPECT_EQ(result.data, QByteArrayLiteral("via-proxy"));

    // (i) exactly one retry.
    const QList<QByteArray> log = server_.requestLog();
    ASSERT_EQ(log.size(), 2);

    // (ii) the retry carries Proxy-Authorization and (iii) CurlProxy::authMask came from the
    // parsed challenge -- both only possible if credentialsFor() was consulted exactly once with
    // the realm-carrying, translated prompt text (finding 5; HttpTestServer's challenge is
    // `Basic realm="test"`, u_DownloaderHttpByCurl.pas:271-292's format).
    EXPECT_EQ(fakeResolver->calls, 1);
    EXPECT_TRUE(log.at(1).contains("Proxy-Authorization:"));
    EXPECT_TRUE(fakeResolver->lastDescription.contains(QStringLiteral("\"test\"")));
    EXPECT_TRUE(
        fakeResolver->lastDescription.contains(QStringLiteral("is requesting a username and password")));

    // Finding 3, pinned: curl_easy_reset() (triggered by the retry's CurlProxy-only change) clears
    // CURLOPT_HTTPHEADER, and method/headers are unchanged, so rebuildHeaderList() never re-sets
    // it -- the retry carries NO custom headers. Legacy-faithful, kept 1:1 [D-18].
    const QByteArray retryHeaders = log.at(1).toUpper();
    EXPECT_FALSE(retryHeaders.contains("USER-AGENT:"));
    EXPECT_FALSE(retryHeaders.contains("ACCEPT-ENCODING:"));
}

// T-0293: no login -> CURLOPT_PROXYUSERPWD must be unset (NULL), not "": on libcurl < 8.2x an empty
// string counts as credentials and curl answers the 407 itself (empty Basic auth, a second request).
TEST_F(CurlDownloaderTest, ProxyWithoutCredentialsSendsNoProxyAuthorizationFirst) {
    server_.setProxyMode(true);
    HttpTestServer::Reply reply;
    reply.body = "via-proxy";
    server_.addReply("/proxied", reply);

    auto proxySettings = std::make_shared<ProxySettings>();
    proxySettings->useProxy = true;
    proxySettings->useSystemSettings = false;
    proxySettings->type = ProxyType::Http;
    proxySettings->host = "127.0.0.1:" + QByteArray::number(server_.port());
    proxySettings->useLogin = false;

    auto inet = std::make_shared<InetSettings>();
    inet->timeoutMs = 5000;
    inet->proxy = proxySettings;

    const auto fakeResolver = std::make_shared<FakeProxyResolver>();
    fakeResolver->grant = false;
    CurlDownloader downloader(DownloaderOptions{}, cg::core::ContentTypeRegistry::standard(), nullptr,
                              fakeResolver);

    DownloadRequest req = makeRequest("/proxied", Method::Get, {}, inet);
    const DownloadResult result = downloader.doRequest(req, std::stop_token{});

    const QList<QByteArray> log = server_.requestLog();
    std::string blobs; // shown only when the assertion fails
    for (const QByteArray& blob : log) {
        blobs += "<<<" + QByteArray(blob).replace('\r', "").toStdString() + ">>>\n";
    }
    ASSERT_EQ(log.size(), 1) << blobs;
    EXPECT_FALSE(log.at(0).toUpper().contains("PROXY-AUTHORIZATION:"));
    EXPECT_EQ(fakeResolver->calls, 1);
    // T-0302: u_DownloaderHttpByCurl.pas:296-300,313-318 -- no retry, BuildLoadErrorByUnknownReason
    // (FHttpResponse.ErrorReason): an UnknownError carrying the (empty) curl error text of a response
    // that libcurl delivered normally, NOT the 407 status-code LoadError.
    EXPECT_EQ(result.kind, ResultKind::UnknownError);
    EXPECT_TRUE(result.text.isEmpty()) << result.text.toStdString();
}

// T-0302: the prompt accepted with an EMPTY user name = the same as cancelled (legacy UserName <> '').
TEST_F(CurlDownloaderTest, ProxyAuthEmptyUserFromResolverDoesNotRetry) {
    server_.setProxyMode(true);
    HttpTestServer::Reply reply;
    reply.body = "via-proxy";
    server_.addReply("/proxied", reply);

    auto proxySettings = std::make_shared<ProxySettings>();
    proxySettings->useProxy = true;
    proxySettings->useSystemSettings = false;
    proxySettings->type = ProxyType::Http;
    proxySettings->host = "127.0.0.1:" + QByteArray::number(server_.port());
    proxySettings->useLogin = false;

    auto inet = std::make_shared<InetSettings>();
    inet->timeoutMs = 5000;
    inet->proxy = proxySettings;

    const auto fakeResolver = std::make_shared<FakeProxyResolver>();
    fakeResolver->grantedUser.clear();
    CurlDownloader downloader(DownloaderOptions{}, cg::core::ContentTypeRegistry::standard(), nullptr,
                              fakeResolver);

    DownloadRequest req = makeRequest("/proxied", Method::Get, {}, inet);
    const DownloadResult result = downloader.doRequest(req, std::stop_token{});

    EXPECT_EQ(server_.requestLog().size(), 1);
    EXPECT_EQ(fakeResolver->calls, 1);
    EXPECT_EQ(result.kind, ResultKind::UnknownError);
    EXPECT_TRUE(result.text.isEmpty()) << result.text.toStdString();
}

TEST_F(CurlDownloaderTest, RefusesExternalHostUnderTestGuard) {
    const auto downloader = makeDownloader();
    DownloadRequest req = makeRequest("/anything");
    req.url = "http://example.invalid/tile.png"; // not 127.0.0.1/::1/localhost
    const DownloadResult result = downloader->doRequest(req, std::stop_token{});
    EXPECT_EQ(result.kind, ResultKind::UnknownError);
    EXPECT_TRUE(result.text.contains(QStringLiteral("external network disabled")));
}

// u_DownloaderHttpByCurl.pas:254-326: an exception from the checker (either stage) is caught and
// becomes BuildLoadErrorByUnknownReason(E.Message); the downloader serves the next request.
TEST_F(CurlDownloaderTest, CheckerExceptionBecomesUnknownReasonLoadError) {
    HttpTestServer::Reply reply;
    reply.body = "hello";
    server_.addReply("/get", reply);
    const auto downloader = makeDownloader();

    DownloadRequest before = makeRequest("/get");
    before.checker = std::make_shared<ThrowingChecker>(true, false);
    const DownloadResult r1 = downloader->doRequest(before, std::stop_token{});
    EXPECT_EQ(r1.kind, ResultKind::UnknownError);
    EXPECT_EQ(r1.text, QStringLiteral("before-stage boom Город"));

    DownloadRequest after = makeRequest("/get");
    after.checker = std::make_shared<ThrowingChecker>(false, true);
    const DownloadResult r2 = downloader->doRequest(after, std::stop_token{});
    EXPECT_EQ(r2.kind, ResultKind::UnknownError);
    EXPECT_EQ(r2.text, QStringLiteral("after-stage boom город"));

    const DownloadResult r3 = downloader->doRequest(makeRequest("/get"), std::stop_token{});
    EXPECT_EQ(r3.kind, ResultKind::Ok);
    EXPECT_EQ(r3.data, QByteArrayLiteral("hello"));
}

// T-0329 (measured, no behaviour change): a refused connection is a connection-level failure that
// libcurl reports at once; the time a failed tile takes is the retry loop's pause (u_TileDownloaderSimple.pas
// :152-274, InetConfig SleepOnResetConnection), not this call. No server listens on the port: it is
// taken from a server that is destroyed first.
TEST_F(CurlDownloaderTest, RefusedPortFailsAtOnceWithoutWaitingForTheTimeout) {
    QByteArray deadUrl;
    {
        HttpTestServer gone;
        ASSERT_TRUE(gone.start());
        deadUrl = gone.url("/tile");
    }
    DownloadRequest req = makeRequest("/tile", Method::Get, {}, makeInet(40000)); // the legacy default
    req.url = deadUrl;
    const auto downloader = makeDownloader();
    const auto begin = std::chrono::steady_clock::now();
    const DownloadResult result = downloader->doRequest(req, std::stop_token{});
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    EXPECT_FALSE(result.isServerExists());
    EXPECT_NE(result.kind, ResultKind::Ok);
    EXPECT_TRUE(result.text.contains(QStringLiteral("curl error #7"))) << result.text.toStdString();
    EXPECT_LT(elapsed, std::chrono::milliseconds(1000));
}

// T-0329: the configured timeout bounds a server that accepts and then says nothing
// (u_DownloaderHttpByCurl.pas sets CURLOPT_TIMEOUT_MS and CURLOPT_CONNECTTIMEOUT_MS from the same
// InetConfig.TimeOut).
TEST_F(CurlDownloaderTest, TimeoutBoundsASilentServer) {
    HttpTestServer::Reply reply;
    reply.body = "late";
    reply.delayMs = 3000;
    server_.addReply("/silent", reply);
    const auto downloader = makeDownloader();
    const auto begin = std::chrono::steady_clock::now();
    const DownloadResult result =
        downloader->doRequest(makeRequest("/silent", Method::Get, {}, makeInet(300)), std::stop_token{});
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    EXPECT_FALSE(result.isServerExists());
    EXPECT_TRUE(result.text.contains(QStringLiteral("curl error #28"))) << result.text.toStdString();
    EXPECT_GE(elapsed, std::chrono::milliseconds(250));
    EXPECT_LT(elapsed, std::chrono::milliseconds(2000));
}
