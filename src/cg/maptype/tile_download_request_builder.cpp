// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_download_request_builder.h"

#include <utility>

namespace cg::maptype {

TileDownloadRequestBuilder::TileDownloadRequestBuilder(
    std::shared_ptr<TileDownloadRequestBuilderConfig> config)
    : config_(std::move(config)) {}

} // namespace cg::maptype
