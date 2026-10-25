// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_Downloader.pas, i_DownloaderFactory.pas, u_DownloaderFactory.pas. docs/specs/net.md
// §2.7. FEAT-NET-006 (as a deviation: one engine, no neWinInet/neCurl split -- ADR-0004). Qt-free:
// no Qt Network here or anywhere under src/cg/net (enforced by tools/validate.sh's
// QNAM-IN-DOWNLOADER rule) -- the tile transport is libcurl, added with T-0010B.

#include "cg/net/download_request.h"
#include "cg/net/download_result.h"

#include <functional>
#include <memory>
#include <stop_token>

namespace cg::net {

// Ports TOnDownloadProgress (i_Downloader.pas:38-41). Called from the worker thread, inside
// easy_perform. Must not block.
using ProgressFn = std::function<void(qint64 received, qint64 total)>;

class IDownloader {
public:
    virtual ~IDownloader() = default;

    // Blocking. Returns a Canceled result promptly once `stop` is signalled. Never throws --
    // every failure, including a curl error, becomes a DownloadResult (docs/specs/net.md §4).
    [[nodiscard]] virtual DownloadResult doRequest(const DownloadRequest& request, std::stop_token stop) = 0;
};
using IDownloaderPtr = std::shared_ptr<IDownloader>;

// Ports IDownloaderFactory. Kept per idioms.md §13 purpose (a)'s exception: there are two real
// implementations (CurlDownloader, and a scripted FakeDownloader in tests), and a TTL-holding
// wrapper needs this seam to rebuild its client after the TTL fires.
struct DownloaderOptions {
    bool allowCookies = false; // ZMP AllowUseCookie (FEAT-NET-009)
    bool allowRedirect = true;
    bool tryDetectContentType = false; // ZMP DetectContentType
    bool acceptEncoding = true;        // legacy hardcodes True, u_DownloaderByCurlFactory.pas:47
    ProgressFn onProgress;             // null => CURLOPT_TIMEOUT_MS is set; non-null => it is 0
};

class IDownloaderFactory {
public:
    virtual ~IDownloaderFactory() = default;
    [[nodiscard]] virtual IDownloaderPtr build(const DownloaderOptions& options) = 0;
};

} // namespace cg::net
