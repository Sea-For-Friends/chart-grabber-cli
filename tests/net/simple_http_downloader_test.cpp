// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-016 -- docs/specs/net.md §2.17, §7.14.
#include "cg/core/content_type.h"
#include "cg/net/curl_downloader.h"
#include "cg/net/download_request.h"
#include "cg/net/download_result.h"
#include "cg/net/downloader.h"
#include "cg/net/inet_config.h"
#include "cg/net/proxy_config.h"
#include "cg/net/proxy_resolver.h"
#include "cg/net/simple_http_downloader.h"

#include <QByteArray>
#include <QList>

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>

#include "fixture_path.h"
#include "http_test_server.h"

using cg::net::CurlDownloader;
using cg::net::CurlProxy;
using cg::net::DownloaderOptions;
using cg::net::DownloadRequest;
using cg::net::DownloadResult;
using cg::net::IDownloader;
using cg::net::InetSettings;
using cg::net::IProxyResolver;
using cg::net::Method;
using cg::net::ProxySettings;
using cg::net::ProxyType;
using cg::net::ResultKind;
using cg::net::simpleHttpRequest;
using cg::net::SimpleHttpResponse;
using cg::net::test::HttpTestServer;

namespace {

// docs/specs/net.md §7.0: no test may reach the real network; only the ProxyAuth test below (a
// real CurlDownloader) needs the guard -- set unconditionally, matching curl_downloader_test.cpp.
struct ForceNoExternalNetwork {
    ForceNoExternalNetwork() { qputenv("CG_NET_NO_EXTERNAL_NETWORK", "1"); }
};
const ForceNoExternalNetwork kForceNoExternalNetwork;

// A scripted transport: always returns the given result, records what it was asked to send.
class ScriptedDownloader final : public IDownloader {
public:
    explicit ScriptedDownloader(DownloadResult result) : result_(std::move(result)) {}

    DownloadResult doRequest(const DownloadRequest& request, std::stop_token /*stop*/) override {
        ++calls;
        lastRequest = request;
        return result_;
    }

    DownloadResult result_;
    DownloadRequest lastRequest;
    int calls = 0;
};

std::shared_ptr<InetSettings> makeInet() {
    return std::make_shared<InetSettings>();
}

// T-0010B rework finding 4/5's double, reused here for the proxy-retry test (see
// curl_downloader_test.cpp's identical copy for the rationale).
class FakeProxyResolver final : public IProxyResolver {
public:
    bool resolve(QByteArrayView /*url*/, CurlProxy& /*out*/) override { return false; }
    void setCredentialPrompt(CredentialFn /*fn*/) override {}
    bool credentialsFor(QByteArrayView /*proxyAddress*/, const QString& /*description*/, QByteArray& user,
                        QByteArray& pass) override {
        ++calls;
        user = "proxyuser";
        pass = "proxypass";
        return true;
    }
    int calls = 0;
};

} // namespace

TEST(SimpleHttpDownloaderTest, GetReturnsStatusHeaderAndData) {
    DownloadResult result;
    result.kind = ResultKind::Ok;
    result.statusCode = 200;
    result.rawResponseHeader = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n";
    result.data = "hello";
    ScriptedDownloader downloader(result);

    const SimpleHttpResponse response =
        simpleHttpRequest(downloader, makeInet(), "http://x/y", "", "", std::stop_token{});

    EXPECT_EQ(response.status, 200u);
    EXPECT_EQ(response.responseHeader, result.rawResponseHeader);
    EXPECT_EQ(response.responseData, QByteArray("hello"));
    EXPECT_EQ(downloader.lastRequest.method, Method::Get);
}

TEST(SimpleHttpDownloaderTest, NonEmptyPostDataSelectsPost) {
    DownloadResult result;
    result.kind = ResultKind::Ok;
    result.statusCode = 200;
    ScriptedDownloader downloader(result);

    const SimpleHttpResponse response =
        simpleHttpRequest(downloader, makeInet(), "http://x/y", "", "a=1", std::stop_token{});
    (void)response;

    EXPECT_EQ(downloader.lastRequest.method, Method::Post);
    EXPECT_EQ(downloader.lastRequest.postData, QByteArray("a=1"));
}

TEST(SimpleHttpDownloaderTest, LoadErrorGivesStatusZeroAndEmptyHeader) {
    // The deliberate legacy information loss: LoadError has no server respond, so the script sees
    // status == 0 and an empty header even though the transport knew the real HTTP status.
    DownloadResult result;
    result.kind = ResultKind::LoadError;
    result.statusCode = 500;
    result.rawResponseHeader = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
    ScriptedDownloader downloader(result);

    const SimpleHttpResponse response =
        simpleHttpRequest(downloader, makeInet(), "http://x/y", "", "", std::stop_token{});

    EXPECT_EQ(response.status, 0u);
    EXPECT_TRUE(response.responseHeader.isEmpty());
    EXPECT_TRUE(response.responseData.isEmpty());
}

TEST(SimpleHttpDownloaderTest, BannedGivesStatusAndHeaderButNoData) {
    DownloadResult result;
    result.kind = ResultKind::Banned;
    result.statusCode = 403;
    result.rawResponseHeader = "HTTP/1.1 403 Forbidden\r\n\r\n";
    ScriptedDownloader downloader(result);

    const SimpleHttpResponse response =
        simpleHttpRequest(downloader, makeInet(), "http://x/y", "", "", std::stop_token{});

    EXPECT_EQ(response.status, 403u);
    EXPECT_EQ(response.responseHeader, result.rawResponseHeader);
    EXPECT_TRUE(response.responseData.isEmpty()); // responseData is filled only for Ok
}

TEST(SimpleHttpDownloaderTest, CancelledBeforeStartReturnsZeros) {
    DownloadResult result;
    result.kind = ResultKind::Ok;
    result.statusCode = 200;
    result.data = "should never be seen";
    ScriptedDownloader downloader(result);

    std::stop_source source;
    source.request_stop();
    const SimpleHttpResponse response =
        simpleHttpRequest(downloader, makeInet(), "http://x/y", "", "", source.get_token());

    EXPECT_EQ(response.status, 0u);
    EXPECT_TRUE(response.responseHeader.isEmpty());
    EXPECT_TRUE(response.responseData.isEmpty());
    EXPECT_EQ(downloader.calls, 0); // never reaches the transport
}

TEST(SimpleHttpDownloaderTest, ProxyAuth407RetriesOnceWithCredentials) {
    cgEnsureCoreApplication();
    HttpTestServer server;
    ASSERT_TRUE(server.start());
    server.setProxyMode(true);
    HttpTestServer::Reply reply;
    reply.body = "via-proxy";
    server.addReply("/proxied", reply);

    auto proxySettings = std::make_shared<ProxySettings>();
    proxySettings->useProxy = true;
    proxySettings->useSystemSettings = false;
    proxySettings->type = ProxyType::Http;
    proxySettings->host = "127.0.0.1:" + QByteArray::number(server.port());
    proxySettings->useLogin = false; // no credentials up front -> forces the 407 round trip

    auto inet = std::make_shared<InetSettings>();
    inet->timeoutMs = 5000;
    inet->proxy = proxySettings;

    const auto fakeResolver = std::make_shared<FakeProxyResolver>();
    CurlDownloader downloader(DownloaderOptions{}, cg::core::ContentTypeRegistry::standard(), nullptr,
                              fakeResolver);

    const SimpleHttpResponse response =
        simpleHttpRequest(downloader, inet, server.url("/proxied"), "", "", std::stop_token{});

    EXPECT_EQ(response.status, 200u);
    EXPECT_EQ(response.responseData, QByteArray("via-proxy"));
    EXPECT_EQ(fakeResolver->calls, 1); // credential prompt consulted exactly once

    const QList<QByteArray> log = server.requestLog();
    ASSERT_EQ(log.size(), 2); // exactly one retry
    EXPECT_TRUE(log.at(1).contains("Proxy-Authorization:"));
}
