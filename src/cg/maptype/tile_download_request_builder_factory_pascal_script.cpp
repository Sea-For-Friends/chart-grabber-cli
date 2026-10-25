// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_download_request_builder_factory_pascal_script.h"
// Line citations without a file name are u_TileDownloadRequestBuilderFactoryPascalScript.pas.

#include "cg/maptype/tile_download_request_builder_pascal_script.h"

#include <exception>
#include <utility>

namespace cg::maptype {

TileDownloadRequestBuilderFactoryPascalScript::TileDownloadRequestBuilderFactoryPascalScript(
    const QString& zmpFileName, QByteArray scriptText,
    std::shared_ptr<TileDownloadRequestBuilderConfig> config,
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig, geo::ProjectionSetPtr projectionSet,
    net::IDownloadCheckerPtr checker, std::shared_ptr<const script::IProjConverterFactory> projFactory,
    std::shared_ptr<core::ILanguageManager> languageManager, std::shared_ptr<storage::ITileStorage> storage,
    storage::MapVersionFactoryPtr versionFactory,
    std::shared_ptr<const core::ContentTypeRegistry> contentTypes, int appId, const QString& logsPath)
    : zmpFileName_(zmpFileName), state_(std::make_shared<TileDownloaderStateInternal>()), // :133-135
      config_(std::move(config)), projectionSet_(std::move(projectionSet)),
      tileDownloaderConfig_(std::move(tileDownloaderConfig)), checker_(std::move(checker)),
      languageManager_(std::move(languageManager)), scriptText_(std::move(scriptText)),
      projFactory_(std::move(projFactory)) {
    if (scriptText_.isEmpty()) {
        // :137-141: the URL comes from UrlTemplate::render, nothing else is created.
        isScriptInitialized_ = true;
        return;
    }
    global_ = std::make_shared<PascalScriptGlobal>();                              // :143
    logger_ = std::make_shared<PascalScriptLogger>(appId, logsPath, zmpFileName_); // :145-149
    tileCache_ = std::make_shared<PascalScriptTileCache>(std::move(storage), std::move(versionFactory),
                                                         std::move(contentTypes));           // :151-155
    coordConverter_ = std::make_shared<CoordConverterSimpleByProjectionSet>(projectionSet_); // :157
}

std::shared_ptr<ITileDownloaderStateChangeable> TileDownloadRequestBuilderFactoryPascalScript::state() const {
    return state_;
}

ITileDownloadRequestBuilderPtr
TileDownloadRequestBuilderFactoryPascalScript::buildRequestBuilder(const net::IDownloaderPtr& downloader) {
    if (!state_->enabled()) { // :207
        return nullptr;
    }
    try {
        std::shared_ptr<const script::CompiledScript> compiled;
        std::shared_ptr<const script::IProjConverter> defProjConverter;
        if (!isScriptInitialized_.load(std::memory_order_acquire)) {
            core::WriteLock guard(initMutex_);
            if (!isScriptInitialized_.load(std::memory_order_relaxed)) { // :209, :212
                try {
                    const QByteArray projArgs = config_->defaultProjConverterArgs();
                    if (!projArgs.isEmpty()) {
                        defProjConverter_ = projFactory_->byInitString(projArgs); // :214-216
                    }
                    auto result =
                        script::CompiledScript::compile(scriptText_, zmpFileName_); // DoCompileScript, :218
                    if (result.has_value()) {
                        compiled_ = std::move(*result);
                        isScriptInitialized_.store(true, std::memory_order_release); // :219
                    } else {
                        state_->disable(result.error().what()); // :221-223: EPascalScriptCompileError
                    }
                } catch (const std::exception& e) {
                    state_->disable(QStringLiteral("Unknown script compile error: ") +
                                    QString::fromUtf8(e.what()));
                }
            }
            // A failed compile leaves compiled_ null: the builder below is template-only (LD-18).
            compiled = compiled_;
            defProjConverter = defProjConverter_;
        } else {
            compiled = compiled_;
            defProjConverter = defProjConverter_;
        }
        return std::make_shared<TileDownloadRequestBuilderPascalScript>(
            std::move(compiled), config_, tileDownloaderConfig_, projectionSet_, coordConverter_, downloader,
            checker_, std::move(defProjConverter), projFactory_, languageManager_, global_, logger_,
            tileCache_);
    } catch (const std::exception& e) {
        state_->disable(QStringLiteral("Request builder create error: ") +
                        QString::fromUtf8(e.what())); // :250-252
        return nullptr;
    }
}

} // namespace cg::maptype
