// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_download_request_builder_js.h"

#include "cg/maptype/log_maptype.h"
#include "cg/net/file_url.h"
#include "cg/net/simple_http_downloader.h"

#include <QJSEngine>
#include <QJSValue>
#include <QJSValueIterator>
#include <QRandomGenerator>

#include <atomic>
#include <stdexcept>
#include <string>
#include <utility>

namespace cg::maptype {

namespace {

constexpr auto kLimitText = "script time limit exceeded"; // ADR-0014 point 6
constexpr auto kResultText = "getUrl must return a string or { url: string }";

// The fixed prelude (ADR-0014 point 4): a function of (global, bridge, withHttp) that defines the frozen host
// objects of spec 5.2 / 5.4 on the global object over the one bridge object, which only its closure sees, and
// returns `run`, the wrapper every getUrl call goes through. `run` catches what the script throws and returns
// { v: result } or { e: message, l: line }, so a thrown string is told from a returned one and the lines of
// host errors (thrown by this prelude) are not reported.
const QString& prelude() {
    static const QString text = QStringLiteral(R"JS(
(function (g, b, withHttp) {
  function hostError(m) { var e = Error(m); e.host = true; return e; }
  function ok(r) { if (!r[0]) { throw hostError(r[1]); } return r[1]; }
  function num(name, v) {
    if (typeof v !== 'number') { throw TypeError('pascal.' + name + ': numeric argument expected'); }
    return v;
  }
  function head(h) {
    if (h === undefined || h === null) { return ''; }
    return Object.keys(h).map(function (k) { return k + ': ' + String(h[k]); }).join('\r\n');
  }
  function body(v) { return v === undefined || v === null ? '' : String(v); }
  function def(name, value) {
    Object.defineProperty(g, name, { value: Object.freeze(value), writable: false, enumerable: false, configurable: false });
  }
  def('template', function (t) { return ok(b.tmpl(String(t))); });
  def('log', function (t) { b.log(String(t)); });
  def('shared', {
    get: function (k) { return ok(b.sharedGet(String(k))); },
    set: function (k, v) { b.sharedSet(String(k), String(v)); },
    locked: function (fn) {
      if (typeof fn !== 'function') { throw TypeError('shared.locked: function expected'); }
      ok(b.lockBegin());
      try { return fn(); } finally { b.lockEnd(); }
    }
  });
  if (withHttp) {
    def('http', {
      get: function (url, headers) { return ok(b.http(String(url), head(headers), '')); },
      post: function (url, data, headers) { return ok(b.http(String(url), head(headers), body(data))); }
    });
  }
  def('pascal', {
    round: function (x) { return ok(b.round(num('round', x))); },
    roundEx: function (x, d) { return ok(b.roundEx(num('roundEx', x), num('roundEx', d))); },
    div: function (x, y) { return ok(b.div(num('div', x), num('div', y))); },
    mod: function (x, y) { return ok(b.mod(num('mod', x), num('mod', y))); },
    regExprGetMatchSubStr: function (s, re, i) {
      return ok(b.regExprGetMatchSubStr(String(s), String(re), num('regExprGetMatchSubStr', i)));
    },
    regExprReplaceMatchSubStr: function (s, re, r) {
      return ok(b.regExprReplaceMatchSubStr(String(s), String(re), String(r)));
    },
    random: function (n) { return b.random(num('random', n)); },
    randomRange: function (x, y) { return b.randomRange(num('randomRange', x), num('randomRange', y)); }
  });
  Math.random = function () { return b.random(1073741824) / 1073741824; };
  return function (fn, tile, ctx) {
    try {
      return { v: fn(tile, ctx) };
    } catch (e) {
      if (e !== null && typeof e === 'object' && typeof e.message === 'string') {
        return { e: e.message, l: e.host === true ? undefined : e.lineNumber };
      }
      return { e: String(e) };
    }
  };
})
)JS");
    return text;
}

// The engine gives a meaningless lineNumber to some errors (a RangeError of a stack overflow), so a line is
// reported only when it lies inside the script.
QString withLine(const QString& message, const QJSValue& line, qsizetype scriptLines) {
    return line.isNumber() && line.toInt() >= 1 && line.toInt() <= scriptLines
               ? message + QStringLiteral(" at line ") + QString::number(line.toInt())
               : message;
}

// "<message> at line <n>" of an error value that evaluate() returned (spec 5.1, as the interpreter's "%1 at
// line %2").
QString errorText(const QJSValue& error, qsizetype scriptLines) {
    if (error.property(QStringLiteral("host"))
            .toBool()) { // thrown by the prelude: its line is not the script's
        return error.property(QStringLiteral("message")).toString();
    }
    return withLine(error.property(QStringLiteral("message")).toString(),
                    error.property(QStringLiteral("lineNumber")), scriptLines);
}

// The raw header blob (CR LF between lines, config.requestHeader) as the object ctx.requestHeaders. A line
// without a colon has no JSON form (spec 3, headers) and is left out.
QJSValue headersToObject(QJSEngine& engine, const QByteArray& blob) {
    QJSValue object = engine.newObject();
    for (const QByteArray& line : blob.split('\n')) {
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        object.setProperty(QString::fromUtf8(line.left(colon).trimmed()),
                           QString::fromUtf8(line.mid(colon + 1).trimmed()));
    }
    return object;
}

// The object a script returned as `headers` as a blob: Name: value lines joined by CR LF.
QByteArray objectToHeaders(const QJSValue& object) {
    QByteArray blob;
    QJSValueIterator it(object);
    while (it.hasNext()) {
        it.next();
        if (!blob.isEmpty()) {
            blob += "\r\n";
        }
        blob += (it.name() + QStringLiteral(": ") + it.value().toString()).toUtf8();
    }
    return blob;
}

// Arms the clock of a builder for one run of script code; finish() reports whether the limit fired.
class TimedRun {
public:
    TimedRun(JsScriptWatchdog::Entry& clock, QJSEngine& engine, std::chrono::milliseconds limit)
        : clock_(clock), engine_(engine) {
        clock_.arm(&engine_, limit);
    }
    ~TimedRun() { (void)finish(); }
    TimedRun(const TimedRun&) = delete;
    TimedRun& operator=(const TimedRun&) = delete;

    [[nodiscard]] bool finish() {
        if (!finished_) {
            finished_ = true;
            fired_ = clock_.disarm();
            engine_.setInterrupted(false); // the next call of this engine runs again
        }
        return fired_;
    }

private:
    JsScriptWatchdog::Entry& clock_;
    QJSEngine& engine_;
    bool finished_ = false;
    bool fired_ = false;
};

// Identifies the calling thread for good: std::thread::id and pthread_t are reused once a thread is joined,
// and a slot restarted after the idle trim often gets the id of the thread that ended.
std::uint64_t currentThreadSerial() {
    static std::atomic<std::uint64_t> next{1};
    thread_local const std::uint64_t serial = next.fetch_add(1);
    return serial;
}

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

TileDownloadRequestBuilderJs::TileDownloadRequestBuilderJs(
    QString script, std::shared_ptr<JsSharedStore> shared, std::shared_ptr<JsScriptWatchdog> watchdog,
    std::chrono::milliseconds timeLimit, std::shared_ptr<TileDownloadRequestBuilderConfig> config,
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig, geo::ProjectionSetPtr projectionSet,
    net::IDownloaderPtr downloader, net::IDownloadCheckerPtr checker,
    std::shared_ptr<core::ILanguageManager> languageManager)
    : TileDownloadRequestBuilder(config), script_(std::move(script)), scriptLines_(script_.count(u'\n') + 1),
      shared_(std::move(shared)), watchdog_(std::move(watchdog)), clock_(watchdog_->add()),
      timeLimit_(timeLimit), tileDownloaderConfig_(std::move(tileDownloaderConfig)),
      checker_(std::move(checker)), downloader_(std::move(downloader)),
      projectionSet_(std::move(projectionSet)), languageManager_(std::move(languageManager)) {
    langSubscription_ = languageManager_->changed().subscribe([this] { langChanged_ = true; }); // :164-167
    urlTemplate_ =
        std::make_unique<UrlTemplate>(languageManager_, projectionSet_, std::move(config)); // :171-176
    langChanged_ = true; // :184 OnLangChange: the first request reads the language
}

TileDownloadRequestBuilderJs::~TileDownloadRequestBuilderJs() {
    langSubscription_
        .reset(); // :187-201: the language subscription goes first, then the engine and the template
}

UrlTemplateRequest TileDownloadRequestBuilderJs::templateRequest(const ITileRequestPtr& source) const {
    const storage::MapVersionInfoPtr version = source->versionInfo();
    return {source->tile(), source->zoom(), version.get()};
}

void TileDownloadRequestBuilderJs::ensureEngine() {
    const std::uint64_t me = currentThreadSerial();
    if (engine_ != nullptr && engineThread_ == me) {
        return;
    }
    // A slot restarted on another thread (the idle trim ended the old one): the old engine is dropped here
    // and a new one is built on this thread; only ctx.state and shared survive (ADR-0014 point 3).
    run_.reset();
    engine_.reset();
    bridge_.reset();
    IJsBridgeHost& host = *this; // a private base: converted here, inside the class
    bridge_ = std::make_unique<JsBridge>(host, shared_, clock_);
    engine_ = std::make_unique<QJSEngine>();
    engineThread_ = me;
    try {
        QJSEngine::setObjectOwnership(bridge_.get(), QJSEngine::CppOwnership);
        const QJSValue function = engine_->evaluate(prelude(), QStringLiteral("prelude.js"));
        const bool withHttp = downloader_ != nullptr && config().isUseDownloader(); // :346
        const QJSValue wrapper =
            function.call({engine_->globalObject(), engine_->newQObject(bridge_.get()), QJSValue(withHttp)});
        if (!wrapper.isCallable()) {
            throw std::logic_error("JavaScript prelude failed: " + errorText(wrapper, 0).toStdString());
        }
        run_ = std::make_unique<QJSValue>(wrapper);

        QJSValue result;
        QStringList stackTrace; // non-empty when the script threw, whatever the thrown value is
        bool fired = false;
        {
            TimedRun timed(*clock_, *engine_, timeLimit_);
            result = engine_->evaluate(script_, QStringLiteral("source.js"), 1,
                                       &stackTrace); // a classic script, never a module
            fired = timed.finish();
        }
        bridge_->releaseLock();
        if (fired) {
            throw std::runtime_error(kLimitText);
        }
        if (result.isError()) {
            throw std::runtime_error(errorText(result, scriptLines_).toUtf8().constData());
        }
        if (!stackTrace.isEmpty()) { // a top-level `throw <non-Error value>`: the value is the result
            throw std::runtime_error(result.toString().toUtf8().constData());
        }
    } catch (...) {
        run_.reset(); // the next call starts again from a fresh engine
        engine_.reset();
        bridge_.reset();
        throw;
    }
}

QString TileDownloadRequestBuilderJs::checkScript() {
    try {
        const Locker locker(*this);
        ensureEngine();
        if (!engine_->globalObject().property(QStringLiteral("getUrl")).isCallable()) {
            return QStringLiteral("function getUrl(tile, ctx) is not defined");
        }
    } catch (const std::exception& e) {
        return QString::fromUtf8(e.what());
    }
    return {};
}

QJSValue TileDownloadRequestBuilderJs::makeTile(const ITileRequestPtr& source) {
    const geo::PointI tile = source->tile();
    const quint8 zoom = source->zoom();
    if (!projectionSet_->checkZoom(zoom)) {
        // Zooms[VZoom] (u_..Vars.pas:217): TList.Get raises EListError 'List index out of bounds (N)'
        throw std::out_of_range("List index out of bounds (" + std::to_string(zoom) + ")");
    }
    const geo::IProjection& projection = *projectionSet_->zoom(zoom);
    const geo::LonLatRect ll = projection.tilePos2LonLatRect(tile);
    const geo::DoublePoint topLeft = projection.projectionType()->lonLat2Metr(ll.topLeft());
    const geo::DoublePoint bottomRight = projection.projectionType()->lonLat2Metr(ll.bottomRight());
    QJSValue t = engine_->newObject();
    t.setProperty(QStringLiteral("x"), tile.x());
    t.setProperty(QStringLiteral("y"), tile.y());
    t.setProperty(QStringLiteral("z"), zoom);
    t.setProperty(QStringLiteral("zoom1"), zoom + 1); // GetZ
    t.setProperty(QStringLiteral("lonLeft"), ll.left);
    t.setProperty(QStringLiteral("latTop"), ll.top);
    t.setProperty(QStringLiteral("lonRight"), ll.right);
    t.setProperty(QStringLiteral("latBottom"), ll.bottom);
    t.setProperty(QStringLiteral("mLeft"), topLeft.x());
    t.setProperty(QStringLiteral("mTop"), topLeft.y());
    t.setProperty(QStringLiteral("mRight"), bottomRight.x());
    t.setProperty(QStringLiteral("mBottom"), bottomRight.y());
    return t;
}

QJSValue
TileDownloadRequestBuilderJs::makeContext(const std::shared_ptr<net::LastResponseInfo>& lastResponseInfo,
                                          const ITileRequestPtr& source, const QByteArray& urlBase) {
    const storage::MapVersionInfoPtr version = source->versionInfo();
    QJSValue c = engine_->newObject();
    c.setProperty(QStringLiteral("urlBase"), QString::fromUtf8(urlBase));
    // "" without a version (u_ZmpInfo.pas:1068, u_..Vars.pas:253-256)
    c.setProperty(QStringLiteral("version"), version != nullptr ? version->storeString() : QString());
    c.setProperty(QStringLiteral("lang"), QString::fromUtf8(lang_));
    c.setProperty(QStringLiteral("requestHeaders"), headersToObject(*engine_, config().requestHeader()));
    c.setProperty(QStringLiteral("lastResponseHeaders"),
                  lastResponseInfo != nullptr ? QString::fromLatin1(lastResponseInfo->responseHead())
                                              : QString());
    c.setProperty(QStringLiteral("state"), state_);
    return c;
}

std::optional<net::DownloadRequest>
TileDownloadRequestBuilderJs::buildRequest(const ITileRequestPtr& source,
                                           const std::shared_ptr<net::LastResponseInfo>& lastResponseInfo,
                                           const std::stop_token& cancel) {
    if (cancel.stop_requested()) { // :220
        return std::nullopt;
    }
    const Locker locker(*this);    // :221, :282
    if (cancel.stop_requested()) { // :223
        return std::nullopt;
    }
    settings_ = tileDownloaderConfig_->settings(); // :224
    cancel_ = cancel;

    ensureEngine();
    const TileDownloadRequestBuilderConfigStatic cfg = config().snapshot(); // :333-340
    if (langChanged_.exchange(false)) {                                     // :342-344
        lang_ = languageManager_->currentLanguageCode().toUtf8();
    }
    const QJSValue tile = makeTile(source);
    const QJSValue ctx = makeContext(lastResponseInfo, source, cfg.urlBase);
    currentRequest_ = source; // :376 FPSUrlTemplate.Request := ASource
    urlTemplate_->setRequest(templateRequest(source));

    const QJSValue getUrl = engine_->globalObject().property(QStringLiteral("getUrl"));
    if (!getUrl.isCallable()) {
        throw std::runtime_error("function getUrl(tile, ctx) is not defined");
    }
    QJSValue out;
    bool fired = false;
    {
        bridge_->setInGetUrl(true);
        TimedRun timed(*clock_, *engine_, timeLimit_);
        out = run_->call({getUrl, tile, ctx});
        fired = timed.finish();
        bridge_->setInGetUrl(false);
        bridge_->releaseLock(); // defensive: whether an interrupted script runs the finally of shared.locked
                                // depends on the Qt version
    }
    if (fired) { // :247-256: the error leaves ctx.state as it was
        throw std::runtime_error(kLimitText);
    }
    const QJSValue message = out.property(QStringLiteral("e"));
    if (out.isError() || message.isString()) {
        throw std::runtime_error(
            (message.isString()
                 ? withLine(message.toString(), out.property(QStringLiteral("l")), scriptLines_)
                 : errorText(out, scriptLines_))
                .toUtf8()
                .constData());
    }
    const QJSValue ctxState = ctx.property(QStringLiteral("state"));
    state_ =
        ctxState.isUndefined() || ctxState.isNull() ? QString() : ctxState.toString(); // ScriptBuffer, :279

    const QJSValue value = out.property(QStringLiteral("v"));
    if (value.isUndefined() || value.isNull()) { // no tile here
        return std::nullopt;
    }
    QByteArray url;
    QByteArray requestHeader = cfg.requestHeader;
    QByteArray postData;
    if (value.isString()) {
        url = value.toString().toUtf8();
    } else if (value.isObject() && value.property(QStringLiteral("url")).isString()) {
        url = value.property(QStringLiteral("url")).toString().toUtf8();
        const QJSValue headers = value.property(QStringLiteral("headers"));
        if (headers.isObject()) {
            requestHeader = objectToHeaders(headers); // replaces the request headers
        } else if (!headers.isUndefined() && !headers.isNull()) {
            throw std::runtime_error("getUrl: headers must be an object");
        }
        const QJSValue post = value.property(QStringLiteral("postData"));
        if (!post.isUndefined() && !post.isNull()) {
            postData = post.toString().toUtf8(); // a non-empty one makes it a POST (:257-277)
        }
    } else {
        throw std::runtime_error(kResultText);
    }
    if (url.isEmpty()) { // :256
        return std::nullopt;
    }
    return makeRequest(std::move(url), std::move(requestHeader), std::move(postData), *settings_, checker_,
                       source);
}

QString TileDownloadRequestBuilderJs::templateToUrl(const QString& tmpl) {
    return urlTemplate_->templateToUrl(tmpl);
}

JsHttpReply TileDownloadRequestBuilderJs::httpRequest(const QByteArray& url, const QByteArray& requestHeader,
                                                      const QByteArray& postData) {
    // Sandbox (Q-8; D-143): a file:/// URL is not read, the call answers status 0 with empty header and body.
    if (downloader_ == nullptr || net::filePathFromUrl(url).has_value()) {
        if (downloader_ != nullptr && !fileUrlWarned_) {
            fileUrlWarned_ = true;
            qCWarning(logMapType).noquote()
                << "script file reads are disabled (http" << QString::fromUtf8(url) << ")";
        }
        return {};
    }
    net::SimpleHttpResponse r =
        net::simpleHttpRequest(*downloader_, settings_->inet, url, requestHeader, postData, cancel_);
    return {r.status, std::move(r.responseHeader), std::move(r.responseData)};
}

void TileDownloadRequestBuilderJs::log(const QString& text) {
    qCDebug(logMapType).noquote() << "script:" << text;
}

qint32 TileDownloadRequestBuilderJs::random(qint32 upperExclusive) {
    if (upperExclusive <= 0) { // System.Random(X): X <= 0 gives 0
        return 0;
    }
    return static_cast<qint32>(QRandomGenerator::global()->bounded(upperExclusive));
}

} // namespace cg::maptype
