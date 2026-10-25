// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_download_request_builder_pascal_script.h"

#include "cg/maptype/log_maptype.h"
#include "cg/net/file_url.h"
#include "cg/net/simple_http_downloader.h"

#include <QDateTime>
#include <QRandomGenerator>

#include <atomic>
#include <stdexcept>
#include <string>
#include <utility>

namespace cg::maptype {

namespace {

// TSimpleHttpDownloader (u_SimpleHttpDownloader.pas) as the script's `Downloader` variable: one per
// request, bound to the request's inet settings and cancel token. The 'POST iff the body is
// non-empty' rule lives in net::simpleHttpRequest. Sandbox (user ruling Q-8 2026-10-14; the second D row of
// T-0194): a file:/// URL is not read (legacy :81-124 passes it to the downloader, which reads the file); the
// call reports what simpleHttpRequest reports for no server response, status 0 with empty header and body.
class ScriptHttpDownloader final : public script::IHttpDownloader {
public:
    ScriptHttpDownloader(net::IDownloaderPtr downloader, std::shared_ptr<const net::InetSettings> inet,
                         std::stop_token cancel, std::shared_ptr<std::atomic<bool>> warned)
        : downloader_(std::move(downloader)), inet_(std::move(inet)), cancel_(std::move(cancel)),
          warned_(std::move(warned)) {}

    [[nodiscard]] quint32 doHttpRequest(const QByteArray& url, const QByteArray& requestHeader,
                                        const QByteArray& postData, QByteArray& responseHeader,
                                        QByteArray& responseData) override {
        if (net::filePathFromUrl(url).has_value()) {
            if (!warned_->exchange(true)) {
                qCWarning(logMapType).noquote() << "script file reads are disabled (Downloader.DoHttpRequest"
                                                << QString::fromUtf8(url) << ")";
            }
            responseHeader.clear();
            responseData.clear();
            return 0;
        }
        net::SimpleHttpResponse r =
            net::simpleHttpRequest(*downloader_, inet_, url, requestHeader, postData, cancel_);
        responseHeader = std::move(r.responseHeader);
        responseData = std::move(r.responseData);
        return r.status;
    }

private:
    net::IDownloaderPtr downloader_;
    std::shared_ptr<const net::InetSettings> inet_;
    std::stop_token cancel_;
    std::shared_ptr<std::atomic<bool>> warned_;
};

net::DownloadRequest makeRequest(QByteArray url, QByteArray requestHeader, QByteArray postData,
                                 const net::TileDownloaderSettings& settings,
                                 net::IDownloadCheckerPtr checker, const ITileRequestPtr& source) {
    net::DownloadRequest request;
    request.url = std::move(url);
    request.requestHeader = std::move(requestHeader);
    request.method = postData.isEmpty() ? net::Method::Get : net::Method::Post;
    request.postData = std::move(postData);
    request.inet = settings.inet;
    request.checker = std::move(checker);
    request.source = source; // the checker casts this back to ITileRequest
    return request;
}

} // namespace

TileDownloadRequestBuilderPascalScript::TileDownloadRequestBuilderPascalScript(
    std::shared_ptr<const script::CompiledScript> compiled,
    std::shared_ptr<TileDownloadRequestBuilderConfig> config,
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig, geo::ProjectionSetPtr projectionSet,
    std::shared_ptr<const script::ICoordConverter> coordConverter, net::IDownloaderPtr downloader,
    net::IDownloadCheckerPtr checker, std::shared_ptr<const script::IProjConverter> defProjConverter,
    std::shared_ptr<const script::IProjConverterFactory> projFactory,
    std::shared_ptr<core::ILanguageManager> languageManager, std::shared_ptr<script::IScriptGlobal> global,
    std::shared_ptr<script::IScriptLogger> logger, std::shared_ptr<script::IScriptTileCache> tileCache)
    : TileDownloadRequestBuilder(config), tileDownloaderConfig_(std::move(tileDownloaderConfig)),
      checker_(std::move(checker)), downloader_(std::move(downloader)),
      projectionSet_(std::move(projectionSet)), coordConverter_(std::move(coordConverter)),
      defProjConverter_(std::move(defProjConverter)), projFactory_(std::move(projFactory)),
      global_(std::move(global)), logger_(std::move(logger)), tileCache_(std::move(tileCache)),
      languageManager_(std::move(languageManager)) {
    langSubscription_ = languageManager_->changed().subscribe([this] { langChanged_ = true; }); // :164-167
    urlTemplate_ =
        std::make_unique<UrlTemplate>(languageManager_, projectionSet_, std::move(config)); // :171-176
    // :178-182: FUseUrlTemplateDirectly := ACompiledData = ''; the script instance only for a compiled
    // program.
    auto created = script::UrlScript::create(std::move(compiled), *this);
    if (!created.has_value()) {
        throw std::runtime_error(created.error().what().toUtf8().constData());
    }
    urlScript_ = std::move(*created);
    langChanged_ = true; // :184 OnLangChange: the first request reads the language
}

TileDownloadRequestBuilderPascalScript::~TileDownloadRequestBuilderPascalScript() {
    langSubscription_
        .reset(); // :187-201: the language subscription goes first, then the script and the template
}

UrlTemplateRequest
TileDownloadRequestBuilderPascalScript::templateRequest(const ITileRequestPtr& source) const {
    const storage::MapVersionInfoPtr version = source->versionInfo();
    return {source->tile(), source->zoom(), version.get()};
}

std::optional<net::DownloadRequest> TileDownloadRequestBuilderPascalScript::buildRequest(
    const ITileRequestPtr& source, const std::shared_ptr<net::LastResponseInfo>& lastResponseInfo,
    const std::stop_token& cancel) {
    if (cancel.stop_requested()) { // :220
        return std::nullopt;
    }
    const Locker locker(*this);    // :221, :282
    if (cancel.stop_requested()) { // :223
        return std::nullopt;
    }
    const std::shared_ptr<const net::TileDownloaderSettings> settings =
        tileDownloaderConfig_->settings(); // :224

    if (urlScript_->isTemplateOnly()) { // :226-239
        currentRequest_ = source;       // legacy holds a counted ITileRequest in FPSUrlTemplate
        const QString url = urlTemplate_->render(templateRequest(source));
        if (url.isEmpty()) {
            return std::nullopt;
        }
        // AnsiString(VResultUrl) (:231). UTF-8, the encoding UrlTemplate decoded the config bytes from.
        return makeRequest(url.toUtf8(), config().requestHeader(), {}, *settings, checker_, source);
    }

    script::ScriptVars vars = setVar(lastResponseInfo, *settings, source, cancel); // :241-246
    // :247-256: RunScript / RaiseCurrentException / FPSExec.Stop; raise. UrlScript keeps ScriptBuffer (:279).
    core::Result<script::ScriptRequest> run = urlScript_->buildRequest(std::move(vars), cancel);
    if (!run.has_value()) {
        throw std::runtime_error(run.error().what().toUtf8().constData());
    }
    if (!run->tileExists || run->url.isEmpty()) { // :256
        return std::nullopt;
    }
    // :257-277: a non-empty PostData makes it a POST; both carry the script's RequestHead.
    return makeRequest(std::move(run->url), std::move(run->requestHead), std::move(run->postData), *settings,
                       checker_, source);
}

script::ScriptVars
TileDownloadRequestBuilderPascalScript::setVar(const std::shared_ptr<net::LastResponseInfo>& lastResponseInfo,
                                               const net::TileDownloaderSettings& settings,
                                               const ITileRequestPtr& source, const std::stop_token& cancel) {
    script::ScriptVars vars;
    // :333-340: the three fields under one read lock.
    const TileDownloadRequestBuilderConfigStatic cfg = config().snapshot();
    vars.getUrlBase = cfg.urlBase;
    vars.requestHead = cfg.requestHeader;
    const bool useDownloader = cfg.isUseDownloader;

    if (langChanged_.exchange(false)) { // :342-344
        lang_ = languageManager_->currentLanguageCode().toUtf8();
    }

    if (downloader_ != nullptr && useDownloader) { // :346-357
        vars.downloader =
            std::make_shared<ScriptHttpDownloader>(downloader_, settings.inet, cancel, fileUrlWarned_);
    }

    // ExecTimeSet (u_TileDownloadRequestBuilderPascalScriptVars.pas:192-266)
    const geo::PointI tile = source->tile();
    const quint8 zoom = source->zoom();
    if (!projectionSet_->checkZoom(zoom)) {
        // Zooms[VZoom] (u_..Vars.pas:217): TList.Get raises EListError 'List index out of bounds (N)'
        throw std::out_of_range("List index out of bounds (" + std::to_string(zoom) + ")");
    }
    const geo::IProjection& projection = *projectionSet_->zoom(zoom);
    vars.getX = tile.x();
    vars.getY = tile.y();
    vars.getZ = zoom + 1;
    const geo::LonLatRect ll = projection.tilePos2LonLatRect(tile);
    vars.getLlon = ll.left;
    vars.getTLat = ll.top;
    vars.getRLon = ll.right;
    vars.getBLat = ll.bottom;
    const geo::DoublePoint topLeft = projection.projectionType()->lonLat2Metr(ll.topLeft());
    const geo::DoublePoint bottomRight = projection.projectionType()->lonLat2Metr(ll.bottomRight());
    vars.getLMetr = topLeft.x();
    vars.getTMetr = topLeft.y();
    vars.getRMetr = bottomRight.x();
    vars.getBMetr = bottomRight.y();
    vars.converter = coordConverter_;
    vars.responseHead = lastResponseInfo != nullptr ? lastResponseInfo->responseHead() : QByteArray();
    const storage::MapVersionInfoPtr version = source->versionInfo();
    vars.version = version != nullptr ? version->storeString().toUtf8() : QByteArray();
    vars.lang = lang_;
    vars.defProjConverter = defProjConverter_;
    vars.projFactory = projFactory_;
    vars.global = global_;
    vars.logger = logger_;
    vars.tileCache = tileCache_;

    currentRequest_ = source; // :376 FPSUrlTemplate.Request := ASource
    urlTemplate_->setRequest(templateRequest(source));
    return vars;
}

QByteArray TileDownloadRequestBuilderPascalScript::templateToUrl(const QByteArray& tmpl) const {
    return urlTemplate_->templateToUrl(QString::fromUtf8(tmpl)).toUtf8();
}

int TileDownloadRequestBuilderPascalScript::saveToLocalFile(const QString& fullPath,
                                                            const QByteArray& /*data*/) {
    // Sandbox (Q-8): SaveToLocalFile_P (u_PascalScriptUtils.pas:173-200) wrote the file; now nothing is
    // written and the script sees the failure result 0.
    if (!saveWarned_.exchange(true)) {
        qCWarning(logMapType).noquote()
            << "script file writes are disabled (SaveToLocalFile \"" + fullPath + "\")";
    }
    return 0;
}

bool TileDownloadRequestBuilderPascalScript::fileExists(const QString& fullPath) const {
    // Sandbox (Q-8): SysUtils.FileExists (u_PascalScriptUtils.pas:50, :230) probed the disk; now false.
    if (!existsWarned_.exchange(true)) {
        qCWarning(logMapType).noquote()
            << "script file probes are disabled (FileExists \"" + fullPath + "\")";
    }
    return false;
}

void TileDownloadRequestBuilderPascalScript::writeLn(const QByteArray& /*text*/) {
    // FakeWriteLn (u_PascalScriptWriteLn.pas:54-57): the download path registers no console.
}

qint64 TileDownloadRequestBuilderPascalScript::unixTime() const {
    return QDateTime::currentSecsSinceEpoch(); // true UTC, D-7 (script.md section 12)
}

qint32 TileDownloadRequestBuilderPascalScript::random(qint32 upperExclusive) {
    if (upperExclusive <= 0) { // System.Random(X): X <= 0 gives 0
        return 0;
    }
    return static_cast<qint32>(QRandomGenerator::global()->bounded(upperExclusive));
}

} // namespace cg::maptype
