// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: the three libcurl callback functions of Src/Network/Curl/u_CurlHttpClient.pas:110-175
// (CurlWriteHeaderCallBack, CurlWriteDataCallBack, CurlProgressCallBack/CurlProgressCallBack2).
// docs/specs/net.md §2.8.4. FEAT-NET-009.
//
// Free functions, not member functions: libcurl calls them through a C function pointer, and
// userdata carries the CurlSession* (CURLOPT_HEADERDATA/WRITEDATA/XFERINFODATA/PROGRESSDATA all
// point at the same CurlSession). Defined in curl_session.cpp, where CurlSession is a complete
// type; declared here only so curl_session.cpp and tests can name them.

#include <cstddef>
#include <curl/curl.h>

namespace cg::net {

// CURLOPT_HEADERFUNCTION. userdata is a CurlSession*. Aborted (disconnect() called) -> returns 0,
// which libcurl turns into CURLE_WRITE_ERROR. A chunk starting with "HTTP/" (case-insensitive)
// clears the whole blob first -- only the LAST response's headers survive (redirects, 100
// Continue, proxy CONNECT). Otherwise the bytes are appended verbatim, CRLF included.
std::size_t curlHeaderCallback(char* data, std::size_t size, std::size_t nmemb, void* userdata);

// CURLOPT_WRITEFUNCTION. userdata is a CurlSession*. Aborted -> returns 0; else appends to the
// response body. No size cap.
std::size_t curlWriteCallback(char* data, std::size_t size, std::size_t nmemb, void* userdata);

// CURLOPT_XFERINFOFUNCTION (libcurl > 7.32.0). userdata is a CurlSession*. Non-zero return aborts
// the transfer -- the third way a transfer is torn down mid-flight, alongside the write/header
// callbacks' 0 return and CurlSession::disconnect() itself setting the flag they all poll.
int curlXferInfoCallback(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                         curl_off_t ulnow);

// CURLOPT_PROGRESSFUNCTION (pre-7.32.0 fallback; deprecated by libcurl but the branch is kept per
// docs/specs/net.md §2.8.3 row 19-20). Same abort contract, old (double) signature.
int curlProgressCallback(void* userdata, double dltotal, double dlnow, double ultotal, double ulnow);

} // namespace cg::net
