// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileDownloadRequestBuilderPascalScript.pas (the maptype half: constructor wiring, the language
// flag, SetVar, the template-only path and the turning of the outcome into a download request). The
// execution half - PrepareCompiledScript, the run, the ScriptBuffer that survives between calls - is
// script::UrlScript. Line citations in the .cpp without a file name are this legacy unit.
//
// One instance per (map, download slot). The base class lock is held for the whole buildRequest, like
// the legacy Lock/Unlock; it also serialises the UrlTemplate, which is not thread-safe.
//
// Script sandbox (user ruling Q-8, 2026-10-14; the two D rows of T-0194 in docs/parity/deviations.md): a
// script cannot write or probe files and cannot read a file:/// URL through its Downloader. SaveToLocalFile
// returns 0, FileExists returns false, DoHttpRequest to a file:/// URL answers status 0; each logs once per
// builder.

#include "cg/core/language_manager.h"
#include "cg/core/notifier.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/tile_download_request_builder.h"
#include "cg/maptype/url_template.h"
#include "cg/net/download_checker.h"
#include "cg/net/downloader.h"
#include "cg/net/tile_downloader_config.h"
#include "cg/script/compiled_script.h"
#include "cg/script/script_host.h"
#include "cg/script/url_script.h"

#include <QByteArray>

#include <atomic>
#include <memory>

namespace cg::maptype {

class TileDownloadRequestBuilderPascalScript final : public TileDownloadRequestBuilder,
                                                     private script::IScriptHost {
public:
    // A null `compiled` gives the template-only builder (legacy: ACompiledData = ''). Throws
    // std::runtime_error when the script instance cannot be created (the factory turns it into
    // "Request builder create error: ...").
    TileDownloadRequestBuilderPascalScript(std::shared_ptr<const script::CompiledScript> compiled,
                                           std::shared_ptr<TileDownloadRequestBuilderConfig> config,
                                           std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig,
                                           geo::ProjectionSetPtr projectionSet,
                                           std::shared_ptr<const script::ICoordConverter> coordConverter,
                                           net::IDownloaderPtr downloader, net::IDownloadCheckerPtr checker,
                                           std::shared_ptr<const script::IProjConverter> defProjConverter,
                                           std::shared_ptr<const script::IProjConverterFactory> projFactory,
                                           std::shared_ptr<core::ILanguageManager> languageManager,
                                           std::shared_ptr<script::IScriptGlobal> global,
                                           std::shared_ptr<script::IScriptLogger> logger,
                                           std::shared_ptr<script::IScriptTileCache> tileCache);
    ~TileDownloadRequestBuilderPascalScript() override;

    // Throws std::runtime_error with the script's own message when the script fails (legacy re-raises).
    [[nodiscard]] std::optional<net::DownloadRequest>
    buildRequest(const ITileRequestPtr& source,
                 const std::shared_ptr<net::LastResponseInfo>& lastResponseInfo,
                 const std::stop_token& cancel) override;

private:
    [[nodiscard]] script::ScriptVars setVar(const std::shared_ptr<net::LastResponseInfo>& lastResponseInfo,
                                            const net::TileDownloaderSettings& settings,
                                            const ITileRequestPtr& source, const std::stop_token& cancel);
    [[nodiscard]] UrlTemplateRequest templateRequest(const ITileRequestPtr& source) const;

    // IScriptHost (the registered functions the script calls)
    [[nodiscard]] QByteArray templateToUrl(const QByteArray& tmpl) const override;
    int saveToLocalFile(const QString& fullPath, const QByteArray& data) override;
    [[nodiscard]] bool fileExists(const QString& fullPath) const override;
    void writeLn(const QByteArray& text) override;
    [[nodiscard]] qint64 unixTime() const override;
    [[nodiscard]] qint32 random(qint32 upperExclusive) override;

    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig_;
    net::IDownloadCheckerPtr checker_;
    net::IDownloaderPtr downloader_;
    geo::ProjectionSetPtr projectionSet_;
    std::shared_ptr<const script::ICoordConverter> coordConverter_;
    std::shared_ptr<const script::IProjConverter> defProjConverter_;
    std::shared_ptr<const script::IProjConverterFactory> projFactory_;
    std::shared_ptr<script::IScriptGlobal> global_;
    std::shared_ptr<script::IScriptLogger> logger_;
    std::shared_ptr<script::IScriptTileCache> tileCache_;
    std::shared_ptr<core::ILanguageManager> languageManager_;
    QByteArray lang_;                               // guarded by the base lock
    ITileRequestPtr currentRequest_;                // keeps the version object UrlTemplate points at alive
    std::atomic<bool> langChanged_{false};          // FLangChangeFlag
    std::atomic<bool> saveWarned_{false};           // sandbox: warned once about SaveToLocalFile
    mutable std::atomic<bool> existsWarned_{false}; // sandbox: warned once about FileExists
    std::shared_ptr<std::atomic<bool>> fileUrlWarned_ =
        std::make_shared<std::atomic<bool>>(false); // shared with each ScriptHttpDownloader
    std::unique_ptr<UrlTemplate> urlTemplate_;      // used under the base lock
    std::unique_ptr<script::UrlScript> urlScript_;  // destroyed before urlTemplate_
    core::Subscription langSubscription_;           // declared last, dropped first (destructor :187-201)
};

} // namespace cg::maptype
