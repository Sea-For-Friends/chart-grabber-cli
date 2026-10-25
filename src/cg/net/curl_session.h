// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Network/Curl/t_CurlHttpClient.pas, u_CurlHttpClient.pas. docs/specs/net.md §2.8.
// FEAT-NET-009.
//
// One CURL* easy handle per CurlSession, used by one thread AT A TIME (libcurl's rule; legacy's is mutual
// exclusion too: u_DownloaderHttpWithTTL.pas:102-138 builds/uses/frees the client under FCS from whichever
// thread calls, the GC thread included). The handle may move between threads between calls; the callers
// serialise (TtlDownloader::mutex_, a fresh downloader per call). Debug builds assert that no second thread
// is inside perform()/attachShare() or the destructor (T-0144). CurlProxy is NOT redefined here: it
// already exists in proxy_resolver.h (landed with T-0011A before this header did) and is included from
// there, field-for-field identical to the spec, to keep exactly one definition (Tech Lead note on this
// ticket, docs/handoffs/T-0011A.md).

#include "cg/net/curl_callbacks.h"
#include "cg/net/curl_share.h"
#include "cg/net/download_request.h" // Method
#include "cg/net/downloader.h"       // ProgressFn
#include "cg/net/proxy_resolver.h"   // CurlProxy

#include <QByteArray>
#include <QString>

#include <atomic>
#include <curl/curl.h>

namespace cg::net {

struct CurlOptions { // ports TCurlOptions, t_CurlHttpClient.pas:38-48
    bool storeCookie = false;
    bool followLocation = true;
    bool verifyTls = true; // legacy: IgnoreSSLCertificateErrors, inverted. [D-2]
    int timeoutMs = 40000;
    int connectTimeoutMs = 40000;
    // NOTE legacy's operator== omits AcceptEncoding; we have no such field at all here --
    // Accept-Encoding is a request header, never a curl option (docs/specs/net.md §2.8.3).

    [[nodiscard]] bool operator==(const CurlOptions&) const = default;
};

struct CurlRequest { // ports TCurlRequest
    Method method = Method::Get;
    QByteArray url;
    QByteArray userAgent;
    QByteArray headers;             // raw CRLF blob
    const char* postData = nullptr; // NOT copied -- caller keeps it alive
    qint64 postDataSize = 0;
    const CurlOptions* options = nullptr; // null => defaults
    const CurlProxy* proxy = nullptr;     // null => no proxy at all
};

struct CurlResponse { // ports TCurlResponse
    long code = 0;
    QByteArray headers; // raw blob; see the header-callback reset rule (curl_callbacks.h)
    QByteArray data;
    QString errorReason;
};

class CurlSession {
public:
    CurlSession(QByteArray caBundlePath, ProgressFn onProgress);
    ~CurlSession();
    CurlSession(const CurlSession&) = delete;
    CurlSession& operator=(const CurlSession&) = delete;

    // Blocking. false => response.errorReason is set. Ports DoRequest (u_CurlHttpClient.pas:401-514).
    [[nodiscard]] bool perform(const CurlRequest& request, CurlResponse& response);

    void disconnect() noexcept; // sets the abort flag the callbacks poll; callable from any thread at any
                                // time (the cancel path), also while another thread is inside perform().
    void attachShare(CurlShare& share);

private:
    friend std::size_t curlHeaderCallback(char*, std::size_t, std::size_t, void*);
    friend std::size_t curlWriteCallback(char*, std::size_t, std::size_t, void*);
    friend int curlXferInfoCallback(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t);
    friend int curlProgressCallback(void*, double, double, double, double);

    void curlSetup(); // ports CurlSetup, u_CurlHttpClient.pas:300-364
    void rebuildHeaderList(const QByteArray& headers, bool isPost); // ports CurlUpdateSlist
    [[nodiscard]] bool aborted() const noexcept { return abort_.load(std::memory_order_acquire); }

    QByteArray caBundlePath_;
    ProgressFn onProgress_;
    CURL* curl_ = nullptr;
    curl_slist* slist_ = nullptr;
    CurlShare* share_ = nullptr;
    std::atomic<bool> abort_{false};
    CurlResponse* currentResponse_ = nullptr; // valid only during perform()

    // Re-setup memo, ported 1:1 from FOptions/FProxy/FReqMethod/FReqHeaders/FReqUserAgent
    // (u_CurlHttpClient.pas re-setup rule, docs/specs/net.md §2.8.3). Deliberately reproduces the
    // legacy quirk that a null request.options/proxy is treated as "unchanged", not "cleared".
    CurlOptions cachedOptions_;
    CurlProxy cachedProxy_;
    // Delphi zero-initialises FReqMethod to rmHead (TReqMethod = (rmHead, rmGet, rmPost)), not
    // rmGet -- Method has the same declaration order (download_request.h), so Method::Head is the
    // faithful default. Finding 8 (T-0010B rework): benign either way today (the downloader always
    // prepends a User-Agent: line, so the first request's header blob always differs from the
    // empty cachedHeaders_ and rebuilds the slist regardless), kept 1:1 anyway.
    Method cachedMethod_ = Method::Head;
    QByteArray cachedHeaders_;
    QByteArray cachedUserAgent_;

#ifndef NDEBUG
    std::atomic<bool> inUse_{false}; // debug only: a thread is inside perform()/attachShare()
#endif
};

} // namespace cg::net
