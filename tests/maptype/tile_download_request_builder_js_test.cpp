// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: T-0225 (NEW code, no FEAT id): the JavaScript URL builder of *.source.json, ADR-0014, spec
// map-source-json.md 5.

#include "cg/maptype/tile_download_request_builder_factory_js.h"
#include "cg/maptype/tile_download_request_builder_js.h"
#include "cg/net/download_stats.h"

#include <QFile>
#include <QJSEngine>
#include <QStringList>
#include <QUrl>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <latch>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "maptype_test_util.h"
#include "request_builder_test_util.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using cg::maptype::ITileDownloadRequestBuilder;
using cg::maptype::ITileDownloadRequestBuilderPtr;
using cg::maptype::TileDownloadRequestBuilderFactoryJs;
using cg::maptype::TileDownloadRequestBuilderJs;
using cg::maptype::test::FactoryEnv;
using cg::maptype::test::makeBuilderConfig;
using cg::maptype::test::notFoundReply;
using cg::maptype::test::okReply;
using cg::maptype::test::SandboxLogCapture;
using cg::maptype::test::ScriptedDownloader;
using cg::maptype::test::tileRequest;
using cg::net::LastResponseInfo;
using cg::net::Method;

namespace {

const std::shared_ptr<LastResponseInfo> kNoInfo;
constexpr auto kResultText = "getUrl must return a string or { url: string }";
constexpr auto kLimitText = "script time limit exceeded";

struct Opts {
    QByteArray url = "http://h/{z}/{x}/{y}";
    QByteArray header;
    bool allowHttp = false;
    std::chrono::milliseconds limit = 5000ms;

    Opts& withUrl(QByteArray v) {
        url = std::move(v);
        return *this;
    }
    Opts& withHeader(QByteArray v) {
        header = std::move(v);
        return *this;
    }
    Opts& withHttp(bool v) {
        allowHttp = v;
        return *this;
    }
    Opts& withLimit(std::chrono::milliseconds v) {
        limit = v;
        return *this;
    }
};

class JsBuilderTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { cg::maptype::test::ensureGuiApp(); }

    std::shared_ptr<TileDownloadRequestBuilderFactoryJs> makeFactory(const QByteArray& script,
                                                                     const Opts& o = Opts()) {
        return std::make_shared<TileDownloadRequestBuilderFactoryJs>(
            u"t.source.json"_s, script, makeBuilderConfig(o.url, o.header, o.allowHttp), env.downloaderConfig,
            env.projectionSet, env.checker, env.lang, o.limit);
    }
    ITileDownloadRequestBuilderPtr makeBuilder(const QByteArray& script, const Opts& o = Opts()) {
        auto builder = makeFactory(script, o)->buildRequestBuilder(downloader);
        EXPECT_NE(builder, nullptr);
        return builder;
    }
    // The URL of tile (x, 1, 3) through a builder; fails the test when there is no request.
    static std::string urlOf(ITileDownloadRequestBuilder& b, int x = 0, int zoom = 3) {
        const auto request = b.buildRequest(tileRequest(x, 1, zoom), kNoInfo, {});
        EXPECT_TRUE(request.has_value());
        return request ? request->url.toStdString() : std::string("<no request>");
    }
    static std::string errorOf(ITileDownloadRequestBuilder& b, int x = 0, int zoom = 3) {
        try {
            (void)b.buildRequest(tileRequest(x, 1, zoom), kNoInfo, {});
        } catch (const std::runtime_error& e) {
            return e.what();
        }
        return "<no error>";
    }
    // One script that returns 'u:' + <expression>: the expression's text is the answer.
    std::string eval(const std::string& expression, const Opts& o = Opts()) {
        const QByteArray script =
            QByteArray::fromStdString("function getUrl(tile, ctx) { return 'u:' + (" + expression + "); }");
        auto builder = makeBuilder(script, o);
        return builder ? urlOf(*builder) : std::string("<no builder>");
    }
    static std::string disableReason(const std::shared_ptr<TileDownloadRequestBuilderFactoryJs>& f) {
        return f->state()->getStatic()->disableReason.toStdString();
    }
    [[nodiscard]] static bool fixtureFile(const QString& rel, QByteArray& out) {
        QFile f(cg::maptype::test::fixtureDir() + u"/maptype/json/"_s + rel);
        if (!f.open(QIODevice::ReadOnly)) {
            return false;
        }
        out = f.readAll();
        return true;
    }

    FactoryEnv env;
    std::shared_ptr<ScriptedDownloader> downloader = std::make_shared<ScriptedDownloader>();
};

// Calls f on a new thread and joins it (a thread that ends before the next call: the idle-trim case).
template <class F> void onNewThread(F&& f) {
    std::thread t(std::forward<F>(f));
    t.join();
}

double hexDouble(const QString& text) {
    return std::bit_cast<double>(static_cast<std::uint64_t>(text.mid(2).toULongLong(nullptr, 16)));
}

// --- the contract of getUrl (spec 5.1)
// ----------------------------------------------------------------------

TEST_F(JsBuilderTest, StringReturnIsAGetWithTheConfigHeaders) {
    auto factory = makeFactory("function getUrl(tile, ctx) { return ctx.urlBase + '/' + tile.z; }",
                               Opts().withUrl("http://h/base").withHeader("X-A: 1\r\nX-B: 2"));
    auto builder = factory->buildRequestBuilder(downloader);
    ASSERT_NE(builder, nullptr);
    const auto source = tileRequest(200, 300, 18);
    const auto request = builder->buildRequest(source, kNoInfo, {});
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->url, "http://h/base/18");
    EXPECT_EQ(request->requestHeader, "X-A: 1\r\nX-B: 2");
    EXPECT_EQ(request->method, Method::Get);
    EXPECT_TRUE(request->postData.isEmpty());
    EXPECT_EQ(request->inet, env.downloaderConfig->settings()->inet);
    EXPECT_EQ(request->checker, env.checker);
    EXPECT_EQ(static_cast<const cg::maptype::ITileRequest*>(request->source.get()), source.get());
    EXPECT_TRUE(factory->state()->getStatic()->enabled);
}

TEST_F(JsBuilderTest, ObjectReturnReplacesTheHeadersAndPostDataMakesAPost) {
    auto builder = makeBuilder("function getUrl(tile, ctx) { return { url: 'http://p/Город', headers: { "
                               "'B-2': 'Значение', 'A-1': '1' },"
                               " postData: 'тело=1' }; }",
                               Opts().withHeader("Config: header"));
    const auto request = builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, {});
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->method, Method::Post);
    EXPECT_EQ(request->url, QString::fromUtf8("http://p/Город").toUtf8());
    EXPECT_EQ(request->postData, QString::fromUtf8("тело=1").toUtf8());
    // the script's headers replace the config's; the order of the object is kept
    EXPECT_EQ(request->requestHeader, QString::fromUtf8("B-2: Значение\r\nA-1: 1").toUtf8());
}

TEST_F(JsBuilderTest, ObjectWithoutPostDataIsAGetAndKeepsTheConfigHeadersWhenItGivesNone) {
    struct Case {
        const char* object;
        const char* header;
    };
    const Case cases[] = {
        {"{ url: 'http://g/1' }", "Config: header"},
        {"{ url: 'http://g/1', postData: '' }", "Config: header"},
        {"{ url: 'http://g/1', postData: null, headers: null }", "Config: header"},
        {"{ url: 'http://g/1', headers: { 'X': 'y' } }", "X: y"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.object);
        auto builder = makeBuilder(QByteArray("function getUrl(tile, ctx) { return ") + c.object + "; }",
                                   Opts().withHeader("Config: header"));
        const auto request = builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, {});
        ASSERT_TRUE(request.has_value());
        EXPECT_EQ(request->method, Method::Get);
        EXPECT_EQ(request->url, "http://g/1");
        EXPECT_EQ(request->requestHeader, c.header);
    }
}

TEST_F(JsBuilderTest, NullUndefinedAndEmptyAreNoTileNotAnError) {
    for (const char* body :
         {"return null;", "return undefined;", "return '';", "return { url: '' };", "var x = 1;"}) {
        SCOPED_TRACE(body);
        auto builder = makeBuilder(QByteArray("function getUrl(tile, ctx) { ") + body + " }");
        EXPECT_FALSE(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, {}).has_value());
    }
}

TEST_F(JsBuilderTest, ThrowFailsTheTileWithTheMessageAndLineAndTheNextTileRuns) {
    auto builder = makeBuilder("function getUrl(tile, ctx) {\n"
                               "  if (tile.x === 1) {\n"
                               "    throw Error('boom');\n"
                               "  }\n"
                               "  if (tile.x === 2) { throw 'plain text'; }\n"
                               "  if (tile.x === 3) { return null.field; }\n"
                               "  return 'u' + tile.x;\n"
                               "}\n");
    EXPECT_EQ(errorOf(*builder, 1), "boom at line 3");
    EXPECT_EQ(errorOf(*builder, 2), "plain text"); // not an Error object: no line to report
    EXPECT_NE(errorOf(*builder, 3).find("null"), std::string::npos);
    EXPECT_TRUE(errorOf(*builder, 3).ends_with(" at line 6")) << errorOf(*builder, 3);
    EXPECT_EQ(urlOf(*builder, 4), "u4");
}

// An error the host raises (the prelude throws it) has no line of the script: from getUrl, and from top-level
// code further down than any line of the prelude.
TEST_F(JsBuilderTest, ErrorOfTheHostCarriesNoLine) {
    auto builder = makeBuilder("function getUrl(tile, ctx) {\n"
                               "  shared.locked(function () { shared.locked(function () {}); });\n"
                               "}");
    EXPECT_EQ(errorOf(*builder), "shared.locked is not re-entrant");
    std::string script;
    for (int i = 0; i < 60; ++i) {
        script += "var filler" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    }
    auto factory = makeFactory(QByteArray::fromStdString(script + "template('{x}');\nfunction getUrl() {}"));
    EXPECT_EQ(factory->buildRequestBuilder(downloader), nullptr);
    EXPECT_EQ(disableReason(factory), "template is only available inside getUrl");
}

// Qt 6.12 gives the RangeError of a stack overflow a meaningless lineNumber (about a billion), Qt 6.4 gives
// the line of the recursion: either no line is reported or one that lies inside the two-line script.
TEST_F(JsBuilderTest, RunawayRecursionIsAnOrdinaryErrorWithoutAMeaninglessLine) {
    auto builder = makeBuilder(
        "function f(n) { return f(n + 1) + 1; }\nfunction getUrl(tile, ctx) { return 'u' + f(0); }");
    const std::string text = errorOf(*builder);
    EXPECT_NE(text.find("stack"), std::string::npos) << text; // the engine's RangeError
    const std::size_t at = text.find(" at line ");
    if (at != std::string::npos) {
        const int line = std::stoi(text.substr(at + 9));
        EXPECT_GE(line, 1) << text;
        EXPECT_LE(line, 2) << text;
    }
}

TEST_F(JsBuilderTest, AnyOtherReturnTypeIsARuntimeError) {
    for (const char* value :
         {"42", "true", "{}", "{ url: 5 }", "{ url: null }", "[ 'http://a' ]", "function () {}"}) {
        SCOPED_TRACE(value);
        auto builder = makeBuilder(QByteArray("function getUrl(tile, ctx) { return ") + value + "; }");
        EXPECT_EQ(errorOf(*builder), kResultText);
    }
    auto builder = makeBuilder("function getUrl(tile, ctx) { return { url: 'x', headers: 'A: b' }; }");
    EXPECT_EQ(errorOf(*builder), "getUrl: headers must be an object");
}

TEST_F(JsBuilderTest, GetUrlReplacedByANonFunctionAfterTheCheckFailsTheTile) {
    auto builder = makeBuilder("function getUrl(tile, ctx) { getUrl = 5; return 'first'; }");
    EXPECT_EQ(urlOf(*builder), "first");
    EXPECT_EQ(errorOf(*builder), "function getUrl(tile, ctx) is not defined");
}

// --- the inputs of getUrl (spec 5.2)
// --------------------------------------------------------------------------

// The four golden tile points of tests/script/golden_url_test.cpp, with the Pascal inputs (GetLLon ..
// GetBMetr) as the hand-derived doubles of the golden file; z of the golden is GetZ, so the zoom is z - 1.
TEST_F(JsBuilderTest, TileObjectCarriesTheGoldenBoundsOfTheFourGoldenPoints) {
    const QString path = cg::maptype::test::fixtureDir() + u"/script-goldens/Lantmateriet.se.zmp.golden"_s;
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << path.toStdString();
    std::map<QString, std::map<QString, QString>> sections;
    QString section;
    for (const QByteArray& raw : file.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(u'[')) {
            section = line.mid(1, line.size() - 2);
        } else if (!line.startsWith(u';') && line.contains(u'=')) {
            sections[section][line.left(line.indexOf(u'='))] = line.mid(line.indexOf(u'=') + 1);
        }
    }
    auto builder = makeBuilder(
        "function getUrl(tile, ctx) { return [tile.x, tile.y, tile.z, tile.zoom1, tile.lonLeft, tile.latTop, "
        "tile.lonRight, tile.latBottom, tile.mLeft, tile.mTop, tile.mRight, tile.mBottom].join('|'); }");
    int points = 0;
    for (const char* name : {"P1", "P2", "P3", "P4"}) {
        SCOPED_TRACE(name);
        const auto& g = sections.at(QString::fromLatin1(name));
        const int zoom1 = g.at(u"GetZ"_s).toInt();
        const auto request = builder->buildRequest(
            tileRequest(g.at(u"GetX"_s).toInt(), g.at(u"GetY"_s).toInt(), zoom1 - 1), kNoInfo, {});
        ASSERT_TRUE(request.has_value());
        const QStringList v = QString::fromUtf8(request->url).split(u'|');
        ASSERT_EQ(v.size(), 12);
        EXPECT_EQ(v[0].toInt(), g.at(u"GetX"_s).toInt());
        EXPECT_EQ(v[1].toInt(), g.at(u"GetY"_s).toInt());
        EXPECT_EQ(v[2].toInt(), zoom1 - 1);
        EXPECT_EQ(v[3].toInt(), zoom1);
        const char* keys[] = {"GetLLon",  "GetTLat",  "GetRLon",  "GetBLat",
                              "GetLMetr", "GetTMetr", "GetRMetr", "GetBMetr"};
        for (int i = 0; i < 8; ++i) {
            const double expected = hexDouble(g.at(QString::fromLatin1(keys[i])));
            EXPECT_NEAR(v[4 + i].toDouble(), expected, 1e-9 * std::max(1.0, std::abs(expected))) << keys[i];
        }
        ++points;
    }
    EXPECT_EQ(points, 4);
}

TEST_F(JsBuilderTest, ContextCarriesBaseVersionLanguageHeadersAndPreviousResponse) {
    env.lang->setCurrentLanguageCode(u"ru"_s);
    auto builder =
        makeBuilder("function getUrl(tile, ctx) { return 'u:' + JSON.stringify([ctx.urlBase, ctx.version,"
                    " ctx.lang, ctx.requestHeaders, ctx.lastResponseHeaders, ctx.state]); }",
                    Opts()
                        .withUrl(QString::fromUtf8("http://h/Город/{z}").toUtf8())
                        .withHeader("X-A: 1\r\nX-B:2: x\r\n\r\nNoColon"));
    auto info = std::make_shared<LastResponseInfo>();
    info->setResponseHead("Head-A\r\nB: 2");
    const auto version = std::make_shared<const cg::storage::MapVersionInfo>(u"Вер 7"_s, 1);
    const auto withVersion = std::make_shared<const cg::maptype::TileRequest>(QPoint(1, 1), 1, version);
    const auto request = builder->buildRequest(withVersion, info, {});
    ASSERT_TRUE(request.has_value());
    // a line is split at its first colon and trimmed; an empty line and a line without a colon are left out
    EXPECT_EQ(QString::fromUtf8(request->url),
              QString::fromUtf8(
                  R"(u:["http://h/Город/{z}","Вер 7","ru",{"X-A":"1","X-B":"2: x"},"Head-A\r\nB: 2",""])"));
    // no version object and no previous response: both are ""
    EXPECT_EQ(QString::fromUtf8(builder->buildRequest(tileRequest(1, 1, 1), kNoInfo, {})->url),
              QString::fromUtf8(R"(u:["http://h/Город/{z}","","ru",{"X-A":"1","X-B":"2: x"},"",""])"));
}

TEST_F(JsBuilderTest, LanguageChangeIsPickedUpByTheNextCall) {
    env.lang->setCurrentLanguageCode(u"en"_s);
    auto builder = makeBuilder("function getUrl(tile, ctx) { return 'u:' + ctx.lang; }");
    EXPECT_EQ(urlOf(*builder), "u:en");
    env.lang->setCurrentLanguageCode(u"de"_s);
    EXPECT_EQ(urlOf(*builder), "u:de");
}

TEST_F(JsBuilderTest, RequestHeadersObjectIsAFreshCopyPerCall) {
    auto builder = makeBuilder("function getUrl(tile, ctx) { var seen = "
                               "Object.keys(ctx.requestHeaders).join(','); ctx.requestHeaders.Extra = 'x'; "
                               "return 'u:' + seen; }",
                               Opts().withHeader("X-A: 1"));
    EXPECT_EQ(urlOf(*builder), "u:X-A");
    EXPECT_EQ(urlOf(*builder), "u:X-A");
}

TEST_F(JsBuilderTest, StateSurvivesCallsAndAnErrorKeepsTheOldValue) {
    auto builder = makeBuilder("function getUrl(tile, ctx) {\n"
                               "  var before = ctx.state;\n"
                               "  ctx.state = before + 'x';\n"
                               "  if (tile.x === 9) { throw Error('after the write'); }\n"
                               "  return 'u:' + before + '>' + ctx.state;\n"
                               "}");
    EXPECT_EQ(urlOf(*builder, 0), "u:>x");
    EXPECT_EQ(urlOf(*builder, 0), "u:x>xx");
    EXPECT_EQ(errorOf(*builder, 9), "after the write at line 4");
    EXPECT_EQ(urlOf(*builder, 0), "u:xx>xxx"); // the failed call did not store its write
    auto other = makeBuilder("function getUrl(tile, ctx) { ctx.state += 'y'; return 'u:' + ctx.state; }");
    EXPECT_EQ(urlOf(*other), "u:y"); // the state belongs to one builder (one slot)
}

TEST_F(JsBuilderTest, TemplateRendersTheTagLanguageForTheCurrentTile) {
    auto builder = makeBuilder(
        "function getUrl(tile, ctx) { return template(ctx.urlBase) + '|' + template('{x}-{y}-{z}'); }",
        Opts().withUrl("http://h/{z}/{x}/{y}.png"));
    const auto request = builder->buildRequest(tileRequest(3, 5, 4), kNoInfo, {});
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->url, "http://h/4/3/5.png|3-5-4");
}

TEST_F(JsBuilderTest, CancelledBeforeTheCallGivesNoRequestAndRunsNoScript) {
    auto builder = makeBuilder("function getUrl(tile, ctx) { return 'u'; }");
    std::stop_source cancelled;
    cancelled.request_stop();
    EXPECT_FALSE(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, cancelled.get_token()).has_value());
    std::stop_source live;
    EXPECT_TRUE(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, live.get_token()).has_value());
}

TEST_F(JsBuilderTest, ZoomOutsideTheProjectionIsTheListIndexError) {
    auto builder = makeBuilder("function getUrl(tile, ctx) { return 'u'; }");
    try {
        (void)builder->buildRequest(tileRequest(1, 2, 250), kNoInfo, {});
        FAIL() << "no exception";
    } catch (const std::out_of_range& e) {
        EXPECT_STREQ(e.what(), "List index out of bounds (250)");
    }
}

// --- http (spec 5.2; the Pascal Downloader of :346-357, u_SimpleHttpDownloader.pas:81-124)
// ----------------------

TEST_F(JsBuilderTest, HttpIsDefinedOnlyWithAllowHttpAndADownloader) {
    const std::string probe = "typeof http";
    EXPECT_EQ(eval(probe), "u:undefined");
    EXPECT_EQ(eval(probe, Opts().withHttp(true)), "u:object");
    // allowHttp without a downloader is Pascal's nil Downloader: the name is not defined
    auto noDownloader =
        makeFactory("function getUrl(tile, ctx) { return 'u:' + typeof http; }", Opts().withHttp(true))
            ->buildRequestBuilder(nullptr);
    EXPECT_EQ(urlOf(*noDownloader), "u:undefined");
}

TEST_F(JsBuilderTest, HttpGetAndPostGoThroughTheSlotsDownloaderWithTheLegacyReplyShape) {
    downloader->replies.push_back(
        okReply(200, "Content-Type: x\r\nX-H: 1", QString::fromUtf8("Тело").toUtf8()));
    downloader->replies.push_back(notFoundReply());
    downloader->replies.push_back(okReply(201, "H: 3", "posted"));
    downloader->replies.push_back(okReply(200, "H: 4", "empty-post-is-get"));
    auto builder =
        makeBuilder("function getUrl(tile, ctx) {\n"
                    "  var a = http.get('http://d/a', { 'X-1': 'one', 'X-2': 'два' });\n"
                    "  var b = http.get('http://d/b');\n"
                    "  var c = http.post('http://d/c', 'k=Ключ', { 'Content-Type': 'text/plain' });\n"
                    "  var d = http.post('http://d/d', '');\n"
                    "  return 'u:' + JSON.stringify([a, b, c, d]);\n"
                    "}",
                    Opts().withHttp(true));
    // the reply object has the three members of the spec (the engine lists them in alphabetical order)
    EXPECT_EQ(urlOf(*builder), R"(u:[{"body":"Тело","headers":"Content-Type: x\r\nX-H: 1","status":200},)"
                               R"({"body":"","headers":"","status":404},)"
                               R"({"body":"posted","headers":"H: 3","status":201},)"
                               R"({"body":"empty-post-is-get","headers":"H: 4","status":200}])");
    ASSERT_EQ(downloader->requests.size(), 4u);
    EXPECT_EQ(downloader->requests[0].url, "http://d/a");
    EXPECT_EQ(downloader->requests[0].method, Method::Get);
    EXPECT_EQ(downloader->requests[0].requestHeader, QString::fromUtf8("X-1: one\r\nX-2: два").toUtf8());
    EXPECT_EQ(downloader->requests[0].inet, env.downloaderConfig->settings()->inet);
    EXPECT_EQ(downloader->requests[1].requestHeader, "");
    EXPECT_EQ(downloader->requests[2].method, Method::Post);
    EXPECT_EQ(downloader->requests[2].postData, QString::fromUtf8("k=Ключ").toUtf8());
    EXPECT_EQ(downloader->requests[2].requestHeader, "Content-Type: text/plain");
    EXPECT_EQ(downloader->requests[3].method, Method::Get); // an empty body sends a GET
    EXPECT_TRUE(downloader->requests[3].postData.isEmpty());
}

TEST_F(JsBuilderTest, HttpRefusesFileUrlsWithStatusZeroAndOneWarningPerBuilder) {
    downloader->replies.push_back(okReply(200, "H: 1", "netbody"));
    auto factory = makeFactory(
        "function getUrl(tile, ctx) {\n"
        "  var f = http.get('file:///etc/hostname'), g = http.post('FILE:///etc/hostname', 'x');\n"
        "  var n = http.get('http://d/');\n"
        "  return 'u:' + [f.status, f.headers, f.body, g.status, n.status, n.body].join('|');\n"
        "}",
        Opts().withHttp(true));
    SandboxLogCapture capture;
    auto first = factory->buildRequestBuilder(downloader);
    EXPECT_EQ(urlOf(*first), "u:0|||0|200|netbody");
    ASSERT_EQ(downloader->requests.size(), 1u); // only the http:// call reached the transport
    EXPECT_EQ(downloader->requests[0].url, "http://d/");
    EXPECT_EQ(urlOf(*first), "u:0|||0|0|"); // the queue is empty now: no answer, status 0
    EXPECT_EQ(capture.countContaining("script file reads are disabled"), 1);
    auto second = factory->buildRequestBuilder(downloader);
    (void)urlOf(*second);
    EXPECT_EQ(capture.countContaining("script file reads are disabled"), 2);
}

// Spec 6.4: one token request for 50 tiles over 4 workers (the lock holds the others back until the token is
// shared).
TEST_F(JsBuilderTest, TokenExampleFetchesOneTokenFor50TilesOver4Workers) {
    QByteArray code;
    ASSERT_TRUE(fixtureFile(u"example-token.js"_s, code));
    downloader->replies.push_back(okReply(200, "", R"({"access_token":"TKN"})"));
    auto factory =
        makeFactory(code, Opts().withUrl("https://tiles.example.org/{z}/{x}/{y}.png").withHttp(true));
    std::latch started(4);
    downloader->onRequest = [&] {
        started.wait();
    }; // the fetch waits until all four workers are on their way
    std::mutex mutex;
    std::set<std::string> urls;
    std::set<std::string> headers;
    std::vector<std::thread> workers;
    for (int w = 0; w < 4; ++w) {
        workers.emplace_back([&, w] {
            auto builder = factory->buildRequestBuilder(downloader);
            started.count_down();
            for (int i = w; i < 50; i += 4) {
                const auto request = builder->buildRequest(tileRequest(i, 7, 10), kNoInfo, {});
                const std::scoped_lock lock(mutex);
                if (request.has_value()) {
                    urls.insert(request->url.toStdString());
                    headers.insert(request->requestHeader.toStdString());
                }
            }
        });
    }
    for (auto& t : workers) {
        t.join();
    }
    ASSERT_EQ(downloader->requests.size(), 1u);
    EXPECT_EQ(downloader->requests[0].url, "https://tiles.example.org/api/token");
    EXPECT_EQ(downloader->requests[0].requestHeader, "Referer: https://example.org/");
    ASSERT_EQ(urls.size(), 50u);
    EXPECT_EQ(*urls.begin(), "https://tiles.example.org/10/0/7.png");
    EXPECT_TRUE(urls.contains("https://tiles.example.org/10/49/7.png"));
    EXPECT_EQ(headers, std::set<std::string>{"Authorization: Bearer TKN"});
}

// --- limits (ADR-0014 point 5)
// ---------------------------------------------------------------------------------

TEST_F(JsBuilderTest, DefaultScriptTimeIsFiveSeconds) {
    EXPECT_EQ(TileDownloadRequestBuilderFactoryJs::kDefaultTimeLimit, 5000ms);
}

TEST_F(JsBuilderTest, EndlessLoopStopsNearTheLimitAndTheNextCallOfTheBuilderSucceeds) {
    auto builder =
        makeBuilder("function getUrl(tile, ctx) { ctx.state = 'changed'; if (tile.x === 0) { for (;;) {} }"
                    " return 'u' + tile.x + ctx.state; }",
                    Opts().withLimit(200ms));
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(errorOf(*builder, 0), kLimitText);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    EXPECT_GE(elapsed, 190ms);
    EXPECT_LE(elapsed, 700ms);
    EXPECT_EQ(urlOf(*builder, 1),
              "u1changed"); // the interrupt flag was cleared; the failed call kept no state
    EXPECT_EQ(errorOf(*builder, 0), kLimitText); // and the limit applies to every call
}

TEST_F(JsBuilderTest, SlowHttpReplyAndWaitForTheLockAreNotScriptTime) {
    auto factory = makeFactory("function getUrl(tile, ctx) {\n"
                               "  if (tile.x === 0) { return shared.locked(function () { return 'a' + "
                               "http.get('http://slow/').status; }); }\n"
                               "  if (tile.x === 1) { return shared.locked(function () { return 'b'; }); }\n"
                               "  return 'c' + http.get('http://slow/').status;\n"
                               "}",
                               Opts().withHttp(true).withLimit(200ms));
    std::atomic<int> phase{0};
    std::latch holderEntered(1);
    downloader->onRequest = [&] {
        if (phase.load() == 1) {
            holderEntered.count_down();
        }
        std::this_thread::sleep_for(600ms); // the server is slow: three times the limit
    };
    downloader->replies.push_back(okReply(200));
    downloader->replies.push_back(okReply(200));
    // 1. the slow reply itself
    auto plain = factory->buildRequestBuilder(downloader);
    EXPECT_EQ(urlOf(*plain, 2), "c200");
    // 2. a second worker waits for the lock that the first one holds during its slow reply
    phase = 1;
    auto holder = factory->buildRequestBuilder(downloader);
    auto waiter = factory->buildRequestBuilder(downloader);
    std::string held;
    std::thread holding([&] { held = urlOf(*holder, 0); });
    holderEntered.wait();
    const auto start = std::chrono::steady_clock::now();
    const std::string waited = urlOf(*waiter, 1);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    holding.join();
    EXPECT_EQ(held, "a200");
    EXPECT_EQ(waited, "b");
    EXPECT_GE(elapsed,
              250ms); // the wait really overlapped the holder's slow reply, and was longer than the limit
}

// --- sandbox (ADR-0014 point 4)
// --------------------------------------------------------------------------------

TEST_F(JsBuilderTest, NoRequireXhrQtConsolePrintOrGcExists) {
    EXPECT_EQ(
        eval("[typeof require, typeof XMLHttpRequest, typeof Qt, typeof console, typeof print, typeof gc, "
             "typeof qsTr, typeof importModule, typeof WebSocket].join()"),
        "u:undefined,undefined,undefined,undefined,undefined,undefined,undefined,undefined,undefined");
}

TEST_F(JsBuilderTest, GlobalObjectHoldsOnlyTheBuiltinsGetUrlAndTheHostNames) {
    QJSEngine plain;
    const QStringList baseline =
        plain.evaluate(u"Object.getOwnPropertyNames((function () { return this; })())"_s)
            .toVariant()
            .toStringList();
    ASSERT_FALSE(baseline.isEmpty());
    for (const bool allowHttp : {false, true}) {
        SCOPED_TRACE(allowHttp);
        auto builder =
            makeBuilder("function getUrl(tile, ctx) { return 'u:' + "
                        "Object.getOwnPropertyNames((function () { return this; })()).join('|'); }",
                        Opts().withHttp(allowHttp));
        QStringList names = QString::fromStdString(urlOf(*builder)).mid(2).split(u'|');
        QStringList extra;
        for (const QString& n : names) {
            if (!baseline.contains(n)) {
                extra.append(n);
            }
        }
        extra.sort();
        QStringList expected = {u"getUrl"_s, u"log"_s, u"pascal"_s, u"shared"_s, u"template"_s};
        if (allowHttp) {
            expected.append(u"http"_s);
            expected.sort();
        }
        EXPECT_EQ(extra, expected);
    }
}

TEST_F(JsBuilderTest, HostObjectsAreFrozenAndTheirNamesCannotBeReassigned) {
    EXPECT_EQ(eval("(function () {\n"
                   "  var frozen = [shared, pascal, http, template, log].map(Object.isFrozen).join();\n"
                   "  shared.get = 1; pascal.round = 1; http.get = 1; delete shared.set; shared.extra = 1;\n"
                   "  shared = 5; pascal = 6; template = 7; http = 8;\n"
                   "  return frozen + '|' + [typeof shared.get, typeof shared.set, typeof shared.extra, "
                   "typeof pascal.round,"
                   " typeof http.get, typeof template].join();\n"
                   "})()",
                   Opts().withHttp(true)),
              "u:true,true,true,true,true|function,function,undefined,function,function,function");
    EXPECT_EQ(eval("Object.getOwnPropertyNames(shared).sort().join() + '|' + "
                   "Object.getOwnPropertyNames(pascal).sort().join()"),
              "u:get,locked,set|div,mod,random,randomRange,regExprGetMatchSubStr,regExprReplaceMatchSubStr,"
              "round,roundEx");
}

TEST_F(JsBuilderTest, StaticImportAndDynamicImportOfAnExistingFileDisableTheSource) {
    QFile file(env.tmp.path() + u"/module.mjs"_s);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("export const marker = 'file was read';\n");
    file.close();
    const QByteArray url = QUrl::fromLocalFile(file.fileName()).toString().toUtf8();
    for (const QByteArray& script :
         {QByteArray("import x from 'module.mjs';\nfunction getUrl(tile, ctx) { return 'u'; }"),
          QByteArray("var p = import('") + url + "');\nfunction getUrl(tile, ctx) { return 'u'; }",
          QByteArray("function getUrl(tile, ctx) { import('") + url + "'); return 'u'; }"}) {
        SCOPED_TRACE(script.toStdString());
        auto factory = makeFactory(script);
        const bool hasBuilder = factory->buildRequestBuilder(downloader) != nullptr;
        // a Qt that accepted `import(...)` would still have to refuse to read the file: it resolves nothing
        // here
        EXPECT_FALSE(hasBuilder) << disableReason(factory);
        EXPECT_FALSE(factory->state()->getStatic()->enabled);
    }
}

// --- the factory's check (ADR-0014 point 6)
// ------------------------------------------------------------------

TEST_F(JsBuilderTest, ScriptThatFailsTheCheckDisablesTheSourceAndNoBuilderRunsIt) {
    struct Case {
        const char* script;
        bool allowHttp;
        const char* reason; // the whole text, or a part of it when `partial`
        bool partial;
    };
    const Case cases[] = {
        {"function getUrl(tile, ctx) { return 'x' ", false, "", true}, // syntax error: any text
        {"throw Error('top');\nfunction getUrl() {}", false, "top at line 1", false},
        {"throw 'top';\nfunction getUrl() {}", false, "top", false},
        {"throw 5;\nfunction getUrl() {}", false, "5", false},
        {"var a = 1;\nvar b = null.c;\nfunction getUrl() {}", false, " at line 2", true},
        {"for (;;) {}\nfunction getUrl() {}", false, kLimitText, false},
        {"var x = 1;", false, "function getUrl(tile, ctx) is not defined", false},
        {"var getUrl = 5;", false, "function getUrl(tile, ctx) is not defined", false},
        {"http.get('http://a/');\nfunction getUrl() {}", true, "http is only available inside getUrl", false},
        {"template('{x}');\nfunction getUrl() {}", false, "template is only available inside getUrl", false},
        {"http.get('http://a/');\nfunction getUrl() {}", false, "http",
         true}, // not defined without allowHttp
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.script);
        auto factory = makeFactory(c.script, Opts().withHttp(c.allowHttp).withLimit(200ms));
        EXPECT_EQ(factory->buildRequestBuilder(downloader), nullptr);
        const auto state = factory->state()->getStatic();
        EXPECT_FALSE(state->enabled);
        EXPECT_FALSE(state->disableReason.isEmpty());
        if (c.partial) {
            EXPECT_NE(disableReason(factory).find(c.reason), std::string::npos) << disableReason(factory);
        } else {
            EXPECT_EQ(disableReason(factory), c.reason);
        }
        EXPECT_EQ(factory->buildRequestBuilder(downloader), nullptr); // and it stays so
        EXPECT_TRUE(downloader->requests.empty());
    }
}

TEST_F(JsBuilderTest, TopLevelErrorOfALaterEngineFailsTheTileAndTheNextCallRebuilds) {
    // the check passes (first call); the second engine's top level then fails: the tile fails, no stale
    // engine stays
    auto factory = makeFactory("var n = (typeof shared.get('mark') === 'undefined') ? 0 : 1;\n"
                               "shared.set('mark', 'x');\n"
                               "if (n === 1) { throw Error('second boot'); }\n"
                               "function getUrl(tile, ctx) { return 'u'; }\n");
    auto first = factory->buildRequestBuilder(downloader);
    ASSERT_NE(first, nullptr);     // the check ran on a store of its own: no mark yet
    EXPECT_EQ(urlOf(*first), "u"); // sets mark in the real store
    auto second = factory->buildRequestBuilder(downloader);
    EXPECT_EQ(errorOf(*second), "second boot at line 3");
    EXPECT_EQ(errorOf(*second), "second boot at line 3"); // a failed engine is not kept
}

// --- shared (spec 5.2)
// -----------------------------------------------------------------------------------------

TEST_F(JsBuilderTest, SharedStoreBelongsToTheSourceAndConvertsValuesToStrings) {
    const QByteArray script =
        "function getUrl(tile, ctx) {\n"
        "  if (tile.x === 1) { shared.set(12, 3.5); shared.set('flag', true); return 'u:set'; }\n"
        "  return 'u:' + [typeof shared.get('12'), shared.get(12), shared.get('flag'), typeof "
        "shared.get('absent')].join();\n"
        "}";
    auto factory = makeFactory(script);
    auto a = factory->buildRequestBuilder(downloader);
    auto b = factory->buildRequestBuilder(downloader);
    EXPECT_EQ(urlOf(*b, 0), "u:undefined,,,undefined");
    EXPECT_EQ(urlOf(*a, 1), "u:set");
    EXPECT_EQ(urlOf(*b, 0), "u:string,3.5,true,undefined"); // seen by the other worker of the source
    auto other = makeFactory(script)->buildRequestBuilder(downloader);
    EXPECT_EQ(urlOf(*other, 0), "u:undefined,,,undefined"); // not by another source
}

class JsLockTest : public JsBuilderTest {
protected:
    // Builders 1 and 2 of one source; x selects the case. 0: nested locked, 1: plain locked, 2: throw in fn,
    // 3: endless loop in fn.
    void SetUp() override {
        factory = makeFactory(
            "function getUrl(tile, ctx) {\n"
            "  if (tile.x === 0) {\n"
            "    try { shared.locked(function () { shared.locked(function () { return 1; }); }); }\n"
            "    catch (e) { return 'u:' + e.message; }\n"
            "  }\n"
            "  if (tile.x === 1) { return shared.locked(function () { return 'u:got'; }); }\n"
            "  if (tile.x === 2) {\n"
            "    try { shared.locked(function () { throw Error('inner'); }); } catch (e) { return 'u:' + "
            "e.message; }\n"
            "  }\n"
            "  if (tile.x === 3) { return shared.locked(function () { for (;;) {} }); }\n"
            "  if (tile.x === 4) { return shared.locked(function () { return 'u:7'; }); }\n"
            "}",
            Opts().withLimit(200ms));
        first = factory->buildRequestBuilder(downloader);
        second = factory->buildRequestBuilder(downloader);
    }
    // The second builder takes the lock on another thread; a lock that stayed held would block it for good.
    [[nodiscard]] bool secondCanTakeTheLock() {
        auto done = std::make_shared<std::promise<std::string>>();
        std::future<std::string> result = done->get_future();
        std::thread worker([done, builder = second] { done->set_value(JsBuilderTest::urlOf(*builder, 1)); });
        if (result.wait_for(10s) != std::future_status::ready) {
            worker.detach(); // the lock stayed held: the thread never ends, joining would hang the run
            return false;
        }
        worker.join(); // joined, so that the builder it used is not touched after the test (TSan)
        return result.get() == "u:got";
    }
    std::shared_ptr<TileDownloadRequestBuilderFactoryJs> factory;
    ITileDownloadRequestBuilderPtr first;
    ITileDownloadRequestBuilderPtr second;
};

TEST_F(JsLockTest, LockedInsideLockedIsAnErrorAndTheLockIsReleased) {
    EXPECT_EQ(urlOf(*first, 0), "u:shared.locked is not re-entrant");
    EXPECT_TRUE(secondCanTakeTheLock());
}

TEST_F(JsLockTest, ExceptionInsideTheFunctionPropagatesAndReleasesTheLock) {
    EXPECT_EQ(urlOf(*first, 2), "u:inner");
    EXPECT_TRUE(secondCanTakeTheLock());
}

TEST_F(JsLockTest, TimeLimitInsideTheFunctionReleasesTheLock) {
    EXPECT_EQ(errorOf(*first, 3), kLimitText);
    EXPECT_TRUE(secondCanTakeTheLock());
    EXPECT_EQ(urlOf(*first, 4), "u:7"); // and the first builder can take it again itself
}

// --- the engine and the threads (ADR-0014 point 3)
// -------------------------------------------------------------

TEST_F(JsBuilderTest, ThreadChangeRebuildsTheEngineKeepsTheStateAndLosesTopLevelVariables) {
    auto builder = makeBuilder("var counter = 0;\n"
                               "function getUrl(tile, ctx) { counter++; ctx.state += 'x'; return 'u:' + "
                               "counter + ':' + ctx.state; }");
    std::string a1, a2, b1, c1, c2;
    onNewThread([&] {
        a1 = urlOf(*builder);
        a2 = urlOf(*builder);
    });
    onNewThread([&] { b1 = urlOf(*builder); }); // A is gone: a new engine on this thread
    onNewThread([&] {
        c1 = urlOf(*builder);
        c2 = urlOf(*builder);
    });
    EXPECT_EQ(a1, "u:1:x");
    EXPECT_EQ(a2, "u:2:xx"); // same thread: the engine and its top-level variable stay
    EXPECT_EQ(b1, "u:1:xxx");
    EXPECT_EQ(c1, "u:1:xxxx");
    EXPECT_EQ(c2, "u:2:xxxxx");
}

TEST_F(JsBuilderTest, BuilderCreatedAndUsedOnOneThreadIsDestroyedOnAnother) {
    ITileDownloadRequestBuilderPtr builder;
    onNewThread([&] {
        builder = makeBuilder("function getUrl(tile, ctx) { return 'u:' + template('{x}'); }");
        EXPECT_EQ(urlOf(*builder, 5), "u:5");
    });
    builder.reset(); // the engine goes away on the test's thread
    SUCCEED();
}

// ADR-0014 "a few MiB per engine": the number goes to the log of the run (no assertion).
TEST_F(JsBuilderTest, MemoryOfFourBuildersWithATrivialScriptIsLogged) {
    QFile statm(u"/proc/self/statm"_s);
    if (!statm.open(QIODevice::ReadOnly)) {
        GTEST_SKIP() << "no /proc/self/statm";
    }
    auto residentPages = [] {
        QFile f(u"/proc/self/statm"_s);
        EXPECT_TRUE(f.open(QIODevice::ReadOnly));
        return QString::fromLatin1(f.readAll()).split(u' ').value(1).toLongLong();
    };
    auto factory = makeFactory("function getUrl(tile, ctx) { return 'u'; }");
    const qint64 before = residentPages();
    std::vector<ITileDownloadRequestBuilderPtr> builders;
    for (int i = 0; i < 4; ++i) {
        builders.push_back(factory->buildRequestBuilder(downloader));
        EXPECT_EQ(urlOf(*builders.back()), "u");
    }
    const qint64 growth = residentPages() - before;
    RecordProperty("rss_growth_pages_for_4_builders", std::to_string(growth));
    std::cout << "T-0225 RSS growth of 4 builders: " << growth
              << " pages (page = 4096 bytes on x86-64 and arm64 Linux)\n";
}

// --- the pascal helpers (spec 5.4)
// ---------------------------------------------------------------------------------

TEST_F(JsBuilderTest, PascalNumericHelpersFollowDelphi) {
    struct Case {
        const char* expression;
        const char* expected;
    };
    const Case cases[] = {
        {"[pascal.round(2.5), pascal.round(3.5), pascal.round(-2.5), pascal.round(0.5), pascal.round(1.5), "
         "pascal.round(-0.4)].join()",
         "2,4,-2,0,2,0"},
        {"[pascal.roundEx(1234.5678, 2), pascal.roundEx(3.14159, 4), pascal.roundEx(0 / 0, 2)].join()",
         "1234.57,3.1416,-"},
        {"[pascal.div(7, 2), pascal.div(-7, 2), pascal.div(7, -2), pascal.div(7.9, 2), "
         "pascal.div(2147483648, 1),"
         " pascal.div(-2147483648, -1)].join()",
         "3,-3,-3,3,-2147483648,-2147483648"},
        {"[pascal.mod(7, 2), pascal.mod(-7, 2), pascal.mod(7, -2), pascal.mod(-2147483648, -1)].join()",
         "1,-1,1,0"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.expression);
        EXPECT_EQ(eval(c.expression), std::string("u:") + c.expected);
    }
}

TEST_F(JsBuilderTest, PascalHelpersThrowTheirDocumentedErrors) {
    const char* wrapper =
        "(function (f) { try { f(); return 'no error'; } catch (e) { return e.name + ': ' + e.message; } })";
    struct Case {
        const char* call;
        const char* expected;
    };
    const Case cases[] = {
        {"pascal.div(1, 0)", "Error: Division by zero"},
        {"pascal.mod(1, 0)", "Error: Division by zero"},
        {"pascal.round('a')", "TypeError: pascal.round: numeric argument expected"},
        {"pascal.roundEx(1, undefined)", "TypeError: pascal.roundEx: numeric argument expected"},
        {"pascal.div(1, null)", "TypeError: pascal.div: numeric argument expected"},
        {"pascal.randomRange(1, '2')", "TypeError: pascal.randomRange: numeric argument expected"},
        {"pascal.regExprGetMatchSubStr('a', 'a', '0')",
         "TypeError: pascal.regExprGetMatchSubStr: numeric argument expected"},
        {"pascal.round(1 / 0)", "Error: pascal.round: argument out of range"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.call);
        EXPECT_EQ(eval(std::string(wrapper) + "(function () { " + c.call + "; })"),
                  std::string("u:") + c.expected);
    }
    // a pattern that does not compile throws an Error with the compile message (R16)
    EXPECT_EQ(eval(std::string(wrapper) +
                   "(function () { pascal.regExprGetMatchSubStr('a', '(', 0); }).slice(0, 7)"),
              "u:Error: ");
}

TEST_F(JsBuilderTest, PascalRegularExpressionHelpersBindTheTRegExprOnes) {
    struct Case {
        const char* expression;
        const char* expected;
    };
    const Case cases[] = {
        {R"([pascal.regExprGetMatchSubStr('a12/34b', '(\\d+)/(\\d+)', 2), pascal.regExprGetMatchSubStr('a12/34b', '(\\d+)/(\\d+)', 0),)"
         R"( pascal.regExprGetMatchSubStr('a12/34b', '(\\d+)/(\\d+)', 3), pascal.regExprGetMatchSubStr('abc', '\\d', 0)].join('|'))",
         "34|12/34||"},
        {R"(pascal.regExprGetMatchSubStr('a\nb', 'a.b', 0) === 'a\nb')",
         "true"}, // R1: the dot matches a line break
        {R"([pascal.regExprReplaceMatchSubStr('a1b22', '\\d+', '#'), pascal.regExprReplaceMatchSubStr('k=v;x=y', '(\\w)=(\\w)', '$2=$1'),)"
         R"( pascal.regExprReplaceMatchSubStr('none', '\\d', 'X')].join('|'))",
         "a#b#|v=k;y=x|none"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.expression);
        EXPECT_EQ(eval(c.expression), std::string("u:") + c.expected);
    }
    // strings cross as UTF-8: non-ASCII text outside the match is kept
    EXPECT_EQ(QString::fromUtf8(QByteArray::fromStdString(
                  eval(R"(pascal.regExprReplaceMatchSubStr('Город-1', '\\d', 'X'))"))),
              QString::fromUtf8("u:Город-X"));
}

// A test subclass substitutes the builder's random(); pascal.random, pascal.randomRange and Math.random all
// call it.
class FixedRandomBuilder final : public TileDownloadRequestBuilderJs {
public:
    using TileDownloadRequestBuilderJs::TileDownloadRequestBuilderJs;
    std::vector<qint32> drawn;

protected:
    qint32 random(qint32 upperExclusive) override {
        drawn.push_back(upperExclusive);
        return upperExclusive - 1;
    }
};

TEST_F(JsBuilderTest, RandomHelpersAndMathRandomDrawThroughTheBuildersRandom) {
    const QByteArray script =
        "function getUrl(tile, ctx) { return 'u:' + [pascal.random(10), pascal.randomRange(3, 8), "
        "pascal.randomRange(8, 3),"
        " pascal.randomRange(5, 5), pascal.randomRange(-4, 2), Math.random() * 1073741824].join(); }";
    auto builder = std::make_shared<FixedRandomBuilder>(
        QString::fromUtf8(script), std::make_shared<cg::maptype::JsSharedStore>(),
        std::make_shared<cg::maptype::JsScriptWatchdog>(), 5000ms, makeBuilderConfig("u:"),
        env.downloaderConfig, env.projectionSet, nullptr, env.checker, env.lang);
    EXPECT_EQ(urlOf(*builder), "u:9,7,7,5,1,1073741823");
    // randomRange(5, 5) does not draw; the others draw the span: 10, 5, 5, 6, and Math.random 2^30
    EXPECT_EQ(builder->drawn, (std::vector<qint32>{10, 5, 5, 6, 1073741824}));
}

TEST_F(JsBuilderTest, DefaultRandomStaysInRangeAndNonPositiveBoundGivesZero) {
    const std::string answer = eval(
        "(function () { var seen = {}, ok = true;\n"
        "  for (var i = 0; i < 300; i++) { var r = pascal.random(4); ok = ok && r >= 0 && r < 4 && r === "
        "Math.floor(r); seen[r] = 1;\n"
        "    var m = Math.random(); ok = ok && m >= 0 && m < 1; }\n"
        "  return ok + ',' + Object.keys(seen).length + ',' + pascal.random(0) + ',' + pascal.random(-3);\n"
        "})()");
    EXPECT_EQ(answer, "u:true,4,0,0"); // 300 draws of 4 values miss one with probability 4 * 0.75^300
}
} // namespace
