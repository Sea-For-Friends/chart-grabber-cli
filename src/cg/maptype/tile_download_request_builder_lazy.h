// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileDownloadRequestBuilderLazy.pas

#include "cg/core/sync.h"
#include "cg/maptype/tile_download_request_builder.h"
#include "cg/maptype/tile_download_request_builder_factory_pascal_script.h"
#include "cg/net/downloader.h"

#include <memory>

namespace cg::maptype {

// Asks the factory for the real builder on the first request that finds the factory's state enabled,
// and keeps it. FBuilderCS guards only the creation (u_TileDownloadRequestBuilderLazy.pas:89-102); the
// delegated BuildRequest runs outside it.
class TileDownloadRequestBuilderLazy final : public ITileDownloadRequestBuilder {
public:
    TileDownloadRequestBuilderLazy(net::IDownloaderPtr downloader,
                                   std::shared_ptr<ITileDownloadRequestBuilderFactory> factory);

    [[nodiscard]] std::optional<net::DownloadRequest>
    buildRequest(const ITileRequestPtr& source,
                 const std::shared_ptr<net::LastResponseInfo>& lastResponseInfo,
                 const std::stop_token& cancel) override;

    // Port extension (T-0366): the creation of buildRequest without a request, so that the factory's check of
    // a script runs when a verdict is asked for, not at the first tile.
    void load();

private:
    std::shared_ptr<ITileDownloadRequestBuilderFactory> factory_;
    net::IDownloaderPtr downloader_;
    ITileDownloadRequestBuilderPtr builder_; // guarded by builderMutex_
    core::Mutex builderMutex_;
};

} // namespace cg::maptype
