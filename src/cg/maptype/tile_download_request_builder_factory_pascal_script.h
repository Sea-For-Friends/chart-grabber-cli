// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileDownloadRequestBuilderFactory.pas, u_TileDownloadRequestBuilderFactoryPascalScript.pas
//
// One factory per map (ZMP): compiles the script once, owns the per-ZMP script host objects and the
// downloader state; hands every download slot its own request builder over the same compiled program.

#include "cg/core/language_manager.h"
#include "cg/core/sync.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/coord_converter_simple_by_projection_set.h"
#include "cg/maptype/pascal_script_global.h"
#include "cg/maptype/pascal_script_logger.h"
#include "cg/maptype/pascal_script_tile_cache.h"
#include "cg/maptype/tile_download_request_builder.h"
#include "cg/maptype/tile_downloader_state.h"
#include "cg/net/download_checker.h"
#include "cg/net/downloader.h"
#include "cg/net/tile_downloader_config.h"
#include "cg/script/compiled_script.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_storage.h"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <memory>

namespace cg::maptype {

class ITileDownloadRequestBuilderFactory {
public:
    virtual ~ITileDownloadRequestBuilderFactory() = default;
    [[nodiscard]] virtual std::shared_ptr<ITileDownloaderStateChangeable> state() const = 0;
    [[nodiscard]] virtual ITileDownloadRequestBuilderPtr
    buildRequestBuilder(const net::IDownloaderPtr& downloader) = 0; // null when disabled / failed
};

class TileDownloadRequestBuilderFactoryPascalScript final : public ITileDownloadRequestBuilderFactory {
public:
    // An empty `scriptText` makes a template-only map: no global, logger, tile cache or coordinate
    // converter is created and the script is never compiled (u_…FactoryPascalScript.pas:137-141).
    TileDownloadRequestBuilderFactoryPascalScript(
        const QString& zmpFileName, QByteArray scriptText,
        std::shared_ptr<TileDownloadRequestBuilderConfig> config,
        std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig, geo::ProjectionSetPtr projectionSet,
        net::IDownloadCheckerPtr checker, std::shared_ptr<const script::IProjConverterFactory> projFactory,
        std::shared_ptr<core::ILanguageManager> languageManager,
        std::shared_ptr<storage::ITileStorage> storage, storage::MapVersionFactoryPtr versionFactory,
        std::shared_ptr<const core::ContentTypeRegistry> contentTypes, int appId, const QString& logsPath);

    [[nodiscard]] std::shared_ptr<ITileDownloaderStateChangeable> state() const override;
    [[nodiscard]] ITileDownloadRequestBuilderPtr
    buildRequestBuilder(const net::IDownloaderPtr& downloader) override;

private:
    QString zmpFileName_;
    std::shared_ptr<TileDownloaderStateInternal> state_;
    std::shared_ptr<TileDownloadRequestBuilderConfig> config_;
    geo::ProjectionSetPtr projectionSet_;
    std::shared_ptr<const script::ICoordConverter> coordConverter_;
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig_;
    net::IDownloadCheckerPtr checker_;
    std::shared_ptr<core::ILanguageManager> languageManager_;
    QByteArray scriptText_;
    std::shared_ptr<const script::IProjConverterFactory> projFactory_;
    std::shared_ptr<script::IScriptGlobal> global_;
    std::shared_ptr<script::IScriptLogger> logger_;
    std::shared_ptr<script::IScriptTileCache> tileCache_;

    // FLock guards the initialisation below; isScriptInitialized_ is the double-checked flag (an atomic
    // here, a plain Boolean read outside the lock in legacy :209-212).
    core::Mutex initMutex_;
    std::atomic<bool> isScriptInitialized_{false};
    std::shared_ptr<const script::CompiledScript> compiled_; // null: no script, or the compile failed
    std::shared_ptr<const script::IProjConverter> defProjConverter_;
};

} // namespace cg::maptype
