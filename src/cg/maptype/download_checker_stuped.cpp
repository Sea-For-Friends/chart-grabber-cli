// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/download_checker_stuped.h"

#include "cg/maptype/tile_request.h"
#include "cg/net/download_result_factory.h"

#include <QString>

#include <stdexcept>
#include <utility>

namespace cg::maptype {

DownloadCheckerStuped::DownloadCheckerStuped(net::IAntiBanPtr antiBan, PredicateByBinaryData banCheck,
                                             PredicateByBinaryData emptyTileCheck,
                                             std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig,
                                             storage::ITileStoragePtr storage)
    : antiBan_(std::move(antiBan)), banCheck_(std::move(banCheck)),
      emptyTileCheck_(std::move(emptyTileCheck)), tileDownloaderConfig_(std::move(tileDownloaderConfig)),
      storage_(std::move(storage)) {
    // Port preconditions, no legacy line: a null config would fail at GetStatic (:167) and a null storage at
    // GetTileInfo (:122), both inside the downloader's `except` (u_DownloaderHttpByCurl.pas:324-326); the
    // only creator (u_TileDownloadSubsystem.pas:321-327) passes both non-nil.
    if (!tileDownloaderConfig_) {
        throw std::invalid_argument("DownloadCheckerStuped: the tile downloader config is null");
    }
    if (!storage_) {
        throw std::invalid_argument("DownloadCheckerStuped: the tile storage is null");
    }
}

// IsNeedCheckTileSize (:102-112): `Supports(ARequest, ITileDownloadRequest)` is "the request has a tile
// source", then `Supports(Source, ITileRequestWithSizeCheck)` (:110).
bool DownloadCheckerStuped::isNeedCheckTileSize(const net::DownloadRequest& request) const {
    const ITileRequestPtr tileRequest = tileRequestFromSource(request.source);
    return dynamic_cast<const ITileRequestWithSizeCheck*>(tileRequest.get()) != nullptr;
}

// CheckOldTileSize with PrepareOldTileInfo (:114-142): the stored tile is read WITH data (:122). Any answer
// counts, also "not exists" and TNE whose size is 0 (u_TileInfoBasic.pas:142-145, 237-240), so a body of size
// 0 equals a missing tile. A nil answer (legacy: a folder / SQLite single-file storage without read access,
// an archive storage; the port: TileInfoType::Unknown) leaves CheckOldTileSize False (:135-141): the size
// check is skipped and the transport ends an empty 200 body as data-not-exists (net/curl_downloader.cpp:148).
// A storage error is an exception in legacy, caught by the downloader (u_DownloaderHttpByCurl.pas:324-326)
// and turned into BuildLoadErrorByUnknownReason(E.Message): returned as the error.
core::Result<bool> DownloadCheckerStuped::checkOldTileSize(const net::DownloadRequest& request,
                                                           quint32 newSize) const {
    const ITileRequestPtr tileRequest = tileRequestFromSource(request.source);
    if (!tileRequest) { // PrepareOldTileInfo: not a tile request, no info (:120-121)
        return false;
    }
    const core::Result<storage::TileInfo> info = storage_->tileInfo(
        tileRequest->tile(), tileRequest->zoom(), tileRequest->versionInfo(), storage::LoadMode::WithData);
    if (!info) {
        return core::fail(info.error().code(), info.error().what());
    }
    if (info->type == storage::TileInfoType::Unknown) { // Assigned(VExistsTileInfo) false (:136)
        return false;
    }
    return newSize == info->size;
}

std::optional<net::DownloadResult>
DownloadCheckerStuped::beforeRequest(const net::DownloadRequest& request) const {
    if (antiBan_) {
        antiBan_->preDownload(request);
    }
    return std::nullopt;
}

std::optional<net::DownloadResult>
DownloadCheckerStuped::afterReceiveData(const net::DownloadRequest& request, const QByteArray& body,
                                        quint32& statusCode, QByteArray& contentType,
                                        QByteArray& responseHead) const {
    const std::shared_ptr<const net::TileDownloaderSettings> config = tileDownloaderConfig_->settings();
    if (config->ignoreMimeType) {
        contentType = config->defaultMimeType;
    } else {
        if (contentType.isEmpty()) {
            contentType = config->defaultMimeType;
        } else if (!config->expectedMimeTypes.contains(
                       contentType)) { // Pos(...) <= 0, a substring test (:173)
            return net::result::badContentType(contentType, statusCode, responseHead);
        }
    }
    if (banCheck_ && banCheck_(body)) { // :184-194
        return net::result::banned(statusCode, responseHead);
    }
    if (emptyTileCheck_ && emptyTileCheck_(body)) { // :195-207
        return net::result::dataNotExists(QStringLiteral("Tile is recognized as empty"), statusCode,
                                          responseHead);
    }
    if (isNeedCheckTileSize(request)) { // :209-221
        const core::Result<bool> same = checkOldTileSize(request, static_cast<quint32>(body.size()));
        if (!same) {
            return net::result::loadErrorByUnknownReason(same.error().what());
        }
        if (*same) {
            return net::result::notNecessary(QStringLiteral("Tile with same size exists"), statusCode,
                                             responseHead);
        }
    }
    if (antiBan_) { // :222-230
        return antiBan_->postCheckDownload(request, body, statusCode, responseHead);
    }
    return std::nullopt;
}

} // namespace cg::maptype
