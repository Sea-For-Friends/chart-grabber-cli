// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_download_request_builder_factory_js.h"
// Line citations without a file name are u_TileDownloadRequestBuilderFactoryPascalScript.pas.

#include "cg/maptype/tile_download_request_builder_js.h"

#include <exception>
#include <utility>

namespace cg::maptype {

TileDownloadRequestBuilderFactoryJs::TileDownloadRequestBuilderFactoryJs(
    QString sourceName, const QByteArray& scriptText,
    std::shared_ptr<TileDownloadRequestBuilderConfig> config,
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig, geo::ProjectionSetPtr projectionSet,
    net::IDownloadCheckerPtr checker, std::shared_ptr<core::ILanguageManager> languageManager,
    std::chrono::milliseconds timeLimit)
    : sourceName_(std::move(sourceName)), script_(QString::fromUtf8(scriptText)), timeLimit_(timeLimit),
      state_(std::make_shared<TileDownloaderStateInternal>()), // :133-135
      config_(std::move(config)), tileDownloaderConfig_(std::move(tileDownloaderConfig)),
      projectionSet_(std::move(projectionSet)), checker_(std::move(checker)),
      languageManager_(std::move(languageManager)), shared_(std::make_shared<JsSharedStore>()) { // :143
}

std::shared_ptr<ITileDownloaderStateChangeable> TileDownloadRequestBuilderFactoryJs::state() const {
    return state_;
}

std::shared_ptr<TileDownloadRequestBuilderJs>
TileDownloadRequestBuilderFactoryJs::newBuilder(const net::IDownloaderPtr& downloader,
                                                std::shared_ptr<JsSharedStore> shared) const {
    return std::make_shared<TileDownloadRequestBuilderJs>(script_, std::move(shared), watchdog_, timeLimit_,
                                                          config_, tileDownloaderConfig_, projectionSet_,
                                                          downloader, checker_, languageManager_);
}

ITileDownloadRequestBuilderPtr
TileDownloadRequestBuilderFactoryJs::buildRequestBuilder(const net::IDownloaderPtr& downloader) {
    if (!state_->enabled()) { // :207
        return nullptr;
    }
    try {
        if (!isScriptInitialized_.load(std::memory_order_acquire)) {
            core::WriteLock guard(initMutex_);
            if (!isScriptInitialized_.load(std::memory_order_relaxed) && state_->enabled()) { // :209, :212
                watchdog_ = std::make_shared<JsScriptWatchdog>();
                try {
                    // ADR-0014 point 6: the script runs once in a throw-away engine with a store of its own.
                    const QString failure =
                        newBuilder(downloader, std::make_shared<JsSharedStore>())->checkScript();
                    if (failure.isEmpty()) {
                        isScriptInitialized_.store(true, std::memory_order_release); // :219
                    } else {
                        state_->disable(failure); // :221-223: EPascalScriptCompileError
                    }
                } catch (const std::exception& e) {
                    state_->disable(QStringLiteral("Unknown script compile error: ") +
                                    QString::fromUtf8(e.what()));
                }
            }
        }
        if (!state_->enabled()) { // no builder runs a script that failed its check
            return nullptr;
        }
        return newBuilder(downloader, shared_);
    } catch (const std::exception& e) {
        state_->disable(QStringLiteral("Request builder create error: ") +
                        QString::fromUtf8(e.what())); // :250-252
        return nullptr;
    }
}

} // namespace cg::maptype
