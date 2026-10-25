// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). See snapshot_resolver.h. docs/specs/weather-sources.md section 3.1, 3.4.
#include "cg/app/snapshot_resolver.h"

#include "cg/app/app_context.h"
#include "cg/maptype/map_type.h"
#include "cg/maptype/map_version_request_config.h"
#include "cg/maptype/tile_download_request_builder_config.h"
#include "cg/net/downloader.h"
#include "cg/net/simple_http_downloader.h"
#include "cg/net/tile_downloader_config.h"

#include <QCoreApplication>

namespace cg::app {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(SnapshotResolver)
};

constexpr qsizetype kMaxIndexBytes = qsizetype{8} * 1024 * 1024;

} // namespace

core::Result<Snapshot> resolveSnapshot(const SnapshotDefinition& definition, const maptype::IMapType& map,
                                       const AppContext& app, const std::stop_token& stop) {
    if (definition.type == SnapshotIndexType::NoneRefetch) {
        return parseSnapshotIndex(definition, {}); // T-0337: no request, the instant of now
    }
    const QString url = definition.indexUrl;
    const net::IDownloaderPtr downloader = app.downloaderFactory()->build(net::DownloaderOptions{});
    const net::SimpleHttpResponse response =
        net::simpleHttpRequest(*downloader, map.tileDownloaderConfig()->settings()->inet, url.toUtf8(),
                               map.tileDownloadRequestBuilderConfig()->requestHeader(), {}, stop);
    if (stop.stop_requested()) {
        return core::fail(core::ErrorCode::Cancelled, Tr::tr("cancelled"));
    }
    if (response.status == 0) {
        return core::fail(core::ErrorCode::IoError, Tr::tr("no answer from %1").arg(url));
    }
    if (response.status != 200) {
        return core::fail(core::ErrorCode::IoError, Tr::tr("HTTP %1 from %2").arg(response.status).arg(url));
    }
    if (response.responseData.isEmpty()) {
        return core::fail(core::ErrorCode::IoError, Tr::tr("the answer of %1 is empty").arg(url));
    }
    if (response.responseData.size() > kMaxIndexBytes) {
        return core::fail(core::ErrorCode::IoError, Tr::tr("the answer of %1 is larger than 8 MiB").arg(url));
    }
    auto parsed = parseSnapshotIndex(definition, response.responseData);
    if (!parsed && parsed.error().code() == core::ErrorCode::Unsupported) {
        return parsed; // T-0283: the refusal of --time is a whole text, without the url
    }
    if (!parsed) {
        return core::fail(parsed.error().code(), QStringLiteral("%1: %2").arg(url, parsed.error().what()));
    }
    return parsed;
}

void applySnapshot(maptype::IMapType& map, const QString& version) {
    const auto config = map.versionRequestConfig();
    config->setVersion(version);
    config->setShowOtherVersions(false);
}

} // namespace cg::app
