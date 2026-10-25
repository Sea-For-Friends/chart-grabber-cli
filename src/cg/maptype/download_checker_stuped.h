// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_DownloadCheckerStuped.pas (the interface IDownloadChecker is net/download_checker.h)
//
// "Stuped" is the legacy spelling of the class name; the methods keep their port names (legacy
// BeforeRequest / AfterReciveData).

#include "cg/maptype/predicate_by_static_sample_list.h"
#include "cg/net/anti_ban.h"
#include "cg/net/download_checker.h"
#include "cg/net/tile_downloader_config.h"
#include "cg/storage/tile_storage.h"

#include <memory>
#include <optional>

namespace cg::maptype {

class DownloadCheckerStuped final : public net::IDownloadChecker {
public:
    // `antiBan`, `banCheck` and `emptyTileCheck` may be empty (legacy nil). `storage` is only used by the
    // size check. A null `tileDownloaderConfig` or `storage` is a port precondition (legacy would fail on
    // GetStatic / GetTileInfo, unreachable from the one creator): std::invalid_argument.
    DownloadCheckerStuped(net::IAntiBanPtr antiBan, PredicateByBinaryData banCheck,
                          PredicateByBinaryData emptyTileCheck,
                          std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig,
                          storage::ITileStoragePtr storage);

    // BeforeRequest (:144-153): AntiBan.PreDownload when set; never returns a result.
    [[nodiscard]] std::optional<net::DownloadResult>
    beforeRequest(const net::DownloadRequest& request) const override;

    // AfterReciveData (:155-231): content type, ban samples, empty samples, size check, anti-ban post check.
    [[nodiscard]] std::optional<net::DownloadResult>
    afterReceiveData(const net::DownloadRequest& request, const QByteArray& body, quint32& statusCode,
                     QByteArray& contentType, QByteArray& responseHead) const override;

private:
    [[nodiscard]] bool isNeedCheckTileSize(const net::DownloadRequest& request) const; // :102-112
    [[nodiscard]] core::Result<bool> checkOldTileSize(const net::DownloadRequest& request,
                                                      quint32 newSize) const; // :126-142

    net::IAntiBanPtr antiBan_;
    PredicateByBinaryData banCheck_;
    PredicateByBinaryData emptyTileCheck_;
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig_;
    storage::ITileStoragePtr storage_;
};

} // namespace cg::maptype
