// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-002 (contributes; held open).
// Ports u_ZmpInfo.pas, i_ZmpInfo.pas; docs/specs/maptype.md 2.5, 2.6, 4, 7.4, 8.2 (LD-7, LD-8, LD-14), 8.3
// N-1. Fixtures: real stock ZMPs under tests/fixtures/zmp/sas.maps (the literals below are read off their
// params.txt) and the hand-made tests/fixtures/zmp/broken/ folders.
#include "cg/core/config_provider_folder.h"
#include "cg/core/config_provider_zip.h"
#include "cg/core/language_manager.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/zmp_info.h"
#include "cg/vector/appearance_of_mark_factory.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>

#include "../vector/fake_mark_picture.h"
#include "maptype_test_util.h"

using namespace Qt::StringLiterals;
using cg::core::ConfigProviderFolder;
using cg::core::ContentTypeRegistry;
using cg::core::LanguageManager;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::ProjectionSetFactory;
using cg::maptype::ZmpConfig;
using cg::maptype::ZmpError;
using cg::maptype::ZmpInfo;
using cg::maptype::test::ensureGuiApp;
using cg::maptype::test::fixtureDir;

namespace {

constexpr int kEn = 0, kEs = 1, kRu = 4, kUk = 6; // en + the stock .mo names in name order: es fa fr ru tr uk

void write(const QDir& dir, const QString& name, const QByteArray& data) {
    QFile f(dir.filePath(name));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
}

struct Env {
    QTemporaryDir langDir;
    std::unique_ptr<LanguageManager> languages;
    ProjectionSetFactory projections{HashSeed{}, std::make_shared<const DatumFactory>(HashSeed{})};
    ZmpConfig zmpConfig;
    cg::vector::AppearanceOfMarkFactory appearanceFactory;

    Env() {
        ensureGuiApp();
        for (const char* code : {"uk", "ru", "es", "tr", "fa", "fr"}) {
            write(QDir(langDir.path()), QString::fromLatin1(code) + u".mo"_s, {});
        }
        languages = std::make_unique<LanguageManager>(langDir.path());
    }

    std::shared_ptr<ZmpInfo> load(const QString& dir, const QString& name = {}, int pnum = 7) const {
        return std::make_shared<ZmpInfo>(zmpConfig, *languages, projections, ContentTypeRegistry::standard(),
                                         appearanceFactory, nullptr, name.isEmpty() ? dir : name,
                                         ConfigProviderFolder::create(dir), pnum);
    }
    std::shared_ptr<ZmpInfo> stock(const QString& relative) const {
        return load(fixtureDir() + u"/zmp/sas.maps/"_s + relative, relative);
    }
    std::shared_ptr<ZmpInfo> broken(const QString& name) const {
        return load(fixtureDir() + u"/zmp/broken/"_s + name);
    }
};

QByteArray fileBytes(const QString& relative) {
    QFile f(fixtureDir() + u"/zmp/sas.maps/"_s + relative);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    return f.readAll();
}

QString zmpErrorText(const Env& env, const QString& name, ZmpError::Kind kind) {
    try {
        (void)env.broken(name);
    } catch (const ZmpError& e) {
        EXPECT_EQ(e.kind(), kind);
        return e.message();
    }
    ADD_FAILURE() << "no ZmpError for " << name.toStdString();
    return {};
}

// A throw-away ZMP folder for behaviours that no stock ZMP shows (icon names, info pages, defaults).
struct TempZmp {
    QTemporaryDir dir;
    explicit TempZmp(const QByteArray& params = {}) {
        write(QDir(dir.path()), u"params.txt"_s,
              "[PARAMS]\r\nGUID={11111111-2222-3333-4444-555555555555}\r\n" + params);
    }
    void add(const QString& name, const QByteArray& data) const { write(QDir(dir.path()), name, data); }
};

TEST(ZmpInfo, PlainRasterMap_4umaps) {
    CG_SKIP_IF_NO_CORPUS();
    Env env;
    const auto z = env.stock(u"_osm/4umaps.eu.zmp"_s);
    EXPECT_EQ(z->guid(), QUuid::fromString(u"{D643C5A1-3333-4387-9BE2-DDDF185C117E}"_s));
    EXPECT_FALSE(z->isLayer());
    EXPECT_EQ(z->version(), u""_s);
    EXPECT_EQ(z->layerZOrder(), 0);
    EXPECT_EQ(z->license()->getString(kRu), u"(c) OpenStreetMap contributors, CC-BY-SA"_s);
    EXPECT_EQ(z->fileName(), u"_osm/4umaps.eu.zmp"_s);
    const auto gui = z->gui();
    EXPECT_EQ(gui->name()->getString(kRu), u"OSM 4umaps.eu"_s);
    EXPECT_EQ(gui->sortIndex(), 116);
    EXPECT_EQ(gui->hotKey(), 0);
    EXPECT_FALSE(gui->separator());
    EXPECT_TRUE(gui->enabled());
    EXPECT_EQ(gui->parentSubMenu()->getString(kEn), u"Hike"_s);
    EXPECT_EQ(gui->parentSubMenu()->getString(kEs), u"Hike"_s); // no ParentSubMenu_es: the plain value
    EXPECT_EQ(gui->parentSubMenu()->getString(kRu), u"Туристические"_s);
    EXPECT_EQ(gui->parentSubMenu()->getString(kUk), u"Топокарти"_s);
    EXPECT_EQ(gui->infoUrl()->getString(kRu), u"/info.txt"_s);
    EXPECT_EQ(gui->bmp24().size(), QSize(24, 24));
    EXPECT_EQ(gui->bmp18(), gui->bmp24()); // no 18.bmp: the 24 icon

    EXPECT_EQ(z->tileDownloadRequestBuilderConfig()->urlBase,
              "https://tileserver.4umaps.com/{z}/{x}/{y}.png");
    EXPECT_TRUE(z->tileDownloadRequestBuilderConfig()->serverNames.isEmpty());
    EXPECT_FALSE(z->tileDownloadRequestBuilderConfig()->isUseDownloader);
    const auto d = z->tileDownloaderConfig();
    EXPECT_TRUE(d->enabled);
    EXPECT_FALSE(d->allowUseCookie);
    EXPECT_FALSE(d->detectMimeType);
    EXPECT_EQ(d->expectedMimeTypes, "image/png");
    EXPECT_EQ(d->defaultMimeType, "image/jpg");
    EXPECT_EQ(d->waitIntervalMs, 0U);
    EXPECT_EQ(d->maxConnectToServerCount, 4U); // ZmpConfig default
    EXPECT_EQ(d->iteratorSubRectSize, QPoint(1, 1));
    const auto s = z->storageConfig();
    EXPECT_EQ(s->nameInCache, u"4umaps.eu"_s);
    EXPECT_EQ(s->cacheType, cg::storage::CacheTypeId::Default);
    EXPECT_EQ(s->tileFileExt, ".png");
    EXPECT_TRUE(s->mainContentType.isEmpty());
    EXPECT_TRUE(s->abilities.allowRead && s->abilities.allowScan && s->abilities.allowAdd &&
                s->abilities.allowDelete && s->abilities.allowReplace);
    EXPECT_TRUE(s->useMemCache);
    EXPECT_EQ(s->memCacheCapacity, 100);
    EXPECT_EQ(s->memCacheTtl.count(), 60000);
    EXPECT_EQ(s->memCacheClearStrategy, cg::storage::ClearStrategy::ByYoungest);
    EXPECT_TRUE(z->abilities()->isShowOnSmMap && z->abilities()->useDownload);
    EXPECT_FALSE(z->tilePostDownloadCropConfig()->isCropOnDownload());
    EXPECT_FALSE(z->tilePostDownloadCropConfig()->isCutOnDownload());
    EXPECT_EQ(z->coverage()->centerZoom(), 0);
    EXPECT_TRUE(z->emptyTileSamples().empty());
    EXPECT_TRUE(z->banTileSamples().empty());
    EXPECT_EQ(z->projectionSet(), z->viewProjectionSet());
}

TEST(ZmpInfo, ScriptedMapWithEmptyTilesAndHotKey_BingSat) {
    CG_SKIP_IF_NO_CORPUS();
    Env env;
    const auto z = env.stock(u"Bing/Bing_Sat.zmp"_s);
    EXPECT_EQ(z->guid(), QUuid::fromString(u"{9A360A54-7A70-402D-8A11-D570BD759B5B}"_s));
    EXPECT_EQ(z->version(), u"0"_s); // key spelled "version" (case-insensitive)
    EXPECT_EQ(z->gui()->name()->getString(kEn), u"Bing - Satellite"_s);
    EXPECT_EQ(z->gui()->name()->getString(kRu), u"Bing - Спутник"_s);
    EXPECT_EQ(z->gui()->hotKey(), 86);
    EXPECT_EQ(z->gui()->sortIndex(), 10);
    EXPECT_EQ(z->tileDownloaderConfig()->expectedMimeTypes, "image/jpeg; image/png");
    EXPECT_EQ(z->storageConfig()->nameInCache, u"vesat"_s);
    EXPECT_EQ(z->storageConfig()->tileFileExt, ".jpg");
    ASSERT_EQ(z->emptyTileSamples().size(), 1U); // EmptyTiles/NA.png
    EXPECT_EQ(z->emptyTileSamples()[0], fileBytes(u"Bing/Bing_Sat.zmp/EmptyTiles/NA.png"_s));
    EXPECT_TRUE(z->banTileSamples().empty());
    // The script is reachable raw through the container (spec 2.2).
    EXPECT_EQ(z->dataProvider()->binary(u"GetUrlScript.txt"),
              fileBytes(u"Bing/Bing_Sat.zmp/GetUrlScript.txt"_s));
    EXPECT_EQ(z->gui()->bmp24().size(), QSize(24, 24)); // stock 24.bmp; there is no 18.bmp
    EXPECT_EQ(z->gui()->bmp18(), z->gui()->bmp24());
}

// T-0273 (L-155, D row): a zipped ZMP finds its EmptyTiles folder, so its samples are the folder map's.
TEST(ZmpInfo, ZippedMapLoadsTheSamplesOfTheFolderMap) {
    CG_SKIP_IF_NO_CORPUS();
    Env env;
    const auto folder = env.stock(u"_osm/Gps_tiles.zmp"_s);
    const QString zipPath = fixtureDir() + u"/zmp/zipped/Gps_tiles_stored.zmp"_s;
    const auto opened = cg::core::ConfigProviderZip::open(zipPath);
    ASSERT_TRUE(opened.has_value());
    const auto zip = std::make_shared<ZmpInfo>(env.zmpConfig, *env.languages, env.projections,
                                               ContentTypeRegistry::standard(), env.appearanceFactory,
                                               nullptr, zipPath, *opened, 7);
    std::vector<QByteArray> zipSamples = zip->emptyTileSamples();
    std::vector<QByteArray> folderSamples = folder->emptyTileSamples();
    ASSERT_EQ(zipSamples.size(), 3U); // EmptyTiles/: NA.png, old.png, older.png
    std::ranges::sort(zipSamples);
    std::ranges::sort(folderSamples);
    EXPECT_EQ(zipSamples, folderSamples);
    EXPECT_TRUE(zip->banTileSamples().empty());
}

TEST(ZmpInfo, BothIconsAndMagentaKeyColour_2gis) {
    CG_SKIP_IF_NO_CORPUS();
    Env env;
    const auto z = env.stock(u"2gis.ru.zmp"_s);
    EXPECT_EQ(z->gui()->sortIndex(), 170);
    EXPECT_EQ(z->gui()->bmp24().size(), QSize(24, 24));
    EXPECT_EQ(z->gui()->bmp18().size(), QSize(18, 18));
    // 24.bmp holds 97 pixels of 0xFF00FF; they become transparent black (UpdateBmpTransparency).
    const QImage icon = z->gui()->bmp24();
    int transparent = 0;
    for (int y = 0; y < icon.height(); ++y) {
        for (int x = 0; x < icon.width(); ++x) {
            transparent += icon.pixel(x, y) == 0U ? 1 : 0;
        }
    }
    EXPECT_EQ(transparent, 97);
    // ContentType is spelled with quotes in the file; they are kept (spec 2.2).
    EXPECT_EQ(z->tileDownloaderConfig()->expectedMimeTypes, "\"image/png\"");
    EXPECT_EQ(z->tileDownloaderConfig()->defaultMimeType, "image/png");
    EXPECT_EQ(z->tileDownloadRequestBuilderConfig()->urlBase, "http://tile.maps.2gis.com/tiles?v=1.3&x=");
}

TEST(ZmpInfo, KmlLayerWithMimeSubst_Wiki) {
    CG_SKIP_IF_NO_CORPUS();
    Env env;
    const auto z = env.stock(u"Wikimapia/Wiki.zmp"_s);
    EXPECT_TRUE(z->isLayer());
    EXPECT_EQ(z->gui()->sortIndex(), 45);
    EXPECT_EQ(z->gui()->hotKey(), 87);
    EXPECT_EQ(z->gui()->name()->getString(kUk), u"WikiMapia - інтерактивна"_s);
    EXPECT_EQ(z->gui()->name()->getString(kEs), u"Wikimapia - interactive"_s);
    EXPECT_EQ(z->storageConfig()->tileFileExt, ".kml");
    EXPECT_EQ(z->storageConfig()->mainContentType, "application/vnd.sas.wikimapia.kml+xml");
    EXPECT_EQ(z->tileDownloaderConfig()->expectedMimeTypes, "text/xml; charset=UTF-8");
    EXPECT_EQ(z->contentTypeSubst().substitute("text/xml; charset=UTF-8"),
              "application/vnd.google-earth.kml+xml");
}

TEST(ZmpInfo, ViewInfoSectionAndStorageAbilities_GoogleEarth) {
    CG_SKIP_IF_NO_CORPUS();
    Env env;
    const auto z = env.stock(u"GoogleEarth/GoogleEarth.Direct/Imagery/GoogleEarth.Earth.zmp"_s);
    EXPECT_EQ(z->gui()->sortIndex(), 1001);
    EXPECT_NE(z->projectionSet(), z->viewProjectionSet());
    EXPECT_TRUE(z->projectionSet()->isSame(*env.projections.byCode(4326, 1)));
    EXPECT_TRUE(z->viewProjectionSet()->isSame(*env.projections.byCode(3857, 1))); // [ViewInfo] EPSG=3785
    const auto s = z->storageConfig();
    EXPECT_EQ(s->cacheType, cg::storage::CacheTypeId::Ge);
    EXPECT_EQ(s->nameInCache, u""_s);
    EXPECT_FALSE(z->tileDownloaderConfig()->enabled); // UseDwn=0
    EXPECT_FALSE(z->abilities()->useDownload);
    EXPECT_TRUE(s->abilities.allowRead);
    EXPECT_FALSE(s->abilities.allowScan); // AllowScan=0
    EXPECT_FALSE(s->abilities.allowAdd);  // IsReadOnly=1 gives the AllowAdd/Delete/Replace default
    EXPECT_FALSE(s->abilities.allowDelete);
    EXPECT_FALSE(s->abilities.allowReplace);
}

TEST(ZmpInfo, Cp1251ParamsDecoded_BeelineIoT) {
    CG_SKIP_IF_NO_CORPUS();
    Env env;
    const auto z = env.stock(u"_Coverage/Beeline/Beeline_IoT.zmp"_s);
    EXPECT_TRUE(z->isLayer());
    EXPECT_EQ(z->gui()->name()->getString(kEn), u"Beeline - IoT"_s);
    EXPECT_EQ(z->gui()->name()->getString(kRu), u"Билайн - IoT"_s);
    EXPECT_EQ(z->gui()->parentSubMenu()->getString(kRu), u"Сотовые сети"_s);
    EXPECT_EQ(z->gui()->sortIndex(), -1); // no pnum
    EXPECT_TRUE(z->projectionSet()->isSame(*env.projections.byCode(3857, 1)));
    EXPECT_TRUE(z->tileDownloaderConfig()->detectMimeType);
    EXPECT_EQ(z->tileDownloaderConfig()->expectedMimeTypes, "image/jpeg,image/png");
}

TEST(ZmpInfo, BrokenZmpsGiveTheLegacyTexts) {
    CG_SKIP_IF_NO_FIXTURE(fixtureDir() + u"/zmp/broken"_s);
    Env env;
    EXPECT_EQ(zmpErrorText(env, u"no_params.zmp"_s, ZmpError::Kind::IniNotFound),
              u"Not found \"params.txt\" in zmp"_s);
    EXPECT_EQ(zmpErrorText(env, u"no_params_section.zmp"_s, ZmpError::Kind::ParamsNotFound),
              u"Not found PARAMS section in zmp"_s);
    for (const QString& name :
         {u"no_guid.zmp"_s, u"bad_guid_unbraced.zmp"_s, u"zero_guid.zmp"_s}) { // LD-8, N-1
        EXPECT_EQ(zmpErrorText(env, name, ZmpError::Kind::Guid), u"Empty GUID"_s) << name.toStdString();
    }
}

TEST(ZmpInfo, BadIteratorSubRectSizeIsAnRtlMessageWithoutTheMapName) { // LD-7
    CG_SKIP_IF_NO_FIXTURE(fixtureDir() + u"/zmp/broken"_s);
    Env env;
    try {
        (void)env.broken(u"bad_iterator_one_item.zmp"_s);
        FAIL();
    } catch (const ZmpError&) {
        FAIL();
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "List index out of bounds (1)");
    }
    TempZmp strict("IteratorSubRectSize=1 2,3\r\n"); // blanks are data in StrictDelimiter mode
    try {
        (void)env.load(strict.dir.path());
        FAIL();
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "'1 2' is not a valid integer value");
    }
    try {
        (void)env.broken(u"bad_iterator_not_int.zmp"_s);
        FAIL();
    } catch (const ZmpError&) {
        FAIL();
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "'x' is not a valid integer value");
    }
}

TEST(ZmpInfo,
     UnknownExtensionIsRejectedAtCreation) { // OQ-3, D-74; legacy keeps a nil content type and crashes later
    CG_SKIP_IF_NO_FIXTURE(fixtureDir() + u"/zmp/broken"_s);
    Env env;
    const QString text = zmpErrorText(env, u"unknown_ext.zmp"_s, ZmpError::Kind::UnknownContentType);
    EXPECT_TRUE(text.startsWith(u"Unknown tile content type \"\" / extension \".xyz\" in map "_s))
        << text.toStdString();
    // A known MainStorageContentType rescues an unknown extension, as in u_MapType.pas:376-381.
    TempZmp ok("Ext=.xyz\r\nMainStorageContentType=image/png\r\n");
    EXPECT_NO_THROW((void)env.load(ok.dir.path()));
}

TEST(ZmpInfo, DefaultsOfAnEmptyParamsSection) {
    Env env;
    TempZmp tmp;
    const auto z = env.load(tmp.dir.path(), u"x.zmp"_s, 42);
    EXPECT_EQ(z->gui()->name()->getString(kRu), u"map#42"_s);
    EXPECT_EQ(z->gui()->sortIndex(), -1);
    EXPECT_EQ(z->gui()->infoUrl()->getString(kEn), u""_s);
    EXPECT_TRUE(z->gui()->enabled());
    EXPECT_EQ(z->storageConfig()->tileFileExt, ".jpg");
    EXPECT_EQ(z->tileDownloaderConfig()->expectedMimeTypes, "image/jpg");
    EXPECT_EQ(z->tileDownloaderConfig()->iteratorSubRectSize, QPoint(1, 1));
    EXPECT_FALSE(z->storageConfig()->abilities.isReadOnly());
}

TEST(ZmpInfo, DefaultIconAndPngFallback) {
    Env env;
    TempZmp none;
    const QImage def = env.load(none.dir.path(), u"x.zmp"_s, 42)->gui()->bmp24();
    EXPECT_EQ(def.size(), QSize(32, 32));
    EXPECT_EQ(def.pixel(0, 0), 0xFFBFBFBFU);
    TempZmp withPng;
    QImage png(5, 6, QImage::Format_ARGB32);
    png.fill(Qt::red);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    png.save(&buf, "PNG");
    withPng.add(u"24.png"_s, bytes);
    withPng.add(u"icon_small.png"_s, bytes);
    EXPECT_EQ(env.load(withPng.dir.path())->gui()->bmp24().size(), QSize(5, 6)); // 24.bmp absent -> 24.png
    TempZmp named("SmallIconName=icon_small.png\r\n");
    named.add(u"icon_small.png"_s, bytes);
    EXPECT_EQ(env.load(named.dir.path())->gui()->bmp18().size(), QSize(5, 6));
}

TEST(ZmpInfo, InfoPageChain) { // spec 2.5.4; an empty file counts as absent
    Env env;
    TempZmp tmp;
    tmp.add(u"info.txt"_s, "general");
    tmp.add(u"info_ru.txt"_s, "russian");
    tmp.add(u"index_uk.html"_s, "<html/>");
    tmp.add(u"info_uk.txt"_s, "ignored");
    tmp.add(u"info_es.txt"_s, ""); // empty: falls back to the default
    const auto info = env.load(tmp.dir.path())->gui()->infoUrl();
    EXPECT_EQ(info->getString(kEn), u"/info.txt"_s);
    EXPECT_EQ(info->getString(kRu), u"/info_ru.txt"_s);
    EXPECT_EQ(info->getString(kUk), u"/index_uk.html"_s);
    EXPECT_EQ(info->getString(kEs), u"/info.txt"_s);
    tmp.add(u"index.html"_s, "<html/>");
    EXPECT_EQ(env.load(tmp.dir.path())->gui()->infoUrl()->getString(kEn), u"/index.html"_s);
}

TEST(ZmpInfo, ScriptKeepsItsBomThroughTheContainer) { // LD-14
    Env env;
    TempZmp tmp;
    tmp.add(u"GetUrlScript.txt"_s, "\xEF\xBB\xBF"
                                   "begin end.");
    EXPECT_TRUE(
        env.load(tmp.dir.path())->dataProvider()->binary(u"GetUrlScript.txt").startsWith("\xEF\xBB\xBF"));
}

TEST(ZmpInfo, HotKeyTruncatesToAWordAndHotKeyOverridesDefHotKey) {
    Env env;
    TempZmp a("DefHotKey=87\r\nHotKey=65537\r\n");
    EXPECT_EQ(env.load(a.dir.path())->gui()->hotKey(), 1); // Integer -> TShortCut (Word)
    TempZmp b("DefHotKey=87\r\n");
    EXPECT_EQ(env.load(b.dir.path())->gui()->hotKey(), 87);
}

// --- LoadVectorAppearanceConfig, u_ZmpInfo.pas:1001-1062 (T-K). No stock ZMP carries a Vector section,
// so the ZMPs are hand-made; the section names are the INI form of PARAMS/Vector/Point (PARAMS_Vector_Point).
TEST(ZmpInfo, VectorAppearanceAllThreeSubItems) {
    Env env;
    TempZmp tmp(
        "\r\n[PARAMS_Vector]\r\nIsX=1\r\n"
        "[PARAMS_Vector_Point]\r\nTextColorHex=$01020304\r\nShadowColorHex=$05060708\r\nFontSize=20\r\n"
        "IconSize=48\r\nIconName=pin\r\nIsForceTextColor=0\r\n"
        "[PARAMS_Vector_Line]\r\nLineColorHex=$A6FF0000\r\nLineWidth=3\r\nIsForceLineWidth=0\r\n"
        "[PARAMS_Vector_Poly]\r\nLineColorHex=$11223344\r\nLineWidth=5\r\nFillColorHex=$55667788\r\n"
        "IsForceFillColor=0\r\n");
    const auto z = env.load(tmp.dir.path());
    const auto point = z->pointParams();
    ASSERT_TRUE(point);
    EXPECT_EQ(point->captionAppearance()->textColor(), 0x01020304U);
    EXPECT_EQ(point->captionAppearance()->textBgColor(), 0x05060708U);
    EXPECT_EQ(point->captionAppearance()->fontSize(), 20);
    EXPECT_EQ(point->iconAppearance()->markerSize(), 48);
    EXPECT_EQ(point->iconAppearance()->picName(), u"pin"_s);
    EXPECT_FALSE(point->iconAppearance()->pic()); // null list: name kept, no picture (:487-491)
    EXPECT_FALSE(point->isForceTextColor());      // the one flag given as 0
    EXPECT_TRUE(point->isForceTextBgColor());     // the four absent flags default to True
    EXPECT_TRUE(point->isForceFontSize());
    EXPECT_TRUE(point->isForceMarkerSize());
    EXPECT_TRUE(point->isForcePicName());
    const auto line = z->lineParams();
    ASSERT_TRUE(line);
    EXPECT_EQ(line->lineAppearance()->lineColor(), 0xA6FF0000U);
    EXPECT_EQ(line->lineAppearance()->lineWidth(), 3);
    // LV-1 (L-130): TImportLineParams stores the width flag in both slots (u_ImportConfig.pas:231), so the
    // absent colour flag (default True) is overwritten by IsForceLineWidth=0.
    EXPECT_FALSE(line->isForceLineWidth());
    EXPECT_FALSE(line->isForceLineColor());
    const auto poly = z->polyParams();
    ASSERT_TRUE(poly);
    EXPECT_EQ(poly->borderAppearance()->lineColor(), 0x11223344U);
    EXPECT_EQ(poly->borderAppearance()->lineWidth(), 5);
    EXPECT_EQ(poly->fillAppearance()->fillColor(), 0x55667788U);
    EXPECT_FALSE(poly->isForceFillColor());
    EXPECT_TRUE(poly->isForceLineWidth()); // absent -> True; LV-1 copies it into the colour slot too
    EXPECT_TRUE(poly->isForceLineColor());
}

// Each force flag of the point reads its own key (:1024-1028): one key at a time is 0, the other four stay
// True.
TEST(ZmpInfo, VectorAppearancePointFlagKeys) {
    Env env;
    const char* const keys[] = {"IsForceTextColor", "IsForceShadowColor", "IsForceFontSize",
                                "IsForceIconSize", "IsForceIconName"};
    for (int zero = 0; zero < 5; ++zero) {
        TempZmp tmp(QByteArray("\r\n[PARAMS_Vector]\r\nIsX=1\r\n[PARAMS_Vector_Point]\r\n") + keys[zero] +
                    "=0\r\n");
        const auto point = env.load(tmp.dir.path())->pointParams();
        ASSERT_TRUE(point);
        const bool flags[] = {point->isForceTextColor(), point->isForceTextBgColor(),
                              point->isForceFontSize(), point->isForceMarkerSize(), point->isForcePicName()};
        for (int i = 0; i < 5; ++i) {
            EXPECT_EQ(flags[i], i != zero) << keys[zero] << " flag " << i;
        }
    }
}

// The picture list reaches readAppearancePoint through ZmpInfo and through ZmpInfoSet (:610, :782-783).
TEST(ZmpInfo, VectorAppearancePictureListPassThrough) {
    Env env;
    const auto list = std::make_shared<const cg::vector::test::FakeMarkPictureList>();
    const QByteArray params = "\r\n[PARAMS_Vector]\r\nIsX=1\r\n[PARAMS_Vector_Point]\r\nIconName=b\r\n";
    TempZmp tmp(params);
    const auto z = std::make_shared<ZmpInfo>(env.zmpConfig, *env.languages, env.projections,
                                             ContentTypeRegistry::standard(), env.appearanceFactory, list,
                                             u"x.zmp"_s, ConfigProviderFolder::create(tmp.dir.path()), 0);
    ASSERT_TRUE(z->pointParams());
    ASSERT_TRUE(z->pointParams()->iconAppearance()->pic());
    EXPECT_EQ(z->pointParams()->iconAppearance()->pic()->name(), u"b"_s);
    EXPECT_EQ(list->lookups, 1);

    QTemporaryDir root;
    ASSERT_TRUE(QDir(root.path()).mkdir(u"m.zmp"_s));
    write(QDir(root.path() + u"/m.zmp"_s), u"params.txt"_s,
          "[PARAMS]\r\nGUID={11111111-2222-3333-4444-555555555555}\r\n" + params);
    QStringList errors;
    cg::maptype::ZmpInfoSet set(env.zmpConfig, *env.languages, env.projections,
                                ContentTypeRegistry::standard(), env.appearanceFactory, list, root.path(),
                                {u"m.zmp"_s}, [&errors](const QString& m) { errors.append(m); });
    EXPECT_TRUE(errors.isEmpty());
    const auto loaded = set.zmpByGuid(QUuid::fromString(u"{11111111-2222-3333-4444-555555555555}"_s));
    ASSERT_TRUE(loaded);
    ASSERT_TRUE(loaded->pointParams());
    ASSERT_TRUE(loaded->pointParams()->iconAppearance()->pic());
    EXPECT_EQ(loaded->pointParams()->iconAppearance()->pic()->name(), u"b"_s);
    EXPECT_EQ(list->lookups, 2);
}

TEST(ZmpInfo, VectorAppearanceLineOnlyAndNone) {
    Env env;
    TempZmp lineOnly(
        "\r\n[PARAMS_Vector]\r\nIsX=1\r\n[PARAMS_Vector_Line]\r\nLineWidth=7\r\nIsForceLineColor=0\r\n");
    const auto z = env.load(lineOnly.dir.path());
    EXPECT_FALSE(z->pointParams());
    EXPECT_FALSE(z->polyParams());
    ASSERT_TRUE(z->lineParams());
    EXPECT_EQ(z->lineParams()->lineAppearance()->lineWidth(), 7);
    EXPECT_EQ(z->lineParams()->lineAppearance()->lineColor(),
              0xFFFFFFFFU); // built-in default (vector.md 2.9)
    // IsForceLineColor=0 is lost to the absent IsForceLineWidth (default True): LV-1 again.
    EXPECT_TRUE(z->lineParams()->isForceLineColor());
    EXPECT_TRUE(z->lineParams()->isForceLineWidth());

    TempZmp polyOnly(
        "\r\n[PARAMS_Vector]\r\nIsX=1\r\n[PARAMS_Vector_Poly]\r\nLineWidth=9\r\n"); // the rest at default
    const auto p = env.load(polyOnly.dir.path());
    EXPECT_FALSE(p->pointParams());
    EXPECT_FALSE(p->lineParams());
    ASSERT_TRUE(p->polyParams());
    EXPECT_EQ(p->polyParams()->borderAppearance()->lineColor(), 0xFFFFFFFFU);
    EXPECT_EQ(p->polyParams()->borderAppearance()->lineWidth(), 9);
    EXPECT_EQ(p->polyParams()->fillAppearance()->fillColor(), 0x33FFFFFFU);
    EXPECT_TRUE(p->polyParams()->isForceFillColor());
    EXPECT_TRUE(p->polyParams()->isForceLineWidth());

    TempZmp none; // no Vector section at all
    const auto n = env.load(none.dir.path());
    EXPECT_FALSE(n->pointParams());
    EXPECT_FALSE(n->lineParams());
    EXPECT_FALSE(n->polyParams());

    // A Vector section without sub-items, and a sub-item without its Vector parent section: all null.
    // (A keyless section is deliberately not used anywhere: legacy SectionExists vs port hasSection.)
    for (const char* text :
         {"\r\n[PARAMS_Vector]\r\nIsX=1\r\n", "\r\n[PARAMS_Vector_Line]\r\nLineWidth=7\r\n"}) {
        TempZmp odd(text);
        const auto e = env.load(odd.dir.path());
        EXPECT_FALSE(e->pointParams());
        EXPECT_FALSE(e->lineParams());
        EXPECT_FALSE(e->polyParams());
    }
}

// FEAT-NET-002 corpus sweep: every stock ZMP loads, or fails with one of the legacy texts.
TEST(ZmpInfo, CorpusSweep) {
    CG_SKIP_IF_NO_CORPUS();
    Env env;
    const QString root = fixtureDir() + u"/zmp/sas.maps"_s;
    int total = 0;
    std::map<QString, int> failures;
    QSet<QUuid> guids;
    QDirIterator it(root, {u"*.zmp"_s}, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        ++total;
        try {
            guids.insert(env.load(path, QDir(root).relativeFilePath(path))->guid());
        } catch (const ZmpError& e) {
            ++failures[e.message()];
        } catch (const std::exception& e) {
            ++failures[QString::fromUtf8(e.what())];
        }
    }
    EXPECT_EQ(total, 514);
    for (const auto& [text, count] : failures) {
        ADD_FAILURE() << count << " x " << text.toStdString();
    }
    EXPECT_EQ(guids.size(), 514);
}

} // namespace
