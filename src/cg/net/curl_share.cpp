// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: the share-handle wiring referenced by Src/Network/Curl/u_CurlHttpClient.pas.
// docs/specs/net.md §2.8.2.
#include "cg/net/curl_share.h"

namespace cg::net {

namespace {

void curlShareLock(CURL* /*handle*/, curl_lock_data /*data*/, curl_lock_access /*access*/, void* userptr) {
    static_cast<CurlShare*>(userptr)->lockForCallback();
}

void curlShareUnlock(CURL* /*handle*/, curl_lock_data /*data*/, void* userptr) {
    static_cast<CurlShare*>(userptr)->unlockForCallback();
}

} // namespace

CurlShare::CurlShare() {
    share_ = curl_share_init();
    curl_share_setopt(share_, CURLSHOPT_LOCKFUNC, curlShareLock);
    curl_share_setopt(share_, CURLSHOPT_UNLOCKFUNC, curlShareUnlock);
    curl_share_setopt(share_, CURLSHOPT_USERDATA, this);
    curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
}

CurlShare::~CurlShare() {
    curl_share_cleanup(share_);
}

} // namespace cg::net
