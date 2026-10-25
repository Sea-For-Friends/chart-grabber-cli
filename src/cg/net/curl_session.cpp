// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Network/Curl/t_CurlHttpClient.pas, u_CurlHttpClient.pas. docs/specs/net.md §2.8.
// FEAT-NET-009.
#include "cg/net/curl_session.h"

#include "cg/core/log.h"
#include "cg/net/curl_global.h"
#include "cg/net/http_headers.h"

#include <atomic>
#include <utility>

namespace cg::net {

namespace {

constexpr const char* kMethodStr[] = {"HEAD", "GET", "POST"}; // indexed by Method, ports cMethod

// CURLOPT_PROXYUSERPWD. Ports _GetCurlUserPwd (u_CurlHttpClient.pas:302-308).
[[nodiscard]] QByteArray curlProxyUserPwd(const CurlProxy& proxy) {
    QByteArray result = urlEncode(proxy.userName);
    if (!result.isEmpty() && !proxy.userPass.isEmpty()) {
        result += ':';
        result += urlEncode(proxy.userPass);
    }
    return result;
}

// CURLOPT_DEBUGFUNCTION -- not one of the two byte-exact callbacks in curl_callbacks.h (those are
// the header/write callbacks the acceptance tests assert on); this one only ever runs when
// cg.net's debug logging is enabled (docs/specs/net.md §2.8.3 row 22-24), replacing legacy's
// {$IFDEF WRITE_HTTP_LOG}.
int curlDebugCallback(CURL* /*handle*/, curl_infotype /*type*/, char* data, std::size_t size,
                      void* /*userdata*/) {
    qCDebug(cg::core::logNet).noquote() << QByteArray(data, static_cast<qsizetype>(size));
    return 0;
}

#ifndef NDEBUG
// T-0144, debug only: marks the session as "a thread is inside" for the duration of a call (also when an
// exception leaves it) and asserts that no other thread already is. Mutual exclusion, not thread affinity:
// legacy runs one handle on whichever thread holds FCS (u_DownloaderHttpWithTTL.pas:102-138).
class UseGuard {
public:
    explicit UseGuard(std::atomic<bool>& inUse) : inUse_(inUse) {
        const bool wasInUse = inUse_.exchange(true);
        Q_ASSERT_X(!wasInUse, "CurlSession", "CurlSession in use by another thread");
        Q_UNUSED(wasInUse);
    }
    ~UseGuard() { inUse_.store(false); }
    UseGuard(const UseGuard&) = delete;
    UseGuard& operator=(const UseGuard&) = delete;

private:
    std::atomic<bool>& inUse_;
};
#endif

} // namespace

std::size_t curlHeaderCallback(char* data, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* self = static_cast<CurlSession*>(userdata);
    if (self->aborted()) {
        return 0;
    }
    const std::size_t total = size * nmemb;
    if (total >= 5 && QByteArrayView(data, 5).compare("HTTP/", Qt::CaseInsensitive) == 0) {
        // Collect only the last response's headers (https://curl.se/libcurl/c/CURLOPT_HEADERFUNCTION.html).
        self->currentResponse_->headers.clear();
    }
    self->currentResponse_->headers.append(data, static_cast<qsizetype>(total));
    return total;
}

std::size_t curlWriteCallback(char* data, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* self = static_cast<CurlSession*>(userdata);
    if (self->aborted()) {
        return 0;
    }
    const std::size_t total = size * nmemb;
    self->currentResponse_->data.append(data, static_cast<qsizetype>(total));
    return total;
}

// Ports CurlProgressCallBack (u_CurlHttpClient.pas:159-175) EXACTLY: if not aborted, forward the
// counters to the caller's progress fn (when one is installed) and return 0; if aborted, forward
// nothing and return 1. The callback itself is now installed unconditionally (see curlSetup()) so
// it also works as libcurl's documented abort heartbeat when no ProgressFn is set -- T-0010B
// rework finding 2 / [D-17]; onProgress_ only gates the forwarding, never the installation.
int curlXferInfoCallback(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t /*ultotal*/,
                         curl_off_t /*ulnow*/) {
    auto* self = static_cast<CurlSession*>(userdata);
    if (self->aborted()) {
        return 1;
    }
    if (self->onProgress_) {
        self->onProgress_(static_cast<qint64>(dlnow), static_cast<qint64>(dltotal));
    }
    return 0;
}

int curlProgressCallback(void* userdata, double dltotal, double dlnow, double /*ultotal*/, double /*ulnow*/) {
    auto* self = static_cast<CurlSession*>(userdata);
    if (self->aborted()) {
        return 1;
    }
    if (self->onProgress_) {
        self->onProgress_(static_cast<qint64>(dlnow), static_cast<qint64>(dltotal));
    }
    return 0;
}

CurlSession::CurlSession(QByteArray caBundlePath, ProgressFn onProgress)
    : caBundlePath_(std::move(caBundlePath)), onProgress_(std::move(onProgress)) {}

CurlSession::~CurlSession() {
#ifndef NDEBUG
    Q_ASSERT_X(!inUse_.load(), "CurlSession", "CurlSession in use by another thread (destroyed)");
#endif
    if (slist_ != nullptr) {
        curl_slist_free_all(slist_);
    }
    if (curl_ != nullptr) {
        curl_easy_cleanup(curl_);
    }
}

void CurlSession::attachShare(CurlShare& share) {
#ifndef NDEBUG
    const UseGuard useGuard(inUse_);
#endif
    share_ = &share;
    if (curl_ != nullptr) {
        curl_easy_setopt(curl_, CURLOPT_SHARE, share_->handle());
    }
}

void CurlSession::disconnect() noexcept {
    // The one member callable off the owning thread (docs/specs/net.md §3): sets the flag the
    // three callbacks poll, which is how a transfer aborts mid-flight.
    abort_.store(true, std::memory_order_release);
}

void CurlSession::rebuildHeaderList(const QByteArray& headers, bool isPost) {
    if (slist_ != nullptr) {
        curl_slist_free_all(slist_);
        slist_ = nullptr;
    }
    if (isPost) {
        // Suppresses libcurl's automatic "Expect: 100-continue" for large POST bodies
        // (u_CurlHttpClient.pas:481).
        slist_ = curl_slist_append(slist_, "Expect:");
    }
    // Ports CurlUpdateSlist / GetNextLine (u_CurlHttpClient.pas:366-381,
    // u_NetworkStrFunc.pas:104-129): split the raw CRLF blob on any control character, one
    // curl_slist entry per non-empty line.
    const char* p = headers.constData();
    const char* end = p + headers.size();
    while (p < end) {
        const char* lineStart = p;
        while (p < end && static_cast<unsigned char>(*p) >= ' ') {
            ++p;
        }
        if (p > lineStart) {
            const QByteArray line(lineStart, static_cast<qsizetype>(p - lineStart));
            slist_ = curl_slist_append(slist_, line.constData());
        }
        while (p < end && static_cast<unsigned char>(*p) < ' ') {
            ++p;
        }
    }
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, slist_);
}

void CurlSession::curlSetup() {
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, static_cast<long>(cachedOptions_.timeoutMs));
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(cachedOptions_.connectTimeoutMs));
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, cachedOptions_.followLocation ? 1L : 0L);

    // CURLOPT_CAINFO: unset (system CA store, [D-2]) unless the caller supplied an explicit
    // bundle path, exactly as legacy (u_CurlHttpClient.pas:316-321: `if FCertFileName <> ''`).
    // Nothing in this worktree constructs a CurlSession with a non-empty caBundlePath_ yet.
    if (!caBundlePath_.isEmpty()) {
        curl_easy_setopt(curl_, CURLOPT_CAINFO, caBundlePath_.constData());
    }

    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, cachedOptions_.verifyTls ? 1L : 0L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, cachedOptions_.verifyTls ? 2L : 0L);

    if (cachedOptions_.storeCookie) {
        curl_easy_setopt(curl_, CURLOPT_COOKIEFILE, ""); // in-memory engine, no jar file
    }

    curl_easy_setopt(curl_, CURLOPT_ACCEPT_ENCODING,
                     static_cast<const char*>(nullptr)); // §2.12: we decode ourselves

    if (!cachedProxy_.address.isEmpty()) {
        curl_easy_setopt(curl_, CURLOPT_PROXY, cachedProxy_.address.constData());
    } else {
        // Deliberately left UNSET, not set to "" -- an explicit empty CURLOPT_PROXY disables
        // libcurl's own http_proxy/https_proxy/all_proxy/no_proxy env-var handling
        // (https://curl.se/libcurl/c/CURLOPT_PROXY.html), which is exactly the "let curl read the
        // environment itself" path docs/specs/net.md §2.8.3 row 9 and §2.18 describe for the
        // Linux port -- unlike legacy, which always calls this setopt with PAnsiChar(''), an
        // explicit "" (Windows has no such env convention to preserve). Approved deviation
        // [D-16], docs/specs/net.md §8 (Tech Lead ruling on the T-0010B review, 2026-09-19).
    }
    curl_easy_setopt(curl_, CURLOPT_NOPROXY, cachedProxy_.noProxy.constData());
    curl_easy_setopt(curl_, CURLOPT_PROXYAUTH, cachedProxy_.authMask);
    const QByteArray proxyUserPwd = curlProxyUserPwd(cachedProxy_);
    // T-0293: an empty "" counts as credentials for libcurl < 8.2x (it answers a 407 by itself with empty
    // Basic auth); legacy passes '' (u_CurlHttpClient.pas:339) and relies on the 407 reaching the caller.
    // NULL keeps that on every libcurl.
    curl_easy_setopt(curl_, CURLOPT_PROXYUSERPWD,
                     proxyUserPwd.isEmpty() ? nullptr : proxyUserPwd.constData());

    curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION, curlHeaderCallback);
    curl_easy_setopt(curl_, CURLOPT_HEADERDATA, this);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, this);

    // T-0010B rework finding 2 / [D-17]: installed UNCONDITIONALLY, not only when onProgress_ is
    // set. libcurl's abort heartbeat (a non-zero return tears down easy_perform) is the only way
    // to cancel a transfer that is connecting, stalled, or waiting on a slow server -- the header
    // and write callbacks fire only once bytes arrive. Production builds a CurlSession with a
    // null ProgressFn for tile downloads (matching legacy, u_DownloaderHttpWithTTL.pas:114), so
    // gating the callback on onProgress_ (as legacy does, `Assigned(FCurlProgressCallBack)`,
    // u_CurlHttpClient.pas:348-356) would make disconnect() a no-op in exactly that configuration.
    // Costs nothing to set unconditionally, same reasoning as CURLOPT_NOSIGNAL [D-6]. onProgress_
    // still gates only whether curlXferInfoCallback/curlProgressCallback FORWARD the counters
    // (finding 1) -- it never gates whether the heartbeat itself is installed.
    if (curlVersionNum() > 0x072000U) {
        curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, curlXferInfoCallback);
        curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, this);
    } else {
        // libcurl deprecates CURLOPT_PROGRESSFUNCTION in favour of XFERINFOFUNCTION above;
        // the branch is kept for pre-7.32 libcurl per docs/specs/net.md §2.8.3 row 19-20.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        curl_easy_setopt(curl_, CURLOPT_PROGRESSFUNCTION, curlProgressCallback);
        curl_easy_setopt(curl_, CURLOPT_PROGRESSDATA, this);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    }
    curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);

    if (cg::core::logNet().isDebugEnabled()) {
        curl_easy_setopt(curl_, CURLOPT_DEBUGFUNCTION, curlDebugCallback);
        curl_easy_setopt(curl_, CURLOPT_DEBUGDATA, this);
        curl_easy_setopt(curl_, CURLOPT_VERBOSE, 1L);
    }

    // Additions beyond the legacy 32, each a recorded deviation (docs/specs/net.md §2.8.3).
    curl_easy_setopt(curl_, CURLOPT_MAXREDIRS, 10L);              // [D-5]
    curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);                // [D-6]
    curl_easy_setopt(curl_, CURLOPT_PROTOCOLS_STR, "http,https"); // [D-6]

    if (share_ != nullptr) {
        // curl_easy_reset() (called just before curlSetup() runs) clears CURLOPT_SHARE along with
        // everything else, so it must be re-applied here, not only once from attachShare().
        curl_easy_setopt(curl_, CURLOPT_SHARE, share_->handle());
    }
}

bool CurlSession::perform(const CurlRequest& request, CurlResponse& response) {
#ifndef NDEBUG
    const UseGuard useGuard(inUse_);
#endif

    response.code = 0;
    response.headers.clear();
    response.data.clear();
    response.errorReason.clear();
    abort_.store(false, std::memory_order_release);
    currentResponse_ = &response;

    bool doSetup = false;
    if (curl_ == nullptr) {
        curl_ = curl_easy_init();
        doSetup = true;
        if (curl_ == nullptr) {
            response.errorReason = QStringLiteral("Unexpected result of curl_easy_init()");
            currentResponse_ = nullptr;
            return false;
        }
    } else {
        // Ports IsOptionsChanged/IsProxyChanged (u_CurlHttpClient.pas:391-399) EXACTLY, including
        // the quirk that a null request.options/proxy reads as "unchanged", not "cleared".
        doSetup = (request.options != nullptr && !(cachedOptions_ == *request.options)) ||
                  (request.proxy != nullptr && !(cachedProxy_ == *request.proxy)) ||
                  (request.userAgent != cachedUserAgent_) || (request.method != cachedMethod_);
        if (doSetup) {
            curl_easy_reset(curl_);
        }
    }

    if (doSetup) {
        cachedOptions_ = request.options != nullptr ? *request.options : CurlOptions{};
        cachedProxy_ = request.proxy != nullptr ? *request.proxy : CurlProxy{};
        curlSetup();
    }

    curl_easy_setopt(curl_, CURLOPT_NOBODY, request.method == Method::Head ? 1L : 0L);

    if (request.method == Method::Post) {
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, request.postData);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.postDataSize));
    }

    const char* methodStr = kMethodStr[static_cast<std::size_t>(request.method)];
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, methodStr);
    curl_easy_setopt(curl_, CURLOPT_URL, request.url.constData());

    // KEPT LEGACY QUIRK [D-18] (T-0010B rework finding 3): curl_easy_reset() above (when doSetup
    // fired) clears CURLOPT_HTTPHEADER along with everything else, and curlSetup() never
    // re-applies it -- it is only ever set from rebuildHeaderList() below, and only when method or
    // the header blob actually changed. So a doSetup that is triggered by something OTHER than a
    // method/header change (e.g. curl_downloader.cpp's 407 proxy-credential retry, which only
    // mutates CurlProxy fields) sends its request with NO custom headers at all -- no User-Agent,
    // no Accept-Encoding, no Referer. Legacy has the identical gap: CurlSetup
    // (u_CurlHttpClient.pas:300-364) never sets coHTTPHeader either, and the slist block is
    // guarded by the same `(FReq.Method <> FReqMethod) or (FReq.Headers <> FReqHeaders)` condition
    // (:483-495). Ported 1:1, not fixed -- see the sibling note on the user-abort branch below,
    // which keeps the same slist/cachedHeaders_ memo quirk for the same reason.
    if (request.method != cachedMethod_ || request.headers != cachedHeaders_) {
        rebuildHeaderList(request.headers, request.method == Method::Post);
        cachedMethod_ = request.method;
        cachedHeaders_ = request.headers;
        cachedUserAgent_ = request.userAgent;
    }

    const CURLcode result = curl_easy_perform(curl_);

    if (result != CURLE_OK && !aborted()) {
        if (response.code == 0 && !response.headers.isEmpty()) {
            response.code = static_cast<long>(parseStatusLine(response.headers));
        }
        response.errorReason = QStringLiteral("curl error #%1 (%2) on %3 %4")
                                   .arg(static_cast<int>(result))
                                   .arg(QString::fromLatin1(curl_easy_strerror(result)))
                                   .arg(QString::fromLatin1(methodStr))
                                   .arg(QString::fromUtf8(request.url));
        currentResponse_ = nullptr;
        return false;
    }

    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.code);

    currentResponse_ = nullptr;

    if (aborted()) {
        // Ports the "user abort" branch (u_CurlHttpClient.pas:504-513): CurlClose() cleans up and
        // nils the handle, but does NOT free FCurlSlist or reset the FReq*/FOptions/FProxy memo --
        // faithfully reproduced here, quirk included: an identical next request (same
        // method/headers) skips rebuildHeaderList()'s setopt call, same as legacy.
        response.errorReason = QStringLiteral("Disconnected by user");
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
        return false;
    }

    return true;
}

} // namespace cg::net
