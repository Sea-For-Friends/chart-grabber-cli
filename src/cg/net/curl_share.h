// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: the share-handle wiring referenced by Src/Network/Curl/u_CurlHttpClient.pas (the curl
// share handle is set up once per client in legacy's TDownloaderHttpByCurl, not inside
// t_CurlHttpClient.pas itself). docs/specs/net.md §2.8.2 (curl option #32 of the 32-option
// table). FEAT-NET-009.
//
// One CurlShare per DownloadQueue (i.e. per map). Shares CURL_LOCK_DATA_DNS and
// CURL_LOCK_DATA_SSL_SESSION between that map's CurlSessions, each on its own thread; safe
// because CURLSHOPT_LOCKFUNC/UNLOCKFUNC serialise every access behind one QMutex. Cookies are
// deliberately NOT shared -- every CurlSession keeps its own in-memory cookie engine (see
// docs/specs/net.md §2.8.2): putting cookies here would merge sessions that must stay independent
// and is a user-visible behavior change on session-gated map servers.

#include <QMutex>

#include <curl/curl.h>

namespace cg::net {

class CurlShare {
public:
    CurlShare();
    ~CurlShare();
    CurlShare(const CurlShare&) = delete;
    CurlShare& operator=(const CurlShare&) = delete;

    [[nodiscard]] CURLSH* handle() const noexcept { return share_; }

    // For CURLSHOPT_LOCKFUNC/UNLOCKFUNC only (curl_share.cpp); not part of the public contract.
    void lockForCallback() { mutex_.lock(); }
    void unlockForCallback() { mutex_.unlock(); }

private:
    QMutex mutex_;
    CURLSH* share_ = nullptr;
};

} // namespace cg::net
