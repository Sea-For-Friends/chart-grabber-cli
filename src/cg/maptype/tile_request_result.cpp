// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_request_result.h"

#include <stdexcept>
#include <utility>

namespace cg::maptype {

namespace {

// `FDownloadRequest.Source` (u_TileRequestResult.pas:153, :206): the ITileRequest behind the request.
ITileRequestPtr sourceOf(const net::DownloadRequest& request) {
    return tileRequestFromSource(request.source);
}

// `FDownloadResult.Request as ITileDownloadRequest` (:170, :184, :224, :257): an interface cast, Delphi
// raises EIntfCastError 'Interface not supported' (RTL knowledge, the RTL is not in the tree) when the
// request is not a tile request. Evaluated before the request is copied into the result.
net::DownloadRequest tileRequestOrThrow(net::DownloadRequest request) {
    if (!request.source) {
        throw std::logic_error("Interface not supported");
    }
    return request;
}

// Port precondition, no legacy line: legacy's constructor takes an IDownloadResultError, so a result of
// another kind cannot reach it. Checked FIRST in DownloadError's constructor, then the tile-request cast
// (legacy :257). std::invalid_argument is thrown before std::logic_error when both fail.
void requireErrorKind(const net::DownloadResult& result) {
    if (!isDownloadErrorKind(result.kind)) {
        throw std::invalid_argument("download result is not an error result");
    }
}

net::DownloadRequest downloadErrorRequest(net::DownloadRequest request, const net::DownloadResult& result) {
    requireErrorKind(result); // sequenced: a separate statement before the cast
    return tileRequestOrThrow(std::move(request));
}

QString downloadErrorText(const net::DownloadResult& result) {
    return QStringLiteral("Download error: ") + result.text;
}

} // namespace

bool isDownloadErrorKind(net::ResultKind kind) noexcept {
    switch (kind) {
    case net::ResultKind::ProxyError:
    case net::ResultKind::Banned:
    case net::ResultKind::BadContentType:
    case net::ResultKind::BadContentLength:
    case net::ResultKind::BadContentEncoding:
    case net::ResultKind::LoadError:
    case net::ResultKind::NoConnectToServer:
    case net::ResultKind::UnknownError:
        return true;
    case net::ResultKind::Ok:
    case net::ResultKind::NotNecessary:
    case net::ResultKind::Canceled:
    case net::ResultKind::DataNotExists:
        return false;
    }
    return false; // unreachable: every enumerator is handled above.
}

TileRequestResult::TileRequestResult(ITileRequestPtr request) : request_(std::move(request)) {}

ITileRequestPtr TileRequestResult::request() const {
    return request_;
}

TileRequestResultCanceledAfterBuildDownloadRequest::TileRequestResultCanceledAfterBuildDownloadRequest(
    net::DownloadRequest downloadRequest)
    : TileRequestResultCanceledBeforBuildDownloadRequest(sourceOf(downloadRequest)),
      downloadRequest_(std::move(downloadRequest)) {}

const net::DownloadRequest& TileRequestResultCanceledAfterBuildDownloadRequest::downloadRequest() const {
    return downloadRequest_;
}

TileRequestResultCanceledAfterDownloadRequest::TileRequestResultCanceledAfterDownloadRequest(
    net::DownloadRequest downloadRequest, net::DownloadResult downloadResult)
    : TileRequestResultCanceledAfterBuildDownloadRequest(tileRequestOrThrow(std::move(downloadRequest))),
      downloadResult_(std::move(downloadResult)) {}

const net::DownloadResult& TileRequestResultCanceledAfterDownloadRequest::downloadResult() const {
    return downloadResult_;
}

TileRequestResultOk::TileRequestResultOk(net::DownloadRequest downloadRequest,
                                         net::DownloadResult downloadResult)
    : TileRequestResult(sourceOf(tileRequestOrThrow(downloadRequest))),
      downloadRequest_(std::move(downloadRequest)), downloadResult_(std::move(downloadResult)) {}

const net::DownloadRequest& TileRequestResultOk::downloadRequest() const {
    return downloadRequest_;
}

const net::DownloadResult& TileRequestResultOk::downloadResult() const {
    return downloadResult_;
}

TileRequestResultErrorBeforBuildDownloadRequest::TileRequestResultErrorBeforBuildDownloadRequest(
    ITileRequestPtr request, QString errorText)
    : TileRequestResult(std::move(request)), errorText_(std::move(errorText)) {}

QString TileRequestResultErrorBeforBuildDownloadRequest::errorText() const {
    return errorText_;
}

TileRequestResultErrorAfterBuildDownloadRequest::TileRequestResultErrorAfterBuildDownloadRequest(
    net::DownloadRequest downloadRequest, QString errorText)
    : TileRequestResultErrorBeforBuildDownloadRequest(sourceOf(downloadRequest), std::move(errorText)),
      downloadRequest_(std::move(downloadRequest)) {}

const net::DownloadRequest& TileRequestResultErrorAfterBuildDownloadRequest::downloadRequest() const {
    return downloadRequest_;
}

TileRequestResultDownloadError::TileRequestResultDownloadError(net::DownloadRequest downloadRequest,
                                                               net::DownloadResult downloadResultError)
    : TileRequestResultErrorAfterBuildDownloadRequest(
          downloadErrorRequest(std::move(downloadRequest), downloadResultError),
          downloadErrorText(downloadResultError)),
      downloadResultError_(std::move(downloadResultError)) {}

const net::DownloadResult& TileRequestResultDownloadError::downloadResult() const {
    return downloadResultError_;
}

TileRequestResultErrorAfterDownloadRequest::TileRequestResultErrorAfterDownloadRequest(
    net::DownloadRequest downloadRequest, net::DownloadResult downloadResult, QString errorText)
    : TileRequestResultErrorAfterBuildDownloadRequest(tileRequestOrThrow(std::move(downloadRequest)),
                                                      std::move(errorText)),
      downloadResult_(std::move(downloadResult)) {}

const net::DownloadResult& TileRequestResultErrorAfterDownloadRequest::downloadResult() const {
    return downloadResult_;
}

} // namespace cg::maptype
