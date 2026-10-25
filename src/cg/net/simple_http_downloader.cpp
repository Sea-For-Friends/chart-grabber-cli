// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_SimpleHttpDownloader.pas:127-170. docs/specs/net.md §2.17. FEAT-NET-016.
#include "cg/net/simple_http_downloader.h"

#include <utility>

namespace cg::net {

SimpleHttpResponse simpleHttpRequest(IDownloader& downloader, std::shared_ptr<const InetSettings> inet,
                                     QByteArrayView url, QByteArrayView requestHeader,
                                     QByteArrayView postData, const std::stop_token& stop) {
    SimpleHttpResponse response;
    if (stop.stop_requested()) {
        return response; // {0, "", ""}, u_SimpleHttpDownloader.pas:141 (IsOperationCanceled)
    }

    DownloadRequest request;
    request.url = url.toByteArray();
    request.requestHeader = requestHeader.toByteArray();
    request.inet = std::move(inet);
    if (!postData.isEmpty()) {
        request.method = Method::Post;
        request.postData = postData.toByteArray();
    } else {
        request.method = Method::Get;
    }

    const DownloadResult result = downloader.doRequest(request, stop);

    // Ports the Supports(IDownloadResultWithServerRespond) / Supports(IDownloadResultOk) chain
    // (:113-121). Deliberate information loss: a result with no server respond (e.g. LoadError)
    // reports status 0 and an empty header even when the HTTP status was in fact known.
    if (result.hasServerRespond()) {
        response.status = result.statusCode;
        response.responseHeader = result.rawResponseHeader;
        if (result.kind == ResultKind::Ok) {
            response.responseData = result.data;
        }
    }
    return response;
}

} // namespace cg::net
