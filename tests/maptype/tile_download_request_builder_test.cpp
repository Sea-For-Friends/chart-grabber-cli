// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_TileDownloadRequestBuilderFactoryPascalScript.pas, u_TileDownloadRequestBuilderPascalScript.pas
// (the maptype half), u_TileDownloadRequestBuilderLazy.pas; docs/specs/maptype.md 2.12.4, 7.2.
#include "cg/core/config_provider_ini.h"
#include "cg/maptype/tile_download_request_builder_lazy.h"
#include "cg/maptype/url_template.h"
#include "cg/net/download_stats.h"
#include "cg/script/compiled_script.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <gtest/gtest.h>

#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>

#include "maptype_test_util.h"
#include "request_builder_test_util.h"

using namespace Qt::StringLiterals;
using cg::maptype::ITileDownloadRequestBuilderPtr;
using cg::maptype::TileDownloadRequestBuilderLazy;
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

std::shared_ptr<cg::net::IDownloader> asDownloader(const std::shared_ptr<ScriptedDownloader>& d) {
    return d;
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// --- template-only maps (:226-239) ---------------------------------------------------------------

TEST(TileDownloadRequestBuilder, TemplateOnlyMapBuildsAGetRequestFromTheTemplate) {
    FactoryEnv env;
    auto factory = env.factory({}, makeBuilderConfig("http://h/{z}/{x}/{y}.png", "X-A: 1\r\nX-B: 2"));
    auto builder = factory->buildRequestBuilder(nullptr);
    ASSERT_NE(builder, nullptr);
    const auto source = tileRequest(200, 300, 18);
    const auto request = builder->buildRequest(source, kNoInfo, {});
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->url, "http://h/18/200/300.png");
    EXPECT_EQ(request->requestHeader, "X-A: 1\r\nX-B: 2");
    EXPECT_EQ(request->method, Method::Get);
    EXPECT_TRUE(request->postData.isEmpty());
    EXPECT_EQ(request->inet, env.downloaderConfig->settings()->inet);
    EXPECT_EQ(request->checker, env.checker);
    // Source is stored from the ITileRequestPtr, which is what the checker casts back.
    EXPECT_EQ(static_cast<const cg::maptype::ITileRequest*>(request->source.get()), source.get());
    EXPECT_TRUE(factory->state()->getStatic()->enabled);
}

TEST(TileDownloadRequestBuilder, TemplateOnlyMapCreatesNoScriptObjectsAndNeverTouchesTheLogsFolder) {
    FactoryEnv env;
    const QString logs = env.tmp.path() + u"/never-created"_s;
    auto factory = std::make_shared<cg::maptype::TileDownloadRequestBuilderFactoryPascalScript>(
        u"t.zmp"_s, QByteArray(), makeBuilderConfig("http://h/{x}", {}, false, "+proj=x"),
        env.downloaderConfig, env.projectionSet, env.checker, env.projFactory, env.lang, nullptr, nullptr,
        nullptr, 7, logs);
    ASSERT_NE(factory->buildRequestBuilder(nullptr), nullptr);
    EXPECT_FALSE(QFileInfo::exists(logs));
    // The projection converter is created only on the script path (:209-217).
    EXPECT_EQ(env.projFactory->calls.load(), 0);
}

TEST(TileDownloadRequestBuilder, EmptyRenderedUrlGivesNoRequest) {
    FactoryEnv env;
    auto builder = env.factory({}, makeBuilderConfig(""))->buildRequestBuilder(nullptr);
    ASSERT_NE(builder, nullptr);
    EXPECT_FALSE(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, {}).has_value());
}

TEST(TileDownloadRequestBuilder, CancelledBeforeOrNeverTheTokenDecides) {
    FactoryEnv env;
    auto builder = env.factory({}, makeBuilderConfig("http://h/{x}"))->buildRequestBuilder(nullptr);
    std::stop_source cancelled;
    cancelled.request_stop();
    EXPECT_FALSE(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, cancelled.get_token()).has_value());
    std::stop_source live;
    EXPECT_TRUE(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, live.get_token()).has_value());
}

// --- script maps (:241-279) ----------------------------------------------------------------------

TEST(TileDownloadRequestBuilder, ScriptSeesThePreviousResponseHeadOfItsOwnSlot) {
    FactoryEnv env;
    auto factory = env.factory("begin ResultURL := GetURLBase + ResponseHead; end.", makeBuilderConfig("u:"));
    auto slotA = factory->buildRequestBuilder(nullptr);
    auto slotB = factory->buildRequestBuilder(nullptr);
    ASSERT_NE(slotA, nullptr);
    ASSERT_NE(slotB, nullptr);
    auto infoA = std::make_shared<LastResponseInfo>();
    auto infoB = std::make_shared<LastResponseInfo>();
    infoA->setResponseHead("Head-A");
    infoB->setResponseHead("Head-B");
    const auto tile = tileRequest(1, 2, 3);
    EXPECT_EQ(slotA->buildRequest(tile, infoA, {})->url, "u:Head-A");
    EXPECT_EQ(slotB->buildRequest(tile, infoB, {})->url, "u:Head-B");
    infoA->setResponseHead("Head-A2");
    EXPECT_EQ(slotA->buildRequest(tile, infoA, {})->url, "u:Head-A2");
    EXPECT_EQ(slotA->buildRequest(tile, kNoInfo, {})->url, "u:"); // no previous response: ''
}

TEST(TileDownloadRequestBuilder, ScriptPostDataMakesAPostWithTheScriptsRequestHead) {
    FactoryEnv env;
    auto factory = env.factory(
        "begin ResultURL := 'http://p/' + inttostr(getx); PostData := 'a=b'; RequestHead := 'H: v'; end.",
        makeBuilderConfig("u:", "Config: header"));
    auto builder = factory->buildRequestBuilder(nullptr);
    const auto request = builder->buildRequest(tileRequest(5, 6, 7), kNoInfo, {});
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->method, Method::Post);
    EXPECT_EQ(request->url, "http://p/5");
    EXPECT_EQ(request->postData, "a=b");
    EXPECT_EQ(request->requestHeader, "H: v"); // the script's, not the config's
    EXPECT_EQ(request->inet, env.downloaderConfig->settings()->inet);
}

TEST(TileDownloadRequestBuilder, ScriptWithoutPostDataGivesAGetKeepingTheConfigHeader) {
    FactoryEnv env;
    auto builder = env.factory("begin ResultURL := GetURLBase + inttostr(getz); end.",
                               makeBuilderConfig("u:", "Config: header"))
                       ->buildRequestBuilder(nullptr);
    const auto request = builder->buildRequest(tileRequest(5, 6, 7), kNoInfo, {});
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->method, Method::Get);
    EXPECT_EQ(request->url, "u:8"); // GetZ = zoom + 1
    EXPECT_EQ(request->requestHeader, "Config: header");
}

TEST(TileDownloadRequestBuilder, EmptyResultUrlMeansTheTileDoesNotExist) {
    FactoryEnv env;
    auto builder =
        env.factory("begin ResultURL := ''; end.", makeBuilderConfig("u:"))->buildRequestBuilder(nullptr);
    EXPECT_FALSE(builder->buildRequest(tileRequest(5, 6, 7), kNoInfo, {}).has_value());
}

TEST(TileDownloadRequestBuilder, ScriptVarsCarryTheTileGeometryLanguageAndVersion) {
    FactoryEnv env;
    env.lang->setCurrentLanguageCode(u"ru"_s);
    auto builder =
        env.factory("begin ResultURL := inttostr(getx) + '|' + inttostr(gety) + '|' + inttostr(getz) + '|' + "
                    "Lang + '|' + Version + '|' + RoundEx(GetRMetr, 4) + '|' + RoundEx(GetBMetr, 4); end.",
                    makeBuilderConfig("u:"))
            ->buildRequestBuilder(nullptr);
    const auto version = std::make_shared<const cg::storage::MapVersionInfo>(u"Вер 7"_s, 1);
    const auto source = std::make_shared<const cg::maptype::TileRequest>(QPoint(1, 1), 1, version);
    const auto request = builder->buildRequest(source, kNoInfo, {});
    ASSERT_TRUE(request.has_value());
    // Zoom 1 tile (1,1) of Web Mercator: east edge x = pi * 6378137, south edge y = -pi * 6378137.
    EXPECT_EQ(request->url, QString::fromUtf8("1|1|2|ru|Вер 7|20037508.3428|-20037508.3428").toUtf8());
}

TEST(TileDownloadRequestBuilder, LanguageChangeIsPickedUpByTheNextRequest) {
    FactoryEnv env;
    env.lang->setCurrentLanguageCode(u"en"_s);
    auto builder =
        env.factory("begin ResultURL := Lang; end.", makeBuilderConfig("u:"))->buildRequestBuilder(nullptr);
    EXPECT_EQ(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, {})->url, "en");
    env.lang->setCurrentLanguageCode(u"de"_s);
    EXPECT_EQ(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, {})->url, "de");
}

TEST(TileDownloadRequestBuilder, TemplateToUrlInsideTheScriptSeesTheCurrentTile) {
    FactoryEnv env;
    auto builder =
        env.factory("begin ResultURL := TemplateToUrl('h/{x}/{y}/{z}'); end.", makeBuilderConfig("u:"))
            ->buildRequestBuilder(nullptr);
    EXPECT_EQ(builder->buildRequest(tileRequest(11, 22, 9), kNoInfo, {})->url, "h/11/22/9");
    EXPECT_EQ(builder->buildRequest(tileRequest(33, 44, 5), kNoInfo, {})->url, "h/33/44/5");
}

TEST(TileDownloadRequestBuilder, ZoomOutsideTheProjectionSetRaisesTheListIndexError) {
    FactoryEnv env;
    auto builder =
        env.factory("begin ResultURL := 'x'; end.", makeBuilderConfig("u:"))->buildRequestBuilder(nullptr);
    try {
        (void)builder->buildRequest(tileRequest(1, 2, 24), kNoInfo, {});
        FAIL() << "no exception";
    } catch (const std::out_of_range& e) {
        EXPECT_STREQ(e.what(), "List index out of bounds (24)");
    }
}

// The IScriptHost functions of the download path under the Q-8 sandbox: SaveToLocalFile (legacy
// u_PascalScriptUtils.pas:173-200) writes nothing and returns 0, FileExists (SysUtils) is false even for an
// existing file; FakeWriteLn and System.Random are unchanged.
TEST(TileDownloadRequestBuilder, HostFunctionsOfTheScript) {
    FactoryEnv env;
    const QString nested = env.tmp.path() + u"/a/b/saved.bin"_s;
    const QString existing = env.tmp.path() + u"/existing.bin"_s;
    {
        QFile f(existing);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("xyz");
    }
    const QByteArray script =
        "var P, E, R: string; begin P := '" + nested.toUtf8() + "'; E := '" + existing.toUtf8() +
        "';\n"
        "  WriteLn('ignored');\n"
        "  R := inttostr(SaveToLocalFile(P, 'abc')) + ',' + inttostr(SaveToLocalFile(E, '')) + ',' + "
        "inttostr(Random(1)) + ',' + inttostr(Random(-5));\n"
        "  if FileExists(E) then R := R + ',exists';\n"
        "  if not FileExists(E) then R := R + ',notseen';\n"
        "  if not FileExists('" +
        env.tmp.path().toUtf8() +
        "') then R := R + ',nodir';\n"
        "  ResultURL := R; end.";
    auto builder = env.factory(script, makeBuilderConfig("u:"))->buildRequestBuilder(nullptr);
    EXPECT_EQ(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, {})->url, "0,0,0,0,notseen,nodir");
    EXPECT_FALSE(QFileInfo::exists(nested));
    EXPECT_FALSE(QFileInfo::exists(env.tmp.path() + u"/a"_s)); // not even the parent directory
    EXPECT_EQ(readFile(existing), QByteArray("xyz"));          // an empty write did not truncate
}

// Each sandbox refusal logs once per builder, not per call; a second builder logs again.
TEST(TileDownloadRequestBuilder, SandboxWarningsAreLoggedOncePerBuilder) {
    FactoryEnv env;
    const QString path = env.tmp.path() + u"/w.bin"_s;
    const QByteArray script = "var S: string; begin S := '" + path.toUtf8() +
                              "'; SaveToLocalFile(S, 'a'); SaveToLocalFile(S, 'b'); if FileExists(S) then "
                              "S := ''; if FileExists(S) then S := ''; ResultURL := 'x'; end.";
    auto factory = env.factory(script, makeBuilderConfig("u:"));
    SandboxLogCapture capture;
    auto first = factory->buildRequestBuilder(nullptr);
    ASSERT_TRUE(first->buildRequest(tileRequest(1, 2, 3), kNoInfo, {}).has_value());
    ASSERT_TRUE(first->buildRequest(tileRequest(1, 2, 4), kNoInfo, {}).has_value());
    EXPECT_EQ(capture.countContaining("script file writes are disabled (SaveToLocalFile \"" + path + "\")"),
              1);
    EXPECT_EQ(capture.countContaining("script file probes are disabled (FileExists \"" + path + "\")"), 1);
    auto second = factory->buildRequestBuilder(nullptr);
    ASSERT_TRUE(second->buildRequest(tileRequest(1, 2, 3), kNoInfo, {}).has_value());
    EXPECT_EQ(capture.countContaining("script file writes are disabled"), 2);
    EXPECT_EQ(capture.countContaining("script file probes are disabled"), 2);
}

// The script's Downloader and file:/// URLs (Q-8): status 0 with empty header and body, no request reaches
// the transport, one warning per builder; an http:// URL is unchanged. The tile transport reading file:///
// (net::CurlDownloader) is not touched: tests/net/file_url_test.cpp FileUrl.FileUrlIsRead.
TEST(TileDownloadRequestBuilder, ScriptDownloaderRefusesFileUrlsButNotHttp) {
    FactoryEnv env;
    const QString file = env.tmp.path() + u"/data.txt"_s;
    {
        QFile f(file);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("filebody");
    }
    // file:/// + the path without its leading '/' (/tmp/x on POSIX, C:/x on Windows, T-0335)
    const QByteArray fileRest = (file.startsWith(u'/') ? file.mid(1) : file).toUtf8();
    auto downloader = std::make_shared<ScriptedDownloader>();
    downloader->replies.push_back(okReply(200, "H: 1", "netbody"));
    const QByteArray script =
        "var h, d: AnsiString; r1, r2, r3: Cardinal; begin "
        "r1 := Downloader.DoHttpRequest('file:///" +
        fileRest +
        "', '', '', h, d); "
        "ResultURL := 'f' + inttostr(r1) + '[' + h + '][' + d + ']'; "
        "r2 := Downloader.DoHttpRequest('FILE:///" +
        fileRest +
        "', '', '', h, d); "
        "r3 := Downloader.DoHttpRequest('http://d/', '', '', h, d); "
        "ResultURL := ResultURL + 'f' + inttostr(r2) + 'h' + inttostr(r3) + '[' + h + '][' + d + ']'; end.";
    auto builder =
        env.factory(script, makeBuilderConfig("u:", {}, true))->buildRequestBuilder(asDownloader(downloader));
    SandboxLogCapture capture;
    EXPECT_EQ(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, {})->url, "f0[][]f0h200[H: 1][netbody]");
    ASSERT_EQ(downloader->requests.size(), 1u); // only the http:// call reached the transport
    EXPECT_EQ(downloader->requests[0].url, "http://d/");
    // two file:/// calls in the script, one warning
    EXPECT_EQ(capture.countContaining("script file reads are disabled"), 1);
}

// --- one stock scripted ZMP end to end -----------------------------------------------------------

TEST(TileDownloadRequestBuilder, StockGgcZmpEndToEndWithAFakeDownloader) {
    CG_SKIP_IF_NO_CORPUS();
    const QString dir = cg::maptype::test::fixtureDir() + u"/zmp/sas.maps/GGC/karta_ggc_1km.zmp"_s;
    const QByteArray script = readFile(dir + u"/GetUrlScript.txt"_s);
    ASSERT_FALSE(script.isEmpty());
    FactoryEnv env;
    // The ZMP leaves IsUseDownloaderInScript off; the script asks the downloader for the zoom probe only when
    // on.
    auto factory =
        env.factory(script, makeBuilderConfig("https://maps.melda.ru/pub/ggc/1km.png/", "X-T: 1", true),
                    u"karta_ggc_1km.zmp"_s);
    auto downloader = std::make_shared<ScriptedDownloader>();
    downloader->replies.push_back(okReply(200));
    downloader->replies.push_back(notFoundReply());
    auto builder = factory->buildRequestBuilder(asDownloader(downloader));
    ASSERT_NE(builder, nullptr);

    // Zoom 10 (GetZ 11): the probe GETs z11/, the 200 raises the known-good zoom, the tile URL follows.
    auto request = builder->buildRequest(tileRequest(1234, 5678, 10), kNoInfo, {});
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->url, "https://maps.melda.ru/pub/ggc/1km.png/z11/1/x1234/5/y5678.png");
    EXPECT_EQ(request->requestHeader, "X-T: 1");
    ASSERT_EQ(downloader->requests.size(), 1u);
    EXPECT_EQ(downloader->requests[0].url, "https://maps.melda.ru/pub/ggc/1km.png/z11/");
    EXPECT_EQ(downloader->requests[0].method, Method::Get);

    // Zoom 11 (GetZ 12): the probe is answered 404, so z12 does not exist: no request.
    EXPECT_FALSE(builder->buildRequest(tileRequest(1, 1, 11), kNoInfo, {}).has_value());
    ASSERT_EQ(downloader->requests.size(), 2u);
    EXPECT_EQ(downloader->requests[1].url, "https://maps.melda.ru/pub/ggc/1km.png/z12/");

    // ScriptBuffer ('11;12') survived: zoom 10 needs no probe any more, zoom 11 is refused without one.
    request = builder->buildRequest(tileRequest(2048, 0, 10), kNoInfo, {});
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->url, "https://maps.melda.ru/pub/ggc/1km.png/z11/2/x2048/0/y0.png");
    EXPECT_FALSE(builder->buildRequest(tileRequest(1, 1, 11), kNoInfo, {}).has_value());
    EXPECT_EQ(downloader->requests.size(), 2u);
    EXPECT_TRUE(factory->state()->getStatic()->enabled);
}

TEST(TileDownloadRequestBuilder, ScriptWithoutUseDownloaderGetsNoDownloader) {
    FactoryEnv env;
    auto downloader = std::make_shared<ScriptedDownloader>();
    auto builder =
        env.factory("begin if Assigned(Downloader) then ResultURL := 'has' else ResultURL := 'none'; end.",
                    makeBuilderConfig("u:"))
            ->buildRequestBuilder(asDownloader(downloader));
    EXPECT_EQ(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, {})->url, "none");
}

TEST(TileDownloadRequestBuilder, ScriptWithUseDownloaderButNoDownloaderGetsNone) {
    FactoryEnv env;
    auto builder =
        env.factory("begin if Assigned(Downloader) then ResultURL := 'has' else ResultURL := 'none'; end.",
                    makeBuilderConfig("u:", {}, true))
            ->buildRequestBuilder(nullptr);
    EXPECT_EQ(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, {})->url, "none");
}

TEST(TileDownloadRequestBuilder, ScriptDownloaderIsBoundToTheRequestsCancelToken) {
    FactoryEnv env;
    auto downloader = std::make_shared<ScriptedDownloader>();
    downloader->replies.push_back(okReply(200));
    auto builder =
        env.factory("var h, d: AnsiString; begin "
                    "ResultURL := 'c' + inttostr(Downloader.DoHttpRequest('http://d/', '', '', h, d)); end.",
                    makeBuilderConfig("u:", {}, true))
            ->buildRequestBuilder(asDownloader(downloader));
    std::stop_source live;
    EXPECT_EQ(builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, live.get_token())->url, "c200");
    EXPECT_EQ(downloader->requests.size(), 1u);
}

// --- failures ------------------------------------------------------------------------------------

TEST(TileDownloadRequestBuilderFactory,
     CompileErrorDisablesWithTheCompilersMessageAndStillReturnsATemplateBuilder) {
    const QByteArray broken = "var A: string;\nbegin\n  A := 'unterminated1\n  A := 'unterminated2\nend.";
    FactoryEnv env;
    auto factory = env.factory(broken, makeBuilderConfig("http://h/{x}/{y}"));
    // LD-18 / OQ-13: the call that compiles gets a builder over no program, i.e. a template-only one.
    auto builder = factory->buildRequestBuilder(nullptr);
    ASSERT_NE(builder, nullptr);
    const auto state = factory->state()->getStatic();
    EXPECT_FALSE(state->enabled);
    const auto compiled = cg::script::CompiledScript::compile(broken, u"test.zmp"_s);
    ASSERT_FALSE(compiled.has_value());
    // u_ResStrings.pas:109: "Compilation error in script " (space) then CR LF, and CR LF after each message.
    EXPECT_TRUE(state->disableReason.startsWith(u"Compilation error in script \r\n[Line 3"_s))
        << state->disableReason.toStdString();
    EXPECT_TRUE(state->disableReason.endsWith(u"\r\n"_s)) << state->disableReason.toStdString();
    EXPECT_EQ(state->disableReason, compiled.error().what());
    const auto request = builder->buildRequest(tileRequest(4, 5, 6), kNoInfo, {});
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->url, "http://h/4/5"); // the template, not the script
    // Disabled: the next ask gets nothing, and the first reason is kept.
    EXPECT_EQ(factory->buildRequestBuilder(nullptr), nullptr);
    EXPECT_EQ(factory->state()->getStatic()->disableReason, compiled.error().what());
}

TEST(TileDownloadRequestBuilderFactory, ScriptCompilesOnceForManySlots) {
    FactoryEnv env;
    auto factory =
        env.factory("begin ResultURL := 'ok'; end.", makeBuilderConfig("u:", {}, false, "+proj=merc"));
    for (int i = 0; i < 3; ++i) {
        ASSERT_NE(factory->buildRequestBuilder(nullptr), nullptr);
    }
    // The default projection converter is created in the same step as the compile (:214-219).
    EXPECT_EQ(env.projFactory->calls.load(), 1);
    EXPECT_EQ(env.projFactory->lastArgs, "+proj=merc");
}

// A script that fails while running: the exception reaches the caller with the original text, the builder
// state is not touched (no Disable in legacy), and the next request runs again.
TEST(TileDownloadRequestBuilder, RuntimeErrorPropagatesWithTheHostsTextAndLeavesTheStateEnabled) {
    FactoryEnv env;
    auto factory =
        env.factory("begin if getx = 1 then TileCache.Write(1, 1, 1, '', 'image/nope', 'x', true); "
                    "ResultURL := 'ok' + inttostr(getx); end.",
                    makeBuilderConfig("u:"));
    auto lazy = std::make_shared<TileDownloadRequestBuilderLazy>(nullptr, factory);
    try {
        (void)lazy->buildRequest(tileRequest(1, 2, 3), kNoInfo, {});
        FAIL() << "no exception";
    } catch (const std::runtime_error& e) {
        EXPECT_TRUE(QString::fromUtf8(e.what()).startsWith(u"Unknown Content-Type: image/nope"_s))
            << e.what();
    }
    EXPECT_TRUE(factory->state()->getStatic()->enabled);
    const auto next = lazy->buildRequest(tileRequest(2, 2, 3), kNoInfo, {});
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->url, "ok2");
}

TEST(TileDownloadRequestBuilder, DivisionByZeroInTheScriptPropagates) {
    FactoryEnv env;
    auto builder =
        env.factory("begin ResultURL := inttostr(10 div (getx - getx)); end.", makeBuilderConfig("u:"))
            ->buildRequestBuilder(nullptr);
    try {
        (void)builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, {});
        FAIL() << "no exception";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(QString::fromUtf8(e.what()).indexOf(u"Division by zero"_s), -1) << e.what();
    }
}

TEST(TileDownloadRequestBuilderFactory, ProjConverterFactoryThatThrowsDisablesAsUnknownCompileError) {
    class Throwing final : public cg::script::IProjConverterFactory {
    public:
        [[nodiscard]] std::shared_ptr<const cg::script::IProjConverter> byEpsg(int) const override {
            return nullptr;
        }
        [[nodiscard]] std::shared_ptr<const cg::script::IProjConverter>
        byInitString(const QByteArray&) const override {
            throw std::runtime_error("bad proj args");
        }
    };
    FactoryEnv env;
    auto factory = std::make_shared<cg::maptype::TileDownloadRequestBuilderFactoryPascalScript>(
        u"t.zmp"_s, QByteArray("begin ResultURL := 'ok'; end."), makeBuilderConfig("u:", {}, false, "+x"),
        env.downloaderConfig, env.projectionSet, env.checker, std::make_shared<Throwing>(), env.lang, nullptr,
        std::make_shared<cg::storage::MapVersionFactory>(),
        std::make_shared<const cg::core::ContentTypeRegistry>(), 7, env.tmp.path());
    // The builder is still returned (falls through with no program); the state says why it is off.
    EXPECT_NE(factory->buildRequestBuilder(nullptr), nullptr);
    EXPECT_FALSE(factory->state()->getStatic()->enabled);
    EXPECT_EQ(factory->state()->getStatic()->disableReason, u"Unknown script compile error: bad proj args"_s);
}

// --- lazy wrapper (…Lazy.pas:76-109) ---------------------------------------------------------------

class CountingBuilder final : public cg::maptype::ITileDownloadRequestBuilder {
public:
    std::optional<cg::net::DownloadRequest> buildRequest(const cg::maptype::ITileRequestPtr&,
                                                         const std::shared_ptr<LastResponseInfo>&,
                                                         const std::stop_token&) override {
        ++calls;
        cg::net::DownloadRequest r;
        r.url = "built";
        return r;
    }
    std::atomic<int> calls{0};
};

class FakeFactory final : public cg::maptype::ITileDownloadRequestBuilderFactory {
public:
    [[nodiscard]] std::shared_ptr<cg::maptype::ITileDownloaderStateChangeable> state() const override {
        return stateInternal;
    }
    [[nodiscard]] ITileDownloadRequestBuilderPtr
    buildRequestBuilder(const cg::net::IDownloaderPtr& d) override {
        const int now = ++inside;
        int seen = maxInside.load();
        while (now > seen && !maxInside.compare_exchange_weak(seen, now)) {
        }
        ++builds;
        lastDownloader = d;
        std::this_thread::yield();
        --inside;
        return result;
    }

    std::shared_ptr<cg::maptype::TileDownloaderStateInternal> stateInternal{
        std::make_shared<cg::maptype::TileDownloaderStateInternal>()};
    std::shared_ptr<CountingBuilder> result{std::make_shared<CountingBuilder>()};
    std::atomic<int> inside{0}, maxInside{0}, builds{0};
    cg::net::IDownloaderPtr lastDownloader;
};

TEST(TileDownloadRequestBuilderLazy, AsksTheFactoryOnceWithItsDownloaderAndDelegates) {
    auto factory = std::make_shared<FakeFactory>();
    const auto downloader = std::make_shared<ScriptedDownloader>();
    TileDownloadRequestBuilderLazy lazy(downloader, factory);
    EXPECT_EQ(lazy.buildRequest(tileRequest(1, 2, 3), kNoInfo, {})->url, "built");
    EXPECT_EQ(lazy.buildRequest(tileRequest(1, 2, 3), kNoInfo, {})->url, "built");
    EXPECT_EQ(factory->builds.load(), 1);
    EXPECT_EQ(factory->result->calls.load(), 2);
    EXPECT_EQ(factory->lastDownloader, downloader);
}

TEST(TileDownloadRequestBuilderLazy, DisabledStateAndCancelBuildNothing) {
    auto factory = std::make_shared<FakeFactory>();
    TileDownloadRequestBuilderLazy lazy(nullptr, factory);
    std::stop_source cancelled;
    cancelled.request_stop();
    EXPECT_FALSE(lazy.buildRequest(tileRequest(1, 2, 3), kNoInfo, cancelled.get_token()).has_value());
    EXPECT_EQ(factory->builds.load(), 0);
    factory->stateInternal->disable(u"off"_s);
    EXPECT_FALSE(lazy.buildRequest(tileRequest(1, 2, 3), kNoInfo, {}).has_value());
    EXPECT_EQ(factory->builds.load(), 0);
}

TEST(TileDownloadRequestBuilderLazy, AKeptBuilderKeepsServingAfterTheFactoryIsDisabled) {
    auto factory = std::make_shared<FakeFactory>();
    TileDownloadRequestBuilderLazy lazy(nullptr, factory);
    ASSERT_TRUE(lazy.buildRequest(tileRequest(1, 2, 3), kNoInfo, {}).has_value());
    factory->stateInternal->disable(u"off"_s);
    // :87 tests the state before anything else, so a disabled factory silences an already built builder too.
    EXPECT_FALSE(lazy.buildRequest(tileRequest(1, 2, 3), kNoInfo, {}).has_value());
    EXPECT_EQ(factory->result->calls.load(), 1);
}

TEST(TileDownloadRequestBuilderLazy, NullFromTheFactoryIsNotKeptAndAskedAgain) {
    auto factory = std::make_shared<FakeFactory>();
    factory->result = nullptr;
    TileDownloadRequestBuilderLazy lazy(nullptr, factory);
    EXPECT_FALSE(lazy.buildRequest(tileRequest(1, 2, 3), kNoInfo, {}).has_value());
    EXPECT_FALSE(lazy.buildRequest(tileRequest(1, 2, 3), kNoInfo, {}).has_value());
    EXPECT_EQ(factory->builds.load(), 2);
}

// --- threads (repeated 1000 times) ---------------------------------------------------------------

// The lazy wrapper creates its builder under its own mutex: eight callers racing on the first request see
// exactly one factory call, never two inside it, and all get the same builder.
TEST(TileDownloadRequestBuilderLazy, ConcurrentFirstRequestsCreateOneBuilderUnderTheMutex) {
    constexpr int kRounds = 1000;
    constexpr int kThreads = 8;
    for (int round = 0; round < kRounds; ++round) {
        auto factory = std::make_shared<FakeFactory>();
        TileDownloadRequestBuilderLazy lazy(nullptr, factory);
        std::atomic<int> served{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&] {
                if (lazy.buildRequest(tileRequest(1, 2, 3), kNoInfo, {}).has_value()) {
                    ++served;
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
        ASSERT_EQ(factory->builds.load(), 1) << "round " << round;
        ASSERT_EQ(factory->maxInside.load(), 1) << "round " << round;
        ASSERT_EQ(served.load(), kThreads) << "round " << round;
        ASSERT_EQ(factory->result->calls.load(), kThreads) << "round " << round;
    }
}

// The factory compiles under its init lock: concurrent first calls give one projection-factory call, never
// overlapping, and every caller gets a builder that runs the script.
TEST(TileDownloadRequestBuilderFactory, ConcurrentFirstCallsCompileOnceUnderTheInitLock) {
    constexpr int kRounds = 1000;
    constexpr int kThreads = 6;
    for (int round = 0; round < kRounds; ++round) {
        FactoryEnv env;
        auto factory = env.factory("begin ResultURL := 'ok'; end.", makeBuilderConfig("u:", {}, false, "+p"));
        std::atomic<int> good{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&] {
                auto builder = factory->buildRequestBuilder(nullptr);
                if (builder != nullptr) {
                    const auto request = builder->buildRequest(tileRequest(1, 2, 3), kNoInfo, {});
                    if (request.has_value() && request->url == "ok") {
                        ++good;
                    }
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
        ASSERT_EQ(env.projFactory->calls.load(), 1) << "round " << round;
        ASSERT_EQ(env.projFactory->maxInside.load(), 1) << "round " << round;
        ASSERT_EQ(good.load(), kThreads) << "round " << round;
    }
}

// One executor per slot, guarded by the builder's lock: two callers on the SAME builder never run the script
// together, while two builders of one factory do (each waits inside the downloader for the other).
TEST(TileDownloadRequestBuilder, SameSlotCallsAreSerialisedAndDifferentSlotsOverlap) {
    constexpr int kRounds = 1000;
    const QByteArray script =
        "var h, d: AnsiString; begin "
        "ResultURL := 'c' + inttostr(Downloader.DoHttpRequest('http://d/', '', '', h, d)); end.";
    for (int round = 0; round < kRounds; ++round) {
        FactoryEnv env;
        auto factory = env.factory(script, makeBuilderConfig("u:", {}, true));
        auto dl = std::make_shared<ScriptedDownloader>();
        std::atomic<int> inside{0}, maxInside{0};
        std::mutex m;
        std::condition_variable cv;
        int arrived = 0;
        std::atomic<bool> rendezvous{false};
        std::atomic<bool> rendezvousTimedOut{false};
        dl->onRequest = [&] {
            const int now = ++inside;
            int seen = maxInside.load();
            while (now > seen && !maxInside.compare_exchange_weak(seen, now)) {
            }
            if (rendezvous) {
                std::unique_lock lock(m);
                ++arrived;
                cv.notify_all();
                if (!cv.wait_for(lock, std::chrono::seconds(5), [&] { return arrived >= 2; })) {
                    rendezvousTimedOut = true;
                }
            } else {
                std::this_thread::yield();
            }
            --inside;
        };
        for (int i = 0; i < 4; ++i) {
            dl->replies.push_back(okReply(200));
        }
        auto slotA = factory->buildRequestBuilder(asDownloader(dl));
        auto slotB = factory->buildRequestBuilder(asDownloader(dl));

        // Same builder, two threads: the builder lock keeps them one after the other.
        std::thread t1([&] { (void)slotA->buildRequest(tileRequest(1, 2, 3), kNoInfo, {}); });
        std::thread t2([&] { (void)slotA->buildRequest(tileRequest(1, 2, 3), kNoInfo, {}); });
        t1.join();
        t2.join();
        ASSERT_EQ(maxInside.load(), 1) << "round " << round;

        // Different builders: both are inside the downloader at the same moment.
        rendezvous = true;
        std::thread t3([&] { (void)slotA->buildRequest(tileRequest(1, 2, 3), kNoInfo, {}); });
        std::thread t4([&] { (void)slotB->buildRequest(tileRequest(1, 2, 3), kNoInfo, {}); });
        t3.join();
        t4.join();
        ASSERT_FALSE(rendezvousTimedOut.load()) << "round " << round;
        ASSERT_EQ(maxInside.load(), 2) << "round " << round;
    }
}

// The builder's lock guards the UrlTemplate, which keeps the current tile and is not thread-safe: threads
// sharing one template-only builder each get the URL of their own tile.
TEST(TileDownloadRequestBuilder, SharedTemplateBuilderGivesEachThreadItsOwnTile) {
    constexpr int kRounds = 200;
    constexpr int kThreads = 4;
    FactoryEnv env;
    auto builder = env.factory({}, makeBuilderConfig("h/{x}/{y}/{z}"))->buildRequestBuilder(nullptr);
    std::atomic<int> wrong{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            const QByteArray expected = "h/" + QByteArray::number(100 + t) + "/" +
                                        QByteArray::number(200 + t) + "/" + QByteArray::number(5 + t);
            for (int i = 0; i < kRounds; ++i) {
                const auto request = builder->buildRequest(tileRequest(100 + t, 200 + t, 5 + t), kNoInfo, {});
                if (!request.has_value() || request->url != expected) {
                    ++wrong;
                }
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    EXPECT_EQ(wrong.load(), 0);
}

// --- the corpus ------------------------------------------------------------------------------------

struct Zmp {
    QString dir;
    QByteArray script;
    std::shared_ptr<cg::maptype::TileDownloadRequestBuilderConfig> config;
    cg::geo::ProjectionSetPtr projectionSet;
};

std::vector<Zmp> loadStockZmps() {
    std::vector<Zmp> all;
    auto datums = std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{});
    cg::geo::ProjectionSetFactory projections{cg::geo::HashSeed{}, datums};
    QDirIterator it(cg::maptype::test::fixtureDir() + u"/zmp/sas.maps"_s, {u"params.txt"_s}, QDir::Files,
                    QDirIterator::Subdirectories);
    QStringList paths;
    while (it.hasNext()) {
        paths.push_back(it.next());
    }
    paths.sort();
    for (const QString& path : paths) {
        const auto ini = cg::core::ConfigProviderIni::open(path);
        if (!ini) {
            ADD_FAILURE() << path.toStdString();
            continue;
        }
        const auto p = (*ini)->subItem(u"PARAMS");
        cg::maptype::TileDownloadRequestBuilderConfigStatic cfg; // loadTileRequestBuilderConfig, zmp_info.cpp
        cfg.urlBase = p->readString(u"DefURLBase", QString()).toUtf8();
        cfg.urlBase = p->readString(u"URLBase", QString::fromUtf8(cfg.urlBase)).toUtf8();
        cfg.serverNames = p->readString(u"ServerNames", QString()).toUtf8();
        cfg.requestHeader = p->readString(u"RequestHead", QString()).toUtf8();
        cfg.isUseDownloader = p->readBool(u"IsUseDownloaderInScript", false);
        cfg.defaultProjConverterArgs = p->readString(u"Proj4Args", QString()).toUtf8();
        const QString dir = QFileInfo(path).absolutePath();
        all.push_back({dir, readFile(dir + u"/GetUrlScript.txt"_s),
                       std::make_shared<cg::maptype::TileDownloadRequestBuilderConfig>(
                           std::make_shared<const cg::maptype::TileDownloadRequestBuilderConfigStatic>(cfg)),
                       projections.byConfig(p.get())});
    }
    return all;
}

// Every stock ZMP goes through the factory and a builder comes back for each (LD-18 for the broken ones).
// 164 are template-only, 350 carry a script; 348 of those compile and keep the state enabled. The two
// Balticsmap scripts are the corpus' known compile failures (corpus_parse_test): the state is disabled with
// the compiler's message, and the call that compiled still got a template-only builder.
TEST(TileDownloadRequestBuilderFactory, AllStockZmpsGoThroughTheFactory) {
    CG_SKIP_IF_NO_CORPUS();
    const std::vector<Zmp> zmps = loadStockZmps();
    int templateOnly = 0, scripted = 0, enabled = 0;
    QStringList disabled;
    for (const Zmp& z : zmps) {
        FactoryEnv env;
        auto factory = std::make_shared<cg::maptype::TileDownloadRequestBuilderFactoryPascalScript>(
            QFileInfo(z.dir).fileName(), z.script, z.config, env.downloaderConfig, z.projectionSet,
            env.checker, env.projFactory, env.lang, nullptr,
            std::make_shared<cg::storage::MapVersionFactory>(),
            std::make_shared<const cg::core::ContentTypeRegistry>(), 7, env.tmp.path());
        const auto builder = factory->buildRequestBuilder(nullptr);
        const auto state = factory->state()->getStatic();
        (z.script.isEmpty() ? templateOnly : scripted)++;
        EXPECT_NE(builder, nullptr) << z.dir.toStdString();
        if (state->enabled) {
            ++enabled;
        } else {
            disabled.push_back(QFileInfo(z.dir).fileName() + u": "_s + state->disableReason);
        }
    }
    EXPECT_EQ(templateOnly, 164);
    EXPECT_EQ(scripted, 350);
    EXPECT_EQ(enabled, 512);
    // Legacy bytes: "Compilation error in script " 20 0D 0A, each message then 0D 0A (u_ResStrings.pas:109).
    const QString reason = u"Compilation error in script \r\n[Line 10, Column 1] expected 'end'\r\n"
                           u"[Line 10, Column 1] expected '.'\r\n"_s;
    EXPECT_EQ(disabled,
              (QStringList{u"balticmaps.eu.map.zmp: "_s + reason, u"balticmaps.eu.topo.zmp: "_s + reason}));
}

// Template-only ZMPs: the builder's URL is the T-0053 rendering of the same config. A {s} server is
// picked at random per builder, so every possible server is rendered and the builder's URL must be one.
TEST(TileDownloadRequestBuilderFactory, TemplateOnlyStockZmpsGiveTheUrlTemplateOracle) {
    CG_SKIP_IF_NO_CORPUS();
    const std::vector<Zmp> zmps = loadStockZmps();
    int checked = 0;
    QStringList wrong;
    for (const Zmp& z : zmps) {
        if (!z.script.isEmpty()) {
            continue;
        }
        FactoryEnv env;
        auto factory = env.factory({}, z.config);
        auto builder = factory->buildRequestBuilder(nullptr);
        ASSERT_NE(builder, nullptr);
        const auto source = tileRequest(200, 300, 10);
        const auto request = builder->buildRequest(source, kNoInfo, {});
        const QByteArray names = z.config->serverNames();
        const int servers = std::max<int>(1, static_cast<int>(QString::fromUtf8(names).split(u',').size()));
        QList<QByteArray> candidates;
        for (int pick = 0; pick < servers; ++pick) {
            cg::maptype::UrlTemplate oracle(env.lang, z.projectionSet, z.config,
                                            [pick](qint32) { return pick; });
            candidates.push_back(oracle.render({QPoint(200, 300), 10, nullptr}).toUtf8());
        }
        ++checked;
        if (!request.has_value() || !candidates.contains(request->url) ||
            request->requestHeader != z.config->requestHeader()) {
            wrong.push_back(z.dir);
        }
    }
    EXPECT_EQ(checked, 164);
    EXPECT_TRUE(wrong.isEmpty()) << wrong.join(u'\n').toStdString();
}

// Literal URLs of three common template shapes, tile (200,300), zoom 10.
TEST(TileDownloadRequestBuilderFactory, TemplateShapesGiveLiteralUrls) {
    const struct {
        const char* base;
        const char* expected;
    } cases[] = {
        {"https://tile.openstreetmap.org/{z}/{x}/{y}.png", "https://tile.openstreetmap.org/10/200/300.png"},
        {"http://khm1.google.com/kh/v=1&x={x}&y={y}&z={z}", "http://khm1.google.com/kh/v=1&x=200&y=300&z=10"},
        {"http://t0.tiles/{z}/{x}/{-y}.png", "http://t0.tiles/10/200/723.png"}, // {-y} = 2^10 - 1 - 300
    };
    for (const auto& c : cases) {
        FactoryEnv env;
        auto builder = env.factory({}, makeBuilderConfig(c.base))->buildRequestBuilder(nullptr);
        EXPECT_EQ(builder->buildRequest(tileRequest(200, 300, 10), kNoInfo, {})->url, c.expected);
    }
}

} // namespace
