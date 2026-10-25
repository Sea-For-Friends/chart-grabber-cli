// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Network/u_DownloaderHttpBase.pas:122-353, Src/Network/Curl/u_DownloaderHttpByCurl.pas,
// Src/Network/Curl/u_DownloaderByCurlFactory.pas. docs/specs/net.md §2.9. FEAT-NET-009.
#include "cg/net/curl_downloader.h"

#include "cg/core/content_detector.h"
#include "cg/net/content_decoder.h"
#include "cg/net/download_result_factory.h"
#include "cg/net/file_url.h"
#include "cg/net/http_headers.h"
#include "cg/net/http_status.h"
#include "cg/net/inet_config.h"
#include "cg/net/proxy_config.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QUrl>

#include <exception>
#include <utility>

namespace cg::net {

namespace {

// Free function has no QObject context for tr(); see download_result_factory.cpp's identical
// pattern and [D-9].
constexpr const char* kTrContext = "cg::net::CurlDownloader";

// docs/specs/net.md §7.0: no test may reach the real network. tests/net/CMakeLists.txt (or, for
// the nocmake fallback, tools/nocmake_build.sh) sets CG_NET_NO_EXTERNAL_NETWORK=1; read once,
// lazily, the first time any CurlDownloader needs it.
[[nodiscard]] bool externalNetworkBlocked() {
    static const bool blocked = qEnvironmentVariableIsSet("CG_NET_NO_EXTERNAL_NETWORK");
    return blocked;
}

[[nodiscard]] bool isAllowedTestHost(const QByteArray& url) {
    const QString host = QUrl::fromEncoded(url).host();
    return host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1") ||
           host == QStringLiteral("localhost");
}

// Ports ExtractFileExt for the WITH-leading-dot convention ContentTypeRegistry::infoByExt expects
// (core/content_type.h). "" when the file name has no '.'.
[[nodiscard]] QByteArray fileExtensionWithDot(const QString& path) {
    const QString name = QFileInfo(path).fileName();
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) {
        return {};
    }
    return name.mid(dot).toUtf8();
}

} // namespace

CurlDownloader::CurlDownloader(DownloaderOptions options, const cg::core::ContentTypeRegistry& types,
                               std::shared_ptr<CurlShare> share, std::shared_ptr<IProxyResolver> proxies)
    : options_(std::move(options)), types_(types), share_(std::move(share)), proxies_(std::move(proxies)),
      session_(QByteArray(), options_.onProgress) {
    if (share_) {
        session_.attachShare(*share_);
    }
}

std::optional<DownloadResult> CurlDownloader::processFileSystemRequest(const DownloadRequest& request) const {
    const std::optional<QByteArray> path = filePathFromUrl(request.url);
    if (!path) {
        return std::nullopt;
    }

    const QString fileName = QString::fromUtf8(*path);
    if (!QFileInfo::exists(fileName)) {
        return result::dataNotExistsByStatusCode(404, QByteArray());
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return result::dataNotExistsByStatusCode(404, QByteArray());
    }
    const QByteArray body = file.readAll();

    const QByteArray ext = fileExtensionWithDot(fileName);
    const cg::core::ContentTypeInfo* info = ext.isEmpty() ? nullptr : types_.infoByExt(ext);

    QByteArray rawHeader;
    if (info != nullptr) {
        rawHeader = "Content-Type: " + info->type; // no trailing CRLF, u_DownloaderHttpBase.pas:337
    }

    // decoding off; detect content-type only for an unknown extension (u_DownloaderHttpBase.pas:342-349)
    return onAfterResponse(false, rawHeader.isEmpty(), request, 200, rawHeader, body);
}

DownloadResult CurlDownloader::onAfterResponse(bool tryDecode, bool tryDetect, const DownloadRequest& request,
                                               quint32 statusCode, QByteArray rawHeader,
                                               QByteArray body) const {
    if (isOkStatus(statusCode)) {
        const QByteArray contentLengthStr = getHeaderValueUp(rawHeader, "CONTENT-LENGTH");
        if (!contentLengthStr.isEmpty()) {
            bool parsed = false;
            const qint64 expected = contentLengthStr.toLongLong(&parsed);
            if (!parsed) {
                // Ports StrToInt raising on a malformed Content-Length (u_DownloaderHttpBase.pas:147),
                // caught by DoRequest's outer `except` (u_DownloaderHttpByCurl.pas:312-318) and
                // turned into a load error -- finding 7 (T-0010B rework): was silently skipped here.
                return result::loadErrorByUnknownReason(QStringLiteral("'%1' is not a valid integer value")
                                                            .arg(QString::fromUtf8(contentLengthStr)));
            }
            if (expected != body.size()) {
                return result::badContentLength(body.size(), expected, statusCode, rawHeader);
            }
        }

        if (tryDecode) {
            const QByteArray encoding = getHeaderValueUp(rawHeader, "CONTENT-ENCODING");
            cg::core::Result<QByteArray> decoded = decode(encoding, body);
            if (!decoded) {
                return result::badContentEncoding(statusCode, rawHeader, encoding, body,
                                                  decoded.error().what());
            }
            body = *decoded;
            deleteHeaderValueUp(rawHeader, "CONTENT-ENCODING");
        }

        QByteArray contentType = getHeaderValueUp(rawHeader, "CONTENT-TYPE");
        if (tryDetect && !body.isEmpty()) {
            const QByteArray detected = cg::core::detectContentType(body);
            if (!detected.isEmpty() && detected != contentType.toLower()) {
                if (!replaceHeaderValueUp(rawHeader, "CONTENT-TYPE", detected)) {
                    addHeaderValue(rawHeader, "Content-Type", detected);
                }
                contentType = detected;
            }
        }

        if (request.checker) {
            if (std::optional<DownloadResult> r =
                    request.checker->afterReceiveData(request, body, statusCode, contentType, rawHeader)) {
                return *r;
            }
        }

        if (!body.isEmpty()) {
            return result::ok(statusCode, rawHeader, contentType, body);
        }
        return result::dataNotExistsZeroSize(statusCode, rawHeader);
    }
    if (isDownloadErrorStatus(statusCode)) {
        return result::loadErrorByStatusCode(statusCode); // header blob NOT passed, spec §2.9
    }
    if (isContentNotExistStatus(statusCode)) {
        return result::dataNotExistsByStatusCode(statusCode, rawHeader);
    }
    return result::loadErrorByUnknownStatusCode(statusCode); // header blob NOT passed, spec §2.9
}

DownloadResult CurlDownloader::doRequest(const DownloadRequest& request, std::stop_token stop) {
    Q_ASSERT(request.inet != nullptr);
    const QMutexLocker locker(&mutex_); // one CurlDownloader per worker in practice; this is the safety net

    if (stop.stop_requested()) {
        return result::canceled();
    }
    const std::stop_callback onStop(stop, [this] { session_.disconnect(); });
    if (stop.stop_requested()) {
        return result::canceled();
    }

    if (std::optional<DownloadResult> fileResult = processFileSystemRequest(request)) {
        return *fileResult;
    }

    if (externalNetworkBlocked() && !isAllowedTestHost(request.url)) {
        return result::loadErrorByUnknownReason(QStringLiteral("external network disabled"));
    }

    // Ports the try..except of u_DownloaderHttpByCurl.pas:254-326: from OnBeforeRequest to OnAfterResponse
    // (checker/anti-ban calls included) any exception becomes BuildLoadErrorByUnknownReason(E.Message).
    // The cancel/file-system steps above stay outside, as in legacy; cancel here is not an exception.
    try {
        if (request.checker) {
            if (std::optional<DownloadResult> r = request.checker->beforeRequest(request)) {
                return *r;
            }
        }

        CurlOptions curlOptions;
        curlOptions.storeCookie = options_.allowCookies;
        curlOptions.followLocation = options_.allowRedirect;
        curlOptions.verifyTls = !request.inet->ignoreTlsErrors;
        curlOptions.timeoutMs = options_.onProgress ? 0 : static_cast<int>(request.inet->timeoutMs);
        curlOptions.connectTimeoutMs = static_cast<int>(request.inet->timeoutMs);

        QByteArray headers = request.requestHeader;
        QByteArray userAgent = getHeaderValueUp(headers, "USER-AGENT");
        if (userAgent.isEmpty()) {
            userAgent = request.inet->userAgent;
            headers = "User-Agent: " + userAgent + "\r\n" + headers; // prepend, not append
        }

        if (options_.acceptEncoding) {
            const QByteArray encodingOld = getHeaderValueUp(headers, "ACCEPT-ENCODING");
            if (encodingOld.isEmpty()) {
                addHeaderValue(headers, "Accept-Encoding", acceptEncodingList());
            } else {
                const QByteArray filtered = filterAcceptEncoding(encodingOld);
                if (!filtered.isEmpty()) {
                    if (filtered != encodingOld) {
                        replaceHeaderValueUp(headers, "ACCEPT-ENCODING", filtered);
                    }
                } else {
                    deleteHeaderValueUp(headers, "ACCEPT-ENCODING");
                }
            }
        }

        CurlProxy curlProxy;
        const CurlProxy* proxyPtr = nullptr;
        if (const std::shared_ptr<const ProxySettings>& proxyCfg = request.inet->proxy;
            proxyCfg && (proxyCfg->useProxy || proxyCfg->useSystemSettings)) {
            curlProxy.authMask = CURLAUTH_ANY;
            if (proxyCfg->useProxy) {
                curlProxy.address = proxySchemePrefix(proxyCfg->type) + proxyCfg->host;
                if (proxyCfg->useLogin) {
                    curlProxy.userName = proxyCfg->login.toUtf8();
                    curlProxy.userPass = proxyCfg->password.toUtf8();
                }
                proxyPtr = &curlProxy;
            } else if (proxies_ && proxies_->resolve(request.url, curlProxy)) {
                proxyPtr = &curlProxy;
            }
        }

        CurlRequest curlRequest;
        curlRequest.method = request.method;
        curlRequest.url = request.url;
        curlRequest.userAgent = userAgent;
        curlRequest.headers = headers;
        if (request.method == Method::Post) {
            curlRequest.postData = request.postData.constData();
            curlRequest.postDataSize = request.postData.size();
        }
        curlRequest.options = &curlOptions;
        curlRequest.proxy = proxyPtr;

        CurlResponse response;
        bool ok = session_.perform(curlRequest, response);

        if (ok && response.code == 407 && proxyPtr != nullptr && proxies_) {
            const std::optional<ProxyAuthChallenge> challenge =
                IProxyResolver::parseChallenge(response.headers);

            // Ports the VDescription construction (u_DownloaderHttpByCurl.pas:271-292): the prompt
            // text the user reads carries the parsed realm -- `<address> "<realm>"` -- when the
            // challenge had one, else just the address; both wrapped in the translated sentence.
            // Finding 5 (T-0010B rework): was previously the bare address, realm dropped entirely.
            QString addressAndRealm = QString::fromUtf8(curlProxy.address);
            if (challenge && !challenge->realm.isEmpty()) {
                addressAndRealm = QStringLiteral("%1 \"%2\"").arg(addressAndRealm, challenge->realm);
            }
            const QString description =
                QCoreApplication::translate(kTrContext,
                                            "The proxy server %1 is requesting a username and password.")
                    .arg(addressAndRealm);

            QByteArray user;
            QByteArray pass;
            // u_DownloaderHttpByCurl.pas:296-300: VResult := UserName <> '' -- a cancelled prompt or an
            // accepted one with an empty name means NO retry and the load error built from the first
            // response's ErrorReason (:313-318), not the 407 status-code path.
            if (proxies_->credentialsFor(curlProxy.address, description, user, pass) && !user.isEmpty()) {
                curlProxy.userName = user;
                curlProxy.userPass = pass;
                if (challenge) {
                    curlProxy.authMask = challenge->curlAuthMask;
                }
                // Exactly one retry -- mutating curlProxy above (not curlRequest.headers/method) is
                // what makes CurlSession::perform() see a proxy-only change and reset the handle; the
                // retry then carries no custom headers at all ([D-18], curl_session.cpp's rebuild-slist
                // block). Kept 1:1 with legacy, pinned by ProxyAuthRetriesOnceWithCredentialsAndRealm.
                ok = session_.perform(curlRequest, response);
            } else {
                ok = false;
            }
        }

        if (!ok) {
            return result::loadErrorByUnknownReason(response.errorReason);
        }

        return onAfterResponse(options_.acceptEncoding, options_.tryDetectContentType, request,
                               static_cast<quint32>(response.code), response.headers, response.data);
    } catch (const std::exception& e) {
        return result::loadErrorByUnknownReason(QString::fromUtf8(e.what()));
    }
}

CurlDownloaderFactory::CurlDownloaderFactory(const cg::core::ContentTypeRegistry& types,
                                             std::shared_ptr<IProxyResolver> proxies)
    : types_(types), proxies_(std::move(proxies)), share_(std::make_shared<CurlShare>()) {}

IDownloaderPtr CurlDownloaderFactory::build(const DownloaderOptions& options) {
    return std::make_shared<CurlDownloader>(options, types_, share_, proxies_);
}

} // namespace cg::net
