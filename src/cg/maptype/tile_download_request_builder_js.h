// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing for the engine (NEW code, ADR-0014); the flow of buildRequest and the host objects mirror
// u_TileDownloadRequestBuilderPascalScript.pas (BuildRequest :217-283, SetVar :328-379) and the script's
// Downloader, u_SimpleHttpDownloader.pas:81-124. The QJSEngine is created on the calling thread, never shared
// and rebuilt when the slot's thread changes (ADR-0014 point 3); ctx.state lives in this class, not in the
// engine.
//
// One instance per (source, download slot). The base class lock is held for the whole buildRequest.
// Line citations in the .cpp without a file name are u_TileDownloadRequestBuilderPascalScript.pas.

#include "cg/core/language_manager.h"
#include "cg/core/notifier.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/js_bridge.h"
#include "cg/maptype/js_script_watchdog.h"
#include "cg/maptype/js_shared_store.h"
#include "cg/maptype/tile_download_request_builder.h"
#include "cg/maptype/url_template.h"
#include "cg/net/download_checker.h"
#include "cg/net/downloader.h"
#include "cg/net/tile_downloader_config.h"

#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

class QJSEngine;
class QJSValue;

namespace cg::maptype {

class TileDownloadRequestBuilderJs : public TileDownloadRequestBuilder, private IJsBridgeHost {
public:
    // `downloader` may be null (no http object). The script is not run here: the first buildRequest (or
    // checkScript) runs its top-level code.
    TileDownloadRequestBuilderJs(QString script, std::shared_ptr<JsSharedStore> shared,
                                 std::shared_ptr<JsScriptWatchdog> watchdog,
                                 std::chrono::milliseconds timeLimit,
                                 std::shared_ptr<TileDownloadRequestBuilderConfig> config,
                                 std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig,
                                 geo::ProjectionSetPtr projectionSet, net::IDownloaderPtr downloader,
                                 net::IDownloadCheckerPtr checker,
                                 std::shared_ptr<core::ILanguageManager> languageManager);
    ~TileDownloadRequestBuilderJs() override;

    // Throws std::runtime_error with "<message> at line <n>" when the script fails (legacy re-raises).
    [[nodiscard]] std::optional<net::DownloadRequest>
    buildRequest(const ITileRequestPtr& source,
                 const std::shared_ptr<net::LastResponseInfo>& lastResponseInfo,
                 const std::stop_token& cancel) override;

    // The factory's one-time check (ADR-0014 point 6): runs the top-level code in a fresh engine and looks
    // for the function getUrl. Empty text = fine, else the message the source is disabled with.
    [[nodiscard]] QString checkScript();

protected:
    // spec 5.4: pascal.random, pascal.randomRange and Math.random all draw here; a test subclass overrides
    // it.
    [[nodiscard]] qint32 random(qint32 upperExclusive) override;

private:
    void ensureEngine();
    [[nodiscard]] QJSValue makeTile(const ITileRequestPtr& source);
    [[nodiscard]] QJSValue makeContext(const std::shared_ptr<net::LastResponseInfo>& lastResponseInfo,
                                       const ITileRequestPtr& source, const QByteArray& urlBase);
    [[nodiscard]] UrlTemplateRequest templateRequest(const ITileRequestPtr& source) const;

    // IJsBridgeHost
    [[nodiscard]] QString templateToUrl(const QString& tmpl) override;
    [[nodiscard]] JsHttpReply httpRequest(const QByteArray& url, const QByteArray& requestHeader,
                                          const QByteArray& postData) override;
    void log(const QString& text) override;

    QString script_;
    qsizetype scriptLines_; // the lines of script_: an error line outside them is not reported
    std::shared_ptr<JsSharedStore> shared_;
    std::shared_ptr<JsScriptWatchdog> watchdog_;
    std::shared_ptr<JsScriptWatchdog::Entry> clock_;
    std::chrono::milliseconds timeLimit_;
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig_;
    net::IDownloadCheckerPtr checker_;
    net::IDownloaderPtr downloader_;
    geo::ProjectionSetPtr projectionSet_;
    std::shared_ptr<core::ILanguageManager> languageManager_;
    QByteArray lang_;                      // guarded by the base lock
    QString state_;                        // ctx.state: survives an engine rebuild
    ITileRequestPtr currentRequest_;       // keeps the version object UrlTemplate points at alive
    std::atomic<bool> langChanged_{false}; // FLangChangeFlag
    bool fileUrlWarned_ = false;           // sandbox: warned once about a file:/// request
    std::shared_ptr<const net::TileDownloaderSettings> settings_; // of the running call
    std::stop_token cancel_;                                      // of the running call
    std::unique_ptr<UrlTemplate> urlTemplate_;
    std::uint64_t engineThread_ = 0;   // serial of the thread that built engine_ (a thread id can be reused)
    std::unique_ptr<JsBridge> bridge_; // outlives the engine and the values below
    std::unique_ptr<QJSEngine> engine_;
    std::unique_ptr<QJSValue> run_;       // the prelude's wrapper function (null before the first engine)
    core::Subscription langSubscription_; // declared last, dropped first
};

} // namespace cg::maptype
