// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_download_request_builder_lazy.h"

#include <utility>

namespace cg::maptype {

TileDownloadRequestBuilderLazy::TileDownloadRequestBuilderLazy(
    net::IDownloaderPtr downloader, std::shared_ptr<ITileDownloadRequestBuilderFactory> factory)
    : factory_(std::move(factory)), downloader_(std::move(downloader)) {}

void TileDownloadRequestBuilderLazy::load() {
    core::WriteLock guard(builderMutex_);
    if (builder_ == nullptr && factory_->state()->getStatic()->enabled) {
        builder_ = factory_->buildRequestBuilder(downloader_);
    }
}

std::optional<net::DownloadRequest>
TileDownloadRequestBuilderLazy::buildRequest(const ITileRequestPtr& source,
                                             const std::shared_ptr<net::LastResponseInfo>& lastResponseInfo,
                                             const std::stop_token& cancel) {
    if (cancel.stop_requested()) { // :86 (ACancelNotifier <> nil) and not IsOperationCanceled
        return std::nullopt;
    }
    if (!factory_->state()->getStatic()->enabled) { // :87
        return std::nullopt;
    }
    ITileDownloadRequestBuilderPtr builder;
    {
        core::WriteLock guard(builderMutex_); // :89-102
        builder = builder_;
        if (builder == nullptr && factory_->state()->getStatic()->enabled) {
            builder = factory_->buildRequestBuilder(downloader_);
            if (builder != nullptr) {
                builder_ = builder;
            }
        }
    }
    if (builder == nullptr) {
        return std::nullopt;
    }
    return builder->buildRequest(source, lastResponseInfo, cancel);
}

} // namespace cg::maptype
