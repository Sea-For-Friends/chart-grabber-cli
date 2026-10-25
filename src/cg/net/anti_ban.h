// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_AntiBan.pas, u_AntiBanStuped.pas:70-180. docs/specs/net.md §2.14. FEAT-NET-012.

#include "cg/net/download_request.h"
#include "cg/net/download_result.h"
#include "cg/net/downloader.h"
#include "cg/net/inet_config.h"

#include <QByteArray>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace cg::net {

// Ports IAntiBan. Exactly two hooks, no more (ADR-0004).
class IAntiBan {
public:
    virtual ~IAntiBan() = default;

    virtual void preDownload(const DownloadRequest& request) = 0;

    // Legacy body is `if False then` (u_AntiBanStuped.pas:174) -- port as a hook that always
    // returns nullopt. See PreloadPageAntiBan::postCheckDownload for why.
    virtual std::optional<DownloadResult> postCheckDownload(const DownloadRequest& request,
                                                            const QByteArray& body, quint32& statusCode,
                                                            QByteArray& responseHead) = 0;
};
using IAntiBanPtr = std::shared_ptr<IAntiBan>;

// Ports TAntiBanStuped. Built per map from params.txt: `UsePreloadPage` (Integer, default 0) and
// `PreloadPage` (String, default empty), read by the caller from the ZMP's `[PARAMS]` sub-item
// (u_AntiBanStuped.pas:70-78) and passed in already-parsed.
class PreloadPageAntiBan final : public IAntiBan {
public:
    // usePreloadPage == 0 or preloadPage empty => the object is inert (legacy FIsEnabled).
    // `fetcher` performs preDownload's preload-page GET. `openInBrowser` is the on-ban action
    // (u_AntiBanStuped.pas:146-158); stored for API-shape fidelity with legacy's constructor but,
    // like legacy's own ExecOnBan, never invoked here -- see postCheckDownload.
    PreloadPageAntiBan(int usePreloadPage, QByteArray preloadPage, IDownloaderPtr fetcher,
                       std::function<void(const QByteArray& url)> openInBrowser);

    void preDownload(const DownloadRequest& request) override;

    [[nodiscard]] std::optional<DownloadResult> postCheckDownload(const DownloadRequest& request,
                                                                  const QByteArray& body, quint32& statusCode,
                                                                  QByteArray& responseHead) override;

private:
    // Ports TAntiBanStuped.ProcessAntiBan (IInvisibleBrowser.NavigateAndWait, :141). Until
    // ADR-0008 lands, a plain GET of the preload page through `fetcher_` (and therefore the same
    // cookie jar), body discarded. [D-14]
    void processAntiBan() const;

    bool enabled_ = false;
    int usePreloadPage_ = 0;
    QByteArray preloadPage_;
    IDownloaderPtr fetcher_;
    std::function<void(const QByteArray& url)> openInBrowser_;
    std::shared_ptr<const InetSettings> preloadInet_;
    std::atomic<int> counter_ = 0;
};

} // namespace cg::net
