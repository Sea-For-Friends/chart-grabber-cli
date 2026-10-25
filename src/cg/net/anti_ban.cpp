// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_AntiBanStuped.pas:70-180. docs/specs/net.md §2.14. FEAT-NET-012.
#include "cg/net/anti_ban.h"

#include <stop_token>
#include <utility>

namespace cg::net {

PreloadPageAntiBan::PreloadPageAntiBan(int usePreloadPage, QByteArray preloadPage, IDownloaderPtr fetcher,
                                       std::function<void(const QByteArray& url)> openInBrowser)
    : enabled_(usePreloadPage > 0 && !preloadPage.isEmpty()), usePreloadPage_(usePreloadPage),
      preloadPage_(std::move(preloadPage)), fetcher_(std::move(fetcher)),
      openInBrowser_(std::move(openInBrowser)), preloadInet_(std::make_shared<const InetSettings>()) {}

void PreloadPageAntiBan::processAntiBan() const {
    if (!fetcher_) {
        return;
    }
    DownloadRequest request;
    request.url = preloadPage_;
    request.method = Method::Get;
    request.inet = preloadInet_;
    (void)fetcher_->doRequest(request, std::stop_token{}); // body discarded, [D-14]
}

void PreloadPageAntiBan::preDownload(const DownloadRequest& /*request*/) {
    if (!enabled_) {
        return;
    }
    // Ports IncDownloadedAndCheckAntiBan (u_AntiBanStuped.pas:123-139). Legacy's VCount is the
    // value InterlockedIncrement returns (u_SimpleFlagWithInterlock.pas:116-119); mirror that by
    // using the value the atomic increment itself returns, not a separate re-read of counter_.
    const int count = ++counter_;
    bool runAntiBan = false;
    if (usePreloadPage_ > 1) {
        runAntiBan = (count % usePreloadPage_) == 0;
    } else {
        runAntiBan = (count == 1);
    }
    if (runAntiBan) {
        processAntiBan();
    }
}

std::optional<DownloadResult> PreloadPageAntiBan::postCheckDownload(const DownloadRequest& /*request*/,
                                                                    const QByteArray& /*body*/,
                                                                    quint32& /*statusCode*/,
                                                                    QByteArray& /*responseHead*/) {
    // Ports PostCheckDownload (u_AntiBanStuped.pas:155-174). Legacy's ban-detection branch is dead
    // code (`if False then begin ... end`) -- spec: "do not restore the dead code". With that
    // branch gone, `Result` never leaves nil in legacy either, so the `Supports(Result, ...)`
    // checks that follow it (ExecOnBan on a ban, FBanFlag.SetFlag on success) are themselves
    // unreachable -- this is a literal no-op, matching legacy's own observable behavior exactly.
    return std::nullopt;
}

} // namespace cg::net
