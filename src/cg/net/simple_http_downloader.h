// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_SimpleHttpDownloader.pas, u_SimpleHttpDownloader.pas:127-170. docs/specs/net.md §2.17.
// FEAT-NET-016. This is what ADR-0003's `Downloader.DoHttpRequest` script binding calls; `script`
// must call this, not re-implement it.

#include "cg/net/downloader.h"
#include "cg/net/inet_config.h"

#include <QByteArray>
#include <QByteArrayView>

#include <memory>
#include <stop_token>

namespace cg::net {

struct SimpleHttpResponse {
    quint32 status = 0; // 0 when the result carried no server respond
    QByteArray responseHeader;
    QByteArray responseData;
};

// Ports TSimpleHttpDownloader.DoHttpRequest (u_SimpleHttpDownloader.pas:127-170). Literal
// behavior, and scripts depend on every clause:
// - Cancelled before start -> {0, "", ""}.
// - `postData` non-empty -> POST with that body; else GET. This is the only place the
//   "non-empty body => POST" rule lives.
// - If the result hasServerRespond() -> status and responseHeader are filled; otherwise both stay
//   0/empty even when the HTTP status was known (e.g. a LoadError from a 500 gives status == 0).
// - responseData is filled only for Ok.
[[nodiscard]] SimpleHttpResponse simpleHttpRequest(IDownloader& downloader,
                                                   std::shared_ptr<const InetSettings> inet,
                                                   QByteArrayView url, QByteArrayView requestHeader,
                                                   QByteArrayView postData, const std::stop_token& stop);

} // namespace cg::net
