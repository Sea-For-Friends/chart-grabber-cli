// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: the process-wide libcurl init/cleanup implied by Src/Network/Curl/u_CurlHttpClient.pas.
// docs/specs/net.md §2.8.1. FEAT-NET-009.
#include "cg/net/curl_global.h"

#include <curl/curl.h>

namespace cg::net {

void curlGlobalInit() {
    curl_global_init(CURL_GLOBAL_ALL);
}

void curlGlobalCleanup() {
    curl_global_cleanup();
}

unsigned curlVersionNum() {
    // T-0288: legacy reads the cached `curl.info.version_num` field (u_CurlHttpClient.pas:348), it
    // does not call the library per request. curl_version_info() rewrites static buffers without a
    // lock (data race since libcurl 8.23), so it is called once; the magic static is thread-safe.
    static const unsigned versionNum = curl_version_info(CURLVERSION_NOW)->version_num;
    return versionNum;
}

} // namespace cg::net
