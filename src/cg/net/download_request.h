// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_DownloadRequest.pas, u_DownloadRequest.pas, i_DownloadChecker.pas (IRequestWithChecker),
// i_TileDownloadRequest.pas. docs/specs/net.md §2.3. Five legacy request interfaces, distinguished
// at runtime by Supports(), collapse into one value type with a method enum per idioms.md §13
// ("no abstraction with one user") -- the observable behavior (verb, body, whether a checker runs)
// is identical either way.

#include <QByteArray>

#include <cstdint>
#include <memory>

namespace cg::net {

// InetSettings (timeouts, UA, proxy, retry, TLS) is a config-provider value snapshot; its
// definition lands with T-0011A (inet_config.h, a struct). A forward declaration is enough here
// since only a pointer is stored and net never dereferences it -- T-0010B: fixed the class/struct
// tag mismatch clang-tidy's clang-diagnostic-mismatched-tags flags once a single TU sees both
// declarations (curl_session.h/curl_downloader.cpp are the first to include both headers).
struct InetSettings;

class IDownloadChecker;
using IDownloadCheckerPtr = std::shared_ptr<const IDownloadChecker>;

enum class Method : std::uint8_t { Head, Get, Post }; // ports TReqMethod, u_CurlHttpClient.pas:34

struct DownloadRequest {
    QByteArray url;           // absolute; may be file:// or an absolute path
    QByteArray requestHeader; // raw CRLF blob, no terminating blank line
    QByteArray postData;      // only read when method == Post
    Method method = Method::Get;

    // postData being non-empty does NOT by itself select Post: legacy decides the verb by
    // interface and the adapter above net picks Post exactly when postData is non-empty
    // (u_SimpleHttpDownloader.pas:141). The transport obeys `method` only.
    std::shared_ptr<const InetSettings> inet; // never null; asserted by the transport
    IDownloadCheckerPtr checker;              // null => no checker gates run

    // Opaque payload the caller (maptype) attaches so its checker can find the tile behind the
    // request, replacing ITileDownloadRequest.Source. net never dereferences it.
    std::shared_ptr<const void> source;
};

} // namespace cg::net
