// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing for the engine (NEW code, ADR-0014); the flow mirrors
// u_TileDownloadRequestBuilderFactoryPascalScript.pas (compile once under the init lock, disable the state on
// failure, "Request builder create error: ...").
//
// One factory per source with a JavaScript `script`: owns the source's `shared` store and the watchdog,
// checks the script once on the first buildRequestBuilder, and hands every download slot its own builder.

#include "cg/core/language_manager.h"
#include "cg/core/sync.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/js_script_watchdog.h"
#include "cg/maptype/js_shared_store.h"
#include "cg/maptype/tile_download_request_builder_factory_pascal_script.h"
#include "cg/net/download_checker.h"
#include "cg/net/downloader.h"
#include "cg/net/tile_downloader_config.h"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <chrono>
#include <memory>

namespace cg::maptype {

class TileDownloadRequestBuilderJs;

class TileDownloadRequestBuilderFactoryJs final : public ITileDownloadRequestBuilderFactory {
public:
    static constexpr std::chrono::milliseconds kDefaultTimeLimit{5000}; // script::ExecBudget maxWall

    // `timeLimit` is the script time per call (ADR-0014 point 5).
    TileDownloadRequestBuilderFactoryJs(QString sourceName, const QByteArray& scriptText,
                                        std::shared_ptr<TileDownloadRequestBuilderConfig> config,
                                        std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig,
                                        geo::ProjectionSetPtr projectionSet, net::IDownloadCheckerPtr checker,
                                        std::shared_ptr<core::ILanguageManager> languageManager,
                                        std::chrono::milliseconds timeLimit = kDefaultTimeLimit);

    [[nodiscard]] std::shared_ptr<ITileDownloaderStateChangeable> state() const override;
    // Null when the source is disabled or the script failed its check.
    [[nodiscard]] ITileDownloadRequestBuilderPtr
    buildRequestBuilder(const net::IDownloaderPtr& downloader) override;

private:
    [[nodiscard]] std::shared_ptr<TileDownloadRequestBuilderJs>
    newBuilder(const net::IDownloaderPtr& downloader, std::shared_ptr<JsSharedStore> shared) const;

    QString sourceName_;
    QString script_;
    std::chrono::milliseconds timeLimit_;
    std::shared_ptr<TileDownloaderStateInternal> state_;
    std::shared_ptr<TileDownloadRequestBuilderConfig> config_;
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig_;
    geo::ProjectionSetPtr projectionSet_;
    net::IDownloadCheckerPtr checker_;
    std::shared_ptr<core::ILanguageManager> languageManager_;
    std::shared_ptr<JsSharedStore> shared_;
    core::Mutex initMutex_; // guards the check below
    std::atomic<bool> isScriptInitialized_{false};
    std::shared_ptr<JsScriptWatchdog> watchdog_; // started with the first builder
};

} // namespace cg::maptype
