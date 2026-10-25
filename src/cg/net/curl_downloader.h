// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Network/u_DownloaderHttpBase.pas:122-353, Src/Network/Curl/u_DownloaderHttpByCurl.pas,
// Src/Network/Curl/u_DownloaderByCurlFactory.pas. docs/specs/net.md §2.9. FEAT-NET-009.
//
// The request pipeline: file:// short-circuit, checker gates, the curl request/response round
// trip (with one 407-retry), then onAfterResponse's Content-Length/decode/detect/checker chain.
// Queue, workers, retry/pacing and proxy resolution are out of scope (T-0011A/B). AntiBan's
// preDownload/postCheckDownload hooks are called from the concrete IDownloadChecker (maptype,
// out of scope here) via `request.checker`, not from this file directly -- see anti_ban.h and
// download_checker.h §2.6. The script adapter is simple_http_downloader.h (T-0012B).

#include "cg/core/content_type.h"
#include "cg/net/curl_session.h"
#include "cg/net/curl_share.h"
#include "cg/net/download_checker.h"
#include "cg/net/downloader.h"
#include "cg/net/proxy_resolver.h"

#include <QMutex>

#include <memory>
#include <optional>
#include <stop_token>

namespace cg::net {

class CurlDownloader final : public IDownloader {
public:
    CurlDownloader(DownloaderOptions options, const cg::core::ContentTypeRegistry& types,
                   std::shared_ptr<CurlShare> share, std::shared_ptr<IProxyResolver> proxies);

    [[nodiscard]] DownloadResult doRequest(const DownloadRequest& request, std::stop_token stop) override;

private:
    // Ports ProcessFileSystemRequest (u_DownloaderHttpBase.pas:270-353). std::nullopt means "not a
    // file:// request, keep going"; a value is the final result.
    [[nodiscard]] std::optional<DownloadResult>
    processFileSystemRequest(const DownloadRequest& request) const;

    // Ports OnAfterResponse (u_DownloaderHttpBase.pas:122-268). `rawHeader`/`body` are consumed by
    // value because both are rewritten in place along the way (decode, content-type rewrite).
    [[nodiscard]] DownloadResult onAfterResponse(bool tryDecode, bool tryDetect,
                                                 const DownloadRequest& request, quint32 statusCode,
                                                 QByteArray rawHeader, QByteArray body) const;

    DownloaderOptions options_;
    const cg::core::ContentTypeRegistry& types_;
    std::shared_ptr<CurlShare> share_;
    std::shared_ptr<IProxyResolver> proxies_;
    CurlSession session_;
    QMutex mutex_; // docs/specs/net.md §2.9 step 1: a safety net, not load-bearing by design
};

class CurlDownloaderFactory final : public IDownloaderFactory {
public:
    CurlDownloaderFactory(const cg::core::ContentTypeRegistry& types,
                          std::shared_ptr<IProxyResolver> proxies);

    [[nodiscard]] IDownloaderPtr build(const DownloaderOptions& options) override;

private:
    const cg::core::ContentTypeRegistry& types_;
    std::shared_ptr<IProxyResolver> proxies_;
    std::shared_ptr<CurlShare> share_; // one per factory (i.e. per DownloadQueue), docs/specs/net.md §2.8.2
};

} // namespace cg::net
