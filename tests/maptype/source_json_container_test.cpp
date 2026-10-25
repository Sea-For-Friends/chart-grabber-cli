// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/maptype/source_json_container (T-0224). NEW code, no legacy unit and no FEAT id: the
// expectations are the literals of docs/specs/map-source-json.md (sections 2, 3, 3.1, 6.1-6.4) and of the
// existing ZMP rules the JSON source must meet (the same-GUID report, the ZMP twin of every example).
// Fixtures: tests/fixtures/maptype/json (README there); the twins are hand-written ZMP folders.
#include "cg/maptype/source_json_container.h"
#include "cg/maptype/tile_download_request_builder_config.h"
#include "cg/maptype/zmp_file_names_iterator_factory.h"
#include "cg/maptype/zmp_info_set.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "maptype_test_util.h"

using namespace Qt::StringLiterals;
using cg::core::IConfigProviderPtr;
using cg::maptype::IZmpInfoPtr;
using cg::maptype::SourceJsonContainer;
using cg::maptype::zmpFileNames;
using cg::maptype::test::fixtureDir;

namespace {

constexpr int kEn = 0, kRu = 4; // as zmp_info_set_test.cpp: en + the stock .mo names es fa fr ru tr uk

using Env = cg::maptype::test::ZmpEnv;

QString jsonDir() {
    return fixtureDir() + u"/maptype/json"_s;
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    return f.readAll();
}

void writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
}

// The smallest valid source (section 3: the required keys, no script).
QJsonObject baseObject() {
    return QJsonDocument::fromJson(
               R"({"format":1,"guid":"{11111111-2222-3333-4444-555555555555}","name":"N",
                   "url":"http://h/{z}/{x}/{y}.png","contentType":"image/png","ext":".png","nameInCache":"n_1"})")
        .object();
}

// baseObject() with one key set to the JSON text `valueJson`; an empty `valueJson` removes the key.
QByteArray withKey(const QString& key, const QString& valueJson) {
    QJsonObject o = baseObject();
    if (valueJson.isEmpty()) {
        o.remove(key);
    } else {
        o.insert(key, QJsonDocument::fromJson((u"["_s + valueJson + u"]"_s).toUtf8()).array().first());
    }
    return QJsonDocument(o).toJson();
}

// The [PARAMS] section of a parsed source, as ZmpInfo reads it.
IConfigProviderPtr paramsOf(const SourceJsonContainer& c) {
    const IConfigProviderPtr file = c.subItem(u"params.txt");
    return file ? file->subItem(u"PARAMS") : nullptr;
}

std::shared_ptr<const SourceJsonContainer> mustParse(const QByteArray& bytes, const QString& folder = {}) {
    auto parsed = SourceJsonContainer::parse(bytes, folder);
    EXPECT_TRUE(parsed.has_value()) << (parsed ? std::string() : parsed.error().what().toStdString());
    return parsed ? *parsed : nullptr;
}

// --- file name predicate and the walk (section 2: Location)
// --------------------------------------------------

TEST(SourceJsonName, EndsWithDotSourceDotJsonAsciiCaseInsensitive) {
    struct Row {
        QString name;
        bool want;
    };
    const Row table[] = {
        {u"OpenSeaMap-Seamarks.source.json"_s, true},
        {u"A.SOURCE.JSON"_s, true},
        {u"b.Source.Json"_s, true},
        {u"Город.source.json"_s, true},
        {u"a.json"_s, false},
        {u"source.json"_s, false},
        {u"a.source.jsonx"_s, false},
        {u"a.source.json.bak"_s, false},
        {u"a.zmp"_s, false},
    };
    for (const Row& r : table) {
        EXPECT_EQ(SourceJsonContainer::isSourceJsonName(r.name), r.want) << r.name.toStdString();
    }
}

TEST(SourceJsonWalk, ReturnsOnlyRegularSourceJsonFilesBesideZmps) {
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + u"/a.source.json"_s, "{}");
    writeFile(root + u"/sub/Deep.SOURCE.JSON"_s, "{}");
    writeFile(root + u"/l1/l2/l3/l4/l5/l6/m.source.json"_s,
              "{}"); // the seventh folder level: within the limit
    writeFile(root + u"/l1/l2/l3/l4/l5/l6/l7/m.source.json"_s, "{}"); // the eighth level: past the limit
    writeFile(root + u"/.hid/y.source.json"_s, "{}");                 // inside a dot folder
    writeFile(root + u"/plain.json"_s, "{}");
    writeFile(root + u"/x.source.json.txt"_s, "{}");
    writeFile(root + u"/z.zmp/params.txt"_s, "[PARAMS]\r\n");
    ASSERT_TRUE(
        QDir().mkpath(root + u"/folder.source.json/inner"_s)); // a directory with that name is no source
    writeFile(root + u"/folder.source.json/inner/q.source.json"_s, "{}");
    QStringList got = zmpFileNames(root);
    got.sort();
    QStringList want = {u"a.source.json"_s, u"sub/Deep.SOURCE.JSON"_s, u"l1/l2/l3/l4/l5/l6/m.source.json"_s,
                        u"z.zmp"_s, u"folder.source.json/inner/q.source.json"_s};
    want.sort();
    EXPECT_EQ(got, want);
}

// --- the worked examples 6.1-6.3 against their ZMP twins (acceptance 1)
// --------------------------------------

struct Example {
    QString stem;
    QUuid guid;
    QString nameEn, nameRu, groupEn, groupRu, license;
    bool layer;
    int sortIndex;
    QString version;
    QByteArray urlBase, serverNames, requestHead, contentTypes, ext, nameInCache;
    quint32 sleepMs, maxConnections;
};

const std::vector<Example>& examples() {
    static const std::vector<Example> v = {
        {u"ex61"_s, QUuid::fromString(u"{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}"_s), u"OpenSeaMap Seamarks"_s,
         u"OpenSeaMap Seamarks"_s, u"Sea"_s, u"Sea"_s, u"Seamarks (c) OpenSeaMap contributors, CC-BY-SA"_s,
         true, -1, u""_s, "https://tiles.openseamap.org/seamark/{z}/{x}/{y}.png", "", "", "image/png", ".png",
         "Osm_OpenSeaMap_Layer_Seamark", 0, 2},
        {u"ex62"_s, QUuid::fromString(u"{9A360A54-7A70-402D-8A11-D570BD759B5B}"_s), u"Bing - Satellite"_s,
         u"Bing - Спутник"_s, u"Satellite"_s, u"Спутниковые"_s, u""_s, false, 10, u"0"_s,
         "http://ecn.t0.tiles.virtualearth.net/tiles/a{q}.jpeg?g={ver}", "", "", "image/jpeg; image/png",
         ".jpg", "vesat", 0, 4},
        {u"ex63"_s, QUuid::fromString(u"{51C1079B-B55D-4DF7-A410-ADC118F53AB1}"_s), u"OpenTopoMap"_s,
         u"OpenTopoMap"_s, u"Land"_s, u"Land"_s,
         u"Map data (c) OpenStreetMap contributors, SRTM | Map style (c) OpenTopoMap (CC-BY-SA)"_s, false, -1,
         u""_s, "https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png", "a,b,c",
         "Referer: https://opentopomap.org/", "image/png", ".png", "Osm_Topo_Map", 200, 4},
    };
    return v;
}

IZmpInfoPtr loadOne(const Env& env, const QString& root, const QString& relativeName) {
    QStringList errors;
    const auto set = env.load(root, {relativeName}, errors);
    EXPECT_TRUE(errors.isEmpty()) << errors.join(u"\n"_s).toStdString();
    return set->guids().size() == 1 ? set->zmpByGuid(set->guids()[0]) : nullptr;
}

TEST(SourceJsonExamples, EachExampleLoadsWithTheLiteralsOfTheSpec) {
    const Env env;
    for (const Example& e : examples()) {
        const IZmpInfoPtr zmp = loadOne(env, jsonDir(), e.stem + u".source.json"_s);
        ASSERT_TRUE(zmp != nullptr) << e.stem.toStdString();
        const std::string n = e.stem.toStdString();
        EXPECT_EQ(zmp->guid(), e.guid) << n;
        EXPECT_EQ(zmp->fileName(), e.stem + u".source.json"_s) << n;
        EXPECT_EQ(zmp->gui()->name()->getString(kEn), e.nameEn) << n;
        EXPECT_EQ(zmp->gui()->name()->getString(kRu), e.nameRu) << n;
        EXPECT_EQ(zmp->gui()->parentSubMenu()->getString(kEn), e.groupEn) << n;
        EXPECT_EQ(zmp->gui()->parentSubMenu()->getString(kRu), e.groupRu) << n;
        EXPECT_EQ(zmp->license()->getString(kEn), e.license) << n;
        EXPECT_EQ(zmp->isLayer(), e.layer) << n;
        EXPECT_EQ(zmp->gui()->sortIndex(), e.sortIndex) << n;
        EXPECT_EQ(zmp->version(), e.version) << n;
        const auto builder = zmp->tileDownloadRequestBuilderConfig();
        EXPECT_EQ(builder->urlBase, e.urlBase) << n;
        EXPECT_EQ(builder->serverNames, e.serverNames) << n;
        EXPECT_EQ(builder->requestHeader, e.requestHead) << n;
        const auto downloader = zmp->tileDownloaderConfig();
        EXPECT_TRUE(downloader->enabled) << n; // no script: downloading stays on
        EXPECT_EQ(downloader->expectedMimeTypes, e.contentTypes) << n;
        EXPECT_EQ(downloader->waitIntervalMs, e.sleepMs) << n;
        EXPECT_EQ(downloader->maxConnectToServerCount, e.maxConnections) << n;
        EXPECT_EQ(zmp->storageConfig()->tileFileExt, e.ext) << n;
        EXPECT_EQ(zmp->storageConfig()->nameInCache, e.nameInCache) << n;
        EXPECT_EQ(zmp->projectionSet()->zoom(0)->projectionType()->projectionEpsg(), 3857) << n;
    }
}

TEST(SourceJsonExamples, EveryZmpInfoGetterEqualsTheSameSourceWrittenAsAZmpFolder) {
    const Env env;
    for (const Example& e : examples()) {
        const std::string n = e.stem.toStdString();
        const IZmpInfoPtr j = loadOne(env, jsonDir(), e.stem + u".source.json"_s);
        const IZmpInfoPtr z = loadOne(env, jsonDir(), u"zmp/"_s + e.stem + u".zmp"_s);
        ASSERT_TRUE(j != nullptr && z != nullptr) << n;
        EXPECT_EQ(j->guid(), z->guid()) << n;
        EXPECT_EQ(j->isLayer(), z->isLayer()) << n;
        EXPECT_EQ(j->layerZOrder(), z->layerZOrder()) << n;
        EXPECT_EQ(j->version(), z->version()) << n;
        for (const int lang : {kEn, kRu}) {
            EXPECT_EQ(j->gui()->name()->getString(lang), z->gui()->name()->getString(lang)) << n;
            EXPECT_EQ(j->gui()->parentSubMenu()->getString(lang), z->gui()->parentSubMenu()->getString(lang))
                << n;
            EXPECT_EQ(j->license()->getString(lang), z->license()->getString(lang)) << n;
        }
        EXPECT_EQ(j->gui()->sortIndex(), z->gui()->sortIndex()) << n;
        EXPECT_EQ(j->gui()->separator(), z->gui()->separator()) << n;
        EXPECT_EQ(j->gui()->enabled(), z->gui()->enabled()) << n;
        EXPECT_EQ(j->gui()->hotKey(), z->gui()->hotKey()) << n;
        EXPECT_EQ(j->gui()->bmp18(), z->gui()->bmp18()) << n;
        EXPECT_EQ(j->gui()->bmp24(), z->gui()->bmp24()) << n;
        const auto jb = j->tileDownloadRequestBuilderConfig(), zb = z->tileDownloadRequestBuilderConfig();
        EXPECT_EQ(jb->urlBase, zb->urlBase) << n;
        EXPECT_EQ(jb->serverNames, zb->serverNames) << n;
        EXPECT_EQ(jb->requestHeader, zb->requestHeader) << n;
        EXPECT_EQ(jb->isUseDownloader, zb->isUseDownloader) << n;
        const auto jd = j->tileDownloaderConfig(), zd = z->tileDownloaderConfig();
        EXPECT_EQ(jd->enabled, zd->enabled) << n;
        EXPECT_EQ(jd->waitIntervalMs, zd->waitIntervalMs) << n;
        EXPECT_EQ(jd->maxConnectToServerCount, zd->maxConnectToServerCount) << n;
        EXPECT_EQ(jd->detectMimeType, zd->detectMimeType) << n;
        EXPECT_EQ(jd->expectedMimeTypes, zd->expectedMimeTypes) << n;
        EXPECT_EQ(jd->defaultMimeType, zd->defaultMimeType) << n;
        EXPECT_EQ(jd->iteratorSubRectSize, zd->iteratorSubRectSize) << n;
        const auto js = j->storageConfig(), zs = z->storageConfig();
        EXPECT_EQ(js->tileFileExt, zs->tileFileExt) << n;
        EXPECT_EQ(js->nameInCache, zs->nameInCache) << n;
        EXPECT_EQ(js->cacheType, zs->cacheType) << n;
        EXPECT_EQ(j->abilities()->useDownload, z->abilities()->useDownload) << n;
        EXPECT_EQ(j->emptyTileSamples(), z->emptyTileSamples()) << n;
        EXPECT_EQ(j->projectionSet()->zoom(0)->projectionType()->projectionEpsg(),
                  z->projectionSet()->zoom(0)->projectionType()->projectionEpsg())
            << n;
    }
}

// --- the key mapping of section 3 and 3.1 (every key, distinct values)
// ---------------------------------------

constexpr auto kFullJson = u8R"({
  "format": 1, "guid": "{aBcDeF01-2345-6789-abcd-ef0123456789}",
  "name": {"": "Name", "ru": "Имя", "de": "Name-de"}, "group": {"": "G\\Sub", "ru": "Группа"},
  "sortIndex": 7, "layer": true, "layerZOrder": -3,
  "url": "http://h/{z}/{x}/{y}", "servers": ["a", "b", "c"],
  "headers": {"Referer": "http://r/", "Accept": "image/*"}, "version": "v9", "epsg": 3395,
  "contentType": "image/png;image/jpeg", "ext": ".png", "detectContentType": true,
  "defaultContentType": "image/jpeg", "nameInCache": "A.b_c-9", "license": {"": "Lic", "ru": "Лиц"},
  "maxConnections": 3, "sleepMs": 250, "crop": {"left": 1, "top": 2, "right": 3, "bottom": 4},
  "emptyTiles": ["AP8Q", "iVBORw0K"], "allowHttp": true, "termsOfUseWarning": "http://terms",
  "maxZoom": 18, "cacheType": 71,
  "snapshot": {"index": "http://i/", "type": "geojson", "item": "it", "timeField": "tf", "versionField": "vf", "pick": "nearest-now"}
})";

TEST(SourceJsonMapping, EveryKeyBecomesItsParamsKey) {
    const auto c = mustParse(QByteArray(reinterpret_cast<const char*>(kFullJson)));
    ASSERT_TRUE(c != nullptr);
    const IConfigProviderPtr p = paramsOf(*c);
    ASSERT_TRUE(p != nullptr);
    struct Row {
        QStringView key;
        QString want;
    };
    const Row strings[] = {
        {u"name", u"Name"_s},
        {u"name_ru", u"Имя"_s},
        {u"name_de", u"Name-de"_s},
        {u"ParentSubMenu", u"G\\Sub"_s},
        {u"ParentSubMenu_ru", u"Группа"_s},
        {u"DefURLBase", u"http://h/{z}/{x}/{y}"_s},
        {u"ServerNames", u"a,b,c"_s},
        {u"Version", u"v9"_s},
        {u"ContentType", u"image/png;image/jpeg"_s},
        {u"Ext", u".png"_s},
        {u"DefaultContentType", u"image/jpeg"_s},
        {u"NameInCache", u"A.b_c-9"_s},
        {u"License", u"Lic"_s},
        {u"License_ru", u"Лиц"_s},
        {u"TermsOfUseWarning", u"http://terms"_s},
        {u"SnapshotIndex", u"http://i/"_s},
        {u"SnapshotIndexType", u"geojson"_s},
        {u"SnapshotItem", u"it"_s},
        {u"SnapshotPick", u"nearest-now"_s}, // T-0337
        {u"SnapshotTimeField", u"tf"_s},
        {u"SnapshotVersionField", u"vf"_s},
    };
    for (const Row& r : strings) {
        EXPECT_EQ(p->readString(r.key, u"<absent>"_s), r.want) << r.key.toString().toStdString();
    }
    const std::pair<QStringView, int> ints[] = {
        {u"pnum", 7},        {u"LayerZOrder", -3}, {u"EPSG", 3395},    {u"MaxConnectToServerCount", 3},
        {u"Sleep", 250},     {u"TileRLeft", 1},    {u"TileRTop", 2},   {u"TileRRight", 3},
        {u"TileRBottom", 4}, {u"MaxZoom", 18},     {u"CacheType", 71},
    };
    for (const auto& [key, want] : ints) {
        EXPECT_EQ(p->readInt(key, -999), want) << key.toString().toStdString();
    }
    for (const QStringView key : {u"asLayer", u"DetectContentType", u"IsUseDownloaderInScript"}) {
        EXPECT_TRUE(p->readBool(key, false)) << key.toString().toStdString();
    }
    EXPECT_EQ(QUuid::fromString(p->readString(u"GUID", {})),
              QUuid::fromString(u"{abcdef01-2345-6789-abcd-ef0123456789}"_s));
    // RequestHead: the two headers, in any order, between the four characters backslash r backslash n
    QStringList headers = p->readString(u"RequestHead", {}).split(u"\\r\\n"_s);
    headers.sort();
    EXPECT_EQ(headers, (QStringList{u"Accept: image/*"_s, u"Referer: http://r/"_s}));
}

TEST(SourceJsonMapping, GetterValuesOfTheFullSourceThroughZmpInfo) {
    const Env env;
    QTemporaryDir tmp;
    writeFile(tmp.filePath(u"full.source.json"_s), QByteArray(reinterpret_cast<const char*>(kFullJson)));
    const IZmpInfoPtr zmp = loadOne(env, tmp.path(), u"full.source.json"_s);
    ASSERT_TRUE(zmp != nullptr);
    EXPECT_EQ(zmp->guid(), QUuid::fromString(u"{abcdef01-2345-6789-abcd-ef0123456789}"_s));
    EXPECT_EQ(zmp->layerZOrder(), -3);
    EXPECT_TRUE(zmp->isLayer());
    EXPECT_EQ(zmp->gui()->sortIndex(), 7);
    EXPECT_EQ(zmp->license()->getString(kRu), u"Лиц"_s);
    EXPECT_EQ(zmp->gui()->name()->getString(kRu), u"Имя"_s);
    EXPECT_EQ(zmp->projectionSet()->zoom(0)->projectionType()->projectionEpsg(), 3395);
    const auto d = zmp->tileDownloaderConfig();
    EXPECT_TRUE(d->detectMimeType);
    EXPECT_EQ(d->defaultMimeType, "image/jpeg");
    EXPECT_EQ(d->waitIntervalMs, 250u);
    EXPECT_EQ(d->maxConnectToServerCount, 3u);
    EXPECT_TRUE(zmp->tileDownloadRequestBuilderConfig()->isUseDownloader); // allowHttp
    EXPECT_EQ(static_cast<int>(zmp->storageConfig()->cacheType), 71);
    // emptyTiles: the base64 samples, decoded, in array order
    ASSERT_EQ(zmp->emptyTileSamples().size(), 2u);
    EXPECT_EQ(zmp->emptyTileSamples()[0], QByteArray("\x00\xFF\x10", 3));
    EXPECT_EQ(zmp->emptyTileSamples()[1], QByteArray("\x89PNG\r\n", 6));
    // headers become the CRLF-separated request header text once the request builder expands them
    cg::maptype::TileDownloadRequestBuilderConfig expanded(zmp->tileDownloadRequestBuilderConfig());
    QStringList lines = QString::fromLatin1(expanded.requestHeader()).split(u"\r\n"_s);
    lines.sort();
    EXPECT_EQ(lines, (QStringList{u"Accept: image/*"_s, u"Referer: http://r/"_s}));
}

TEST(SourceJsonMapping, OptionalKeysAbsentGiveTheZmpDefaults) {
    const Env env;
    QTemporaryDir tmp;
    writeFile(tmp.filePath(u"min.source.json"_s), QJsonDocument(baseObject()).toJson());
    const IZmpInfoPtr zmp = loadOne(env, tmp.path(), u"min.source.json"_s);
    ASSERT_TRUE(zmp != nullptr);
    EXPECT_EQ(zmp->gui()->sortIndex(), -1);
    EXPECT_FALSE(zmp->isLayer());
    EXPECT_EQ(zmp->layerZOrder(), 0);
    EXPECT_EQ(zmp->version(), QString());
    EXPECT_EQ(zmp->projectionSet()->zoom(0)->projectionType()->projectionEpsg(), 3857);
    EXPECT_TRUE(zmp->emptyTileSamples().empty());
    EXPECT_FALSE(zmp->tileDownloadRequestBuilderConfig()->isUseDownloader);
    EXPECT_TRUE(zmp->tileDownloaderConfig()->enabled);
}

TEST(SourceJsonMapping, EpsgCodesAndTheirProjection) {
    const Env env;
    QTemporaryDir tmp;
    const std::pair<int, int> table[] = {{3857, 3857}, {3395, 3395}, {4326, 4326}, {3785, 3857}};
    for (const auto& [code, want] : table) {
        writeFile(tmp.filePath(u"e.source.json"_s), withKey(u"epsg"_s, QString::number(code)));
        const IZmpInfoPtr zmp = loadOne(env, tmp.path(), u"e.source.json"_s);
        ASSERT_TRUE(zmp != nullptr) << code;
        EXPECT_EQ(zmp->projectionSet()->zoom(0)->projectionType()->projectionEpsg(), want) << code;
    }
}

TEST(SourceJsonMapping, DefaultKeyOfALocalisedObjectIsTheEmptyKeyOnly) {
    const auto c = mustParse(withKey(u"name"_s, uR"({"ru":"Рус","":"Дефолт"})"_s));
    ASSERT_TRUE(c != nullptr);
    EXPECT_EQ(paramsOf(*c)->readString(u"name", {}), u"Дефолт"_s);
    EXPECT_EQ(paramsOf(*c)->readString(u"name_ru", {}), u"Рус"_s);
}

// --- script (section 3: script, T-0224 scope)
// ----------------------------------------------------------------

TEST(SourceJsonScript, CodeSourceKeepsTheTextAndKeepsDownloadingOn) {
    const Env env;
    const IZmpInfoPtr zmp = loadOne(env, jsonDir(), u"ex64.source.json"_s);
    ASSERT_TRUE(zmp != nullptr);
    const QByteArray code = zmp->dataProvider()->binary(u"GetUrlScript.js"_s);
    EXPECT_TRUE(code.startsWith("function token() {\n  return shared.locked(function () {\n"));
    EXPECT_TRUE(code.endsWith("  return { url: template(ctx.urlBase), headers: h };\n}\n"));
    EXPECT_TRUE(code.contains("r.body.match(/\"access_token\":\"([^\"]+)\"/)"));
    EXPECT_TRUE(zmp->tileDownloaderConfig()->enabled); // T-0225: the JavaScript builder downloads it
    EXPECT_TRUE(zmp->abilities()->useDownload);
    EXPECT_TRUE(zmp->tileDownloadRequestBuilderConfig()->isUseDownloader); // allowHttp: true
    EXPECT_EQ(zmp->tileDownloadRequestBuilderConfig()->urlBase, "https://tiles.example.org/{z}/{x}/{y}.png");
    EXPECT_EQ(zmp->guid(), QUuid::fromString(u"{00000000-0000-4000-8000-000000000001}"_s));
}

TEST(SourceJsonScript, FileSourceReadsTheFileNextToTheJson) {
    const Env env;
    const IZmpInfoPtr zmp = loadOne(env, jsonDir(), u"ex64_file.source.json"_s);
    ASSERT_TRUE(zmp != nullptr);
    const QByteArray fromFile = readFile(jsonDir() + u"/example-token.js"_s);
    ASSERT_FALSE(fromFile.isEmpty());
    EXPECT_EQ(zmp->dataProvider()->binary(u"GetUrlScript.js"_s), fromFile);
    // the same text as the "code" form of the example
    const IZmpInfoPtr code = loadOne(env, jsonDir(), u"ex64.source.json"_s);
    ASSERT_TRUE(code != nullptr);
    EXPECT_EQ(code->dataProvider()->binary(u"GetUrlScript.js"_s), fromFile);
    EXPECT_TRUE(zmp->tileDownloaderConfig()->enabled);
}

TEST(SourceJsonScript, FileIsResolvedAgainstTheFolderOfTheJsonNotTheMapsRoot) {
    const Env env;
    QTemporaryDir tmp;
    QJsonObject o = baseObject();
    o.insert(u"script"_s, QJsonObject{{u"file"_s, u"nested/s.js"_s}});
    writeFile(tmp.filePath(u"sub/x.source.json"_s), QJsonDocument(o).toJson());
    writeFile(tmp.filePath(u"sub/nested/s.js"_s), "function getUrl(){return 'sub';}");
    writeFile(tmp.filePath(u"nested/s.js"_s), "function getUrl(){return 'root';}"); // the wrong one
    const IZmpInfoPtr zmp = loadOne(env, tmp.path(), u"sub/x.source.json"_s);
    ASSERT_TRUE(zmp != nullptr);
    EXPECT_EQ(zmp->dataProvider()->binary(u"GetUrlScript.js"_s),
              QByteArray("function getUrl(){return 'sub';}"));
}

TEST(SourceJsonScript, UrlIsNotRequiredWithAScript) {
    QJsonObject o = baseObject();
    o.remove(u"url"_s);
    o.insert(u"script"_s, QJsonObject{{u"code"_s, u"function getUrl(t,c){return null;}"_s}});
    EXPECT_TRUE(SourceJsonContainer::parse(QJsonDocument(o).toJson(), {}).has_value());
    o.remove(u"script"_s); // the same without a script: url is required
    EXPECT_FALSE(SourceJsonContainer::parse(QJsonDocument(o).toJson(), {}).has_value());
}

// --- the rules of section 2 and 3 (acceptance 2)
// --------------------------------------------------------------

struct BadCase {
    QString key;       // the key of baseObject() to set
    QString valueJson; // JSON text of the value; empty = remove the key
    QString named;     // the message must name this
};

TEST(SourceJsonRules, EachViolationIsAnErrorThatNamesTheKey) {
    const std::vector<BadCase> table = {
        // unknown key
        {u"colour"_s, u"1"_s, u"colour"_s},
        {u"Name"_s, u"\"x\""_s, u"Name"_s}, // keys are case-sensitive
        // format
        {u"format"_s, u""_s, u"format"_s},
        {u"format"_s, u"2"_s, u"format"_s},
        {u"format"_s, u"\"1\""_s, u"format"_s},
        {u"format"_s, u"1.5"_s, u"format"_s},
        // required keys missing
        {u"guid"_s, u""_s, u"guid"_s},
        {u"name"_s, u""_s, u"name"_s},
        {u"url"_s, u""_s, u"url"_s},
        {u"contentType"_s, u""_s, u"contentType"_s},
        {u"ext"_s, u""_s, u"ext"_s},
        {u"nameInCache"_s, u""_s, u"nameInCache"_s},
        // bad GUID
        {u"guid"_s, u"\"11111111-2222-3333-4444-555555555555\""_s, u"guid"_s}, // braces required
        {u"guid"_s, u"\"{1111-2222}\""_s, u"guid"_s},
        {u"guid"_s, u"\"{1111111G-2222-3333-4444-555555555555}\""_s, u"guid"_s},
        {u"guid"_s, u"\"\""_s, u"guid"_s},
        {u"guid"_s, u"\"x{11111111-2222-3333-4444-555555555555}\""_s, u"guid"_s},
        {u"guid"_s, u"\"{11111111-2222-3333-4444-555555555555}y\""_s, u"guid"_s},
        {u"guid"_s, u"5"_s, u"guid"_s},
        // wrong types
        {u"name"_s, u"5"_s, u"name"_s},
        {u"name"_s, u"{\"ru\":\"x\"}"_s, u"name"_s}, // the object form needs the empty key
        {u"name"_s, u"{\"\":\"d\",\"ru\":5}"_s, u"name"_s},
        {u"group"_s, u"[]"_s, u"group"_s},
        {u"sortIndex"_s, u"\"x\""_s, u"sortIndex"_s},
        {u"sortIndex"_s, u"1.5"_s, u"sortIndex"_s},
        {u"layer"_s, u"1"_s, u"layer"_s},
        {u"layerZOrder"_s, u"true"_s, u"layerZOrder"_s},
        {u"url"_s, u"5"_s, u"url"_s},
        {u"servers"_s, u"\"a\""_s, u"servers"_s},
        {u"servers"_s, u"[\"a\",1]"_s, u"servers"_s},
        {u"headers"_s, u"[]"_s, u"headers"_s},
        {u"headers"_s, u"{\"a\":1}"_s, u"headers"_s},
        {u"version"_s, u"1"_s, u"version"_s},
        {u"epsg"_s, u"\"3857\""_s, u"epsg"_s},
        {u"contentType"_s, u"1"_s, u"contentType"_s},
        {u"ext"_s, u"1"_s, u"ext"_s},
        {u"detectContentType"_s, u"\"yes\""_s, u"detectContentType"_s},
        {u"defaultContentType"_s, u"1"_s, u"defaultContentType"_s},
        {u"nameInCache"_s, u"1"_s, u"nameInCache"_s},
        {u"license"_s, u"1"_s, u"license"_s},
        {u"maxConnections"_s, u"\"2\""_s, u"maxConnections"_s},
        {u"sleepMs"_s, u"1.5"_s, u"sleepMs"_s},
        {u"allowHttp"_s, u"1"_s, u"allowHttp"_s},
        {u"termsOfUseWarning"_s, u"1"_s, u"termsOfUseWarning"_s},
        {u"maxZoom"_s, u"\"5\""_s, u"maxZoom"_s},
        {u"cacheType"_s, u"\"71\""_s, u"cacheType"_s},
        {u"crop"_s, u"1"_s, u"crop"_s},
        {u"snapshot"_s, u"1"_s, u"snapshot"_s},
        {u"emptyTiles"_s, u"\"AP8Q\""_s, u"emptyTiles"_s},
        {u"script"_s, u"1"_s, u"script"_s},
        // values out of the documented set
        {u"epsg"_s, u"1234"_s, u"epsg"_s},
        {u"maxZoom"_s, u"24"_s, u"maxZoom"_s},
        {u"maxZoom"_s, u"-1"_s, u"maxZoom"_s},
        {u"cacheType"_s, u"72"_s, u"cacheType"_s},
        {u"nameInCache"_s, u"\"a/b\""_s, u"nameInCache"_s},
        {u"nameInCache"_s, u"\"Имя\""_s, u"nameInCache"_s},
        {u"nameInCache"_s, u"\"a b\""_s, u"nameInCache"_s},
        {u"nameInCache"_s, u"\"\""_s, u"nameInCache"_s},
        {u"contentType"_s, u"\"\""_s, u"contentType"_s},
        {u"ext"_s, u"\"\""_s, u"ext"_s},
        // T-0346: the three keys of the .zmp that the loader did not know
        {u"ignoreContentType"_s, u"1"_s, u"ignoreContentType"_s},
        {u"ignoreContentType"_s, u"\"true\""_s, u"ignoreContentType"_s},
        {u"navigationWarning"_s, u"1"_s, u"navigationWarning"_s},
        {u"navigationWarning"_s, u"\"true\""_s, u"navigationWarning"_s},
        {u"contentTypeSubst"_s, u"\"a=b\""_s, u"contentTypeSubst"_s},
        {u"contentTypeSubst"_s, u"[]"_s, u"contentTypeSubst"_s},
        {u"contentTypeSubst"_s, u"{}"_s, u"contentTypeSubst"_s},
        {u"contentTypeSubst"_s, u"{\"\":\"image/png\"}"_s, u"contentTypeSubst"_s},
        {u"contentTypeSubst"_s, u"{\"image/png8bit\":5}"_s, u"contentTypeSubst"_s},
        // crop needs all four integers
        {u"crop"_s, u"{\"left\":1,\"top\":2,\"right\":3}"_s, u"crop"_s},
        {u"crop"_s, u"{\"left\":1,\"top\":2,\"right\":3,\"bottom\":\"4\"}"_s, u"crop"_s},
        {u"crop"_s, u"{\"left\":1,\"top\":2,\"right\":3,\"bottom\":4,\"extra\":5}"_s, u"crop"_s},
        // snapshot
        {u"snapshot"_s, u"{\"index\":\"http://i\"}"_s, u"type"_s},
        {u"snapshot"_s, u"{\"type\":\"t\"}"_s, u"index"_s},
        {u"snapshot"_s, u"{\"index\":\"u\",\"type\":\"t\",\"bogus\":\"x\"}"_s, u"bogus"_s},
        {u"snapshot"_s, u"{\"index\":5,\"type\":\"t\"}"_s, u"index"_s},
        // T-0303: the full dotted names (QString + QStringView rewrite)
        {u"snapshot"_s, u"{\"index\":\"u\"}"_s, u"snapshot.type"_s},
        {u"snapshot"_s, u"{\"index\":5,\"type\":\"t\"}"_s, u"snapshot.index"_s},
        {u"snapshot"_s, u"{\"index\":\"u\",\"type\":\"t\",\"versionField\":7}"_s, u"snapshot.versionField"_s},
        {u"snapshot"_s, u"{\"index\":\"u\",\"type\":\"t\",\"pick\":true}"_s, u"snapshot.pick"_s},
        // script: exactly one of file / code, a safe relative path, a readable file
        {u"script"_s, u"{}"_s, u"script"_s},
        {u"script"_s, u"{\"file\":\"a.js\",\"code\":\"x\"}"_s, u"script"_s},
        {u"script"_s, u"{\"code\":5}"_s, u"code"_s},
        {u"script"_s, u"{\"file\":5}"_s, u"file"_s},
        {u"script"_s, u"{\"file\":\"../outside.js\"}"_s, u"script.file"_s},
        {u"script"_s, u"{\"file\":\"sub/../a.js\"}"_s, u"script.file"_s},
        {u"script"_s, u"{\"file\":\"sub\\\\..\\\\a.js\"}"_s, u"script.file"_s},
        {u"script"_s, u"{\"file\":\"/etc/passwd\"}"_s, u"script.file"_s},
        {u"script"_s, u"{\"file\":\"nosuch.js\"}"_s, u"script.file"_s},
    };
    QTemporaryDir tmp;
    writeFile(tmp.filePath(u"folder/a.js"_s), "function getUrl(){}");
    writeFile(tmp.filePath(u"outside.js"_s), "function getUrl(){}"); // reachable by `..` from folder/
    for (const BadCase& c : table) {
        const std::string label = (c.key + u"="_s + c.valueJson).toStdString();
        const auto parsed =
            SourceJsonContainer::parse(withKey(c.key, c.valueJson), tmp.filePath(u"folder"_s));
        ASSERT_FALSE(parsed.has_value()) << label;
        EXPECT_TRUE(parsed.error().what().contains(c.named))
            << label << " -> " << parsed.error().what().toStdString();
    }
}

TEST(SourceJsonRules, EmptyTilesItemsMustBeBase64Strings) {
    for (const QString& value : {u"[1]"_s, u"[\"AP8Q\",true]"_s, u"[\"!!!\"]"_s}) {
        const auto parsed = SourceJsonContainer::parse(withKey(u"emptyTiles"_s, value), {});
        EXPECT_FALSE(parsed.has_value()) << value.toStdString();
    }
}

TEST(SourceJsonRules, ValidVariantsAreAccepted) {
    const std::pair<QString, QString> table[] = {
        {u"guid"_s, u"\"{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}\""_s}, // any case
        {u"guid"_s, u"\"{AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE}\""_s},
        {u"name"_s, u"{\"\":\"d\"}"_s},
        {u"group"_s, u"\"A\\\\B\""_s},
        {u"layerZOrder"_s, u"-1"_s},
        {u"epsg"_s, u"3785"_s},
        {u"epsg"_s, u"4326"_s},
        {u"maxZoom"_s, u"0"_s},
        {u"maxZoom"_s, u"23"_s},
        {u"cacheType"_s, u"71"_s},
        {u"nameInCache"_s, u"\"A.b_c-9\""_s},
        {u"servers"_s, u"[]"_s},
        {u"headers"_s, u"{}"_s},
        {u"emptyTiles"_s, u"[]"_s},
        {u"url"_s, u"\"http://h/a\\\"/b\""_s}, // a slash after an escaped quote is still inside the string
        {u"snapshot"_s, u"{\"index\":\"u\",\"type\":\"t\"}"_s},
    };
    for (const auto& [key, value] : table) {
        const auto parsed = SourceJsonContainer::parse(withKey(key, value), {});
        EXPECT_TRUE(parsed.has_value()) << (key + u"="_s + value).toStdString() << " "
                                        << (parsed ? std::string() : parsed.error().what().toStdString());
    }
}

// T-0346: ignoreContentType, contentTypeSubst and navigationWarning, read back the way the .zmp keys are.
TEST(SourceJsonMapping, IgnoreContentTypeAndNavigationWarningAreTheZmpBooleans) {
    const Env env;
    for (const bool value : {true, false}) {
        QTemporaryDir tmp;
        QJsonObject o = baseObject();
        o.insert(u"ignoreContentType"_s, value);
        o.insert(u"navigationWarning"_s, value);
        writeFile(tmp.filePath(u"f.source.json"_s), QJsonDocument(o).toJson());
        const IZmpInfoPtr zmp = loadOne(env, tmp.path(), u"f.source.json"_s);
        ASSERT_TRUE(zmp != nullptr);
        EXPECT_EQ(zmp->tileDownloaderConfig()->ignoreMimeType, value);
        const auto params = zmp->dataProvider()->subItem(u"params.txt")->subItem(u"PARAMS");
        EXPECT_EQ(params->readString(u"NavigationWarning", u"<absent>"_s), value ? u"1"_s : u"0"_s);
    }
    QTemporaryDir tmp; // absent: the ZMP defaults
    writeFile(tmp.filePath(u"f.source.json"_s), QJsonDocument(baseObject()).toJson());
    const IZmpInfoPtr zmp = loadOne(env, tmp.path(), u"f.source.json"_s);
    ASSERT_TRUE(zmp != nullptr);
    EXPECT_FALSE(zmp->tileDownloaderConfig()->ignoreMimeType);
    const auto params = zmp->dataProvider()->subItem(u"params.txt")->subItem(u"PARAMS");
    EXPECT_EQ(params->readString(u"NavigationWarning", u"<absent>"_s), u"<absent>"_s);
    EXPECT_EQ(params->readString(u"MimeTypeSubst", u"<absent>"_s), u"<absent>"_s);
}

TEST(SourceJsonMapping, ContentTypeSubstSurvivesTheTwoQuoteLevelsOfMimeTypeSubst) {
    const Env env;
    QTemporaryDir tmp;
    QJsonObject subst;
    subst.insert(u"image/png8bit"_s, u"image/png"_s);
    subst.insert(u"Text/XML"_s, u"text/xml; charset=UTF-8"_s); // ';', '=' and a space in the value
    subst.insert(u"a\"b=c;d"_s, u"v\"w"_s);                    // quotes, '=' and ';' in the key
    QJsonObject o = baseObject();
    o.insert(u"contentTypeSubst"_s, subst);
    writeFile(tmp.filePath(u"f.source.json"_s), QJsonDocument(o).toJson());
    const IZmpInfoPtr zmp = loadOne(env, tmp.path(), u"f.source.json"_s);
    ASSERT_TRUE(zmp != nullptr);
    const auto& s = zmp->contentTypeSubst();
    EXPECT_EQ(s.substitute("image/png8bit"), "image/png");
    EXPECT_EQ(s.substitute("TEXT/xml"), "text/xml; charset=UTF-8"); // keys are case-insensitive
    EXPECT_EQ(s.substitute("a\"b=c;d"), "v\"w");
    EXPECT_EQ(s.substitute("image/jpeg"), "image/jpeg"); // not listed: unchanged
}

TEST(SourceJsonMapping, FalseBooleansStayFalse) {
    const Env env;
    QTemporaryDir tmp;
    QJsonObject o = baseObject();
    o.insert(u"layer"_s, false);
    o.insert(u"detectContentType"_s, false);
    o.insert(u"allowHttp"_s, false);
    writeFile(tmp.filePath(u"f.source.json"_s), QJsonDocument(o).toJson());
    const IZmpInfoPtr zmp = loadOne(env, tmp.path(), u"f.source.json"_s);
    ASSERT_TRUE(zmp != nullptr);
    EXPECT_FALSE(zmp->isLayer());
    EXPECT_FALSE(zmp->tileDownloaderConfig()->detectMimeType);
    EXPECT_FALSE(zmp->tileDownloadRequestBuilderConfig()->isUseDownloader);
}

TEST(SourceJsonRules, BomIsAcceptedAndIgnored) {
    const QByteArray doc = QJsonDocument(baseObject()).toJson();
    EXPECT_TRUE(SourceJsonContainer::parse("\xEF\xBB\xBF" + doc, {}).has_value());
    EXPECT_TRUE(SourceJsonContainer::parse(doc, {}).has_value());
    const Env env;
    QTemporaryDir tmp;
    writeFile(tmp.filePath(u"bom.source.json"_s),
              "\xEF\xBB\xBF" + readFile(jsonDir() + u"/ex63.source.json"_s));
    const IZmpInfoPtr zmp = loadOne(env, tmp.path(), u"bom.source.json"_s);
    ASSERT_TRUE(zmp != nullptr);
    EXPECT_EQ(zmp->gui()->name()->getString(kEn), u"OpenTopoMap"_s);
}

TEST(SourceJsonRules, CommentsAndNonObjectsAreErrors) {
    const QByteArray ok = QJsonDocument(baseObject()).toJson();
    const QByteArray cases[] = {
        "// lead\n" + ok,
        "/* lead */" + ok,
        ok + "// trail\n",
        QByteArray(ok).replace("\"format\"", "/* c */ \"format\""),
        QByteArray(),
        "[]",
        "\"text\"",
        "{",
        QByteArray(ok).replace("}", ",}"),
    };
    for (const QByteArray& bytes : cases) {
        EXPECT_FALSE(SourceJsonContainer::parse(bytes, {}).has_value()) << bytes.toStdString();
    }
}

TEST(SourceJsonRules, ADirectoryOrMissingFileFailsToLoad) {
    QTemporaryDir tmp;
    EXPECT_FALSE(SourceJsonContainer::load(tmp.filePath(u"missing.source.json"_s)).has_value());
    EXPECT_FALSE(SourceJsonContainer::load(tmp.path()).has_value());
}

// --- through ZmpInfoSet: report, skip only that file, same-GUID rule (acceptance 2, 3)
// -------------------------

TEST(SourceJsonSet, BadFilesAreReportedLikeABrokenZmpAndOnlyTheyAreSkipped) {
    const Env env;
    QTemporaryDir tmp;
    writeFile(tmp.filePath(u"good.source.json"_s), readFile(jsonDir() + u"/ex63.source.json"_s));
    writeFile(tmp.filePath(u"bad1.source.json"_s), withKey(u"colour"_s, u"1"_s));
    writeFile(tmp.filePath(u"sub/bad2.source.json"_s), withKey(u"guid"_s, u"\"nope\""_s));
    writeFile(tmp.filePath(u"bad3.source.json"_s), withKey(u"script"_s, u"{\"file\":\"../x.js\"}"_s));
    writeFile(tmp.filePath(u"bad4.source.json"_s), "{ not json");
    const QString names[] = {u"bad1.source.json"_s, u"sub/bad2.source.json"_s, u"bad3.source.json"_s,
                             u"bad4.source.json"_s};
    const QString keys[] = {u"colour"_s, u"guid"_s, u"script.file"_s, QString()};
    QStringList errors;
    const auto set = env.loadAll(tmp.path(), errors);
    ASSERT_EQ(errors.size(), 4) << errors.join(u"\n"_s).toStdString();
    for (int i = 0; i < 4; ++i) {
        const QString suffix = u" in map "_s + names[i];
        const auto hit = std::ranges::find_if(errors, [&](const QString& e) { return e.endsWith(suffix); });
        ASSERT_NE(hit, errors.end()) << names[i].toStdString();
        EXPECT_TRUE(hit->startsWith(u"There is an error "_s)) << hit->toStdString();
        EXPECT_TRUE(hit->contains(keys[i])) << hit->toStdString();
    }
    ASSERT_EQ(set->guids().size(), 1u);
    EXPECT_EQ(set->zmpByGuid(set->guids()[0])->fileName(), u"good.source.json"_s);
}

TEST(SourceJsonSet, DuplicateGuidWithAZmpOrAnotherJsonIsTheSameGuidReport) {
    struct Row {
        QString first, second; // the natural sort puts `first` before `second`
        bool firstIsZmp, secondIsZmp;
    };
    const Row table[] = {
        {u"a.zmp"_s, u"b.source.json"_s, true, false},
        {u"a.source.json"_s, u"b.zmp"_s, false, true},
        {u"a.source.json"_s, u"b.source.json"_s, false, false},
    };
    for (const Row& row : table) {
        const Env env;
        QTemporaryDir tmp;
        const auto put = [&](const QString& name, bool zmp) {
            if (zmp) {
                writeFile(tmp.filePath(name + u"/params.txt"_s),
                          readFile(jsonDir() + u"/zmp/ex63.zmp/params.txt"_s));
            } else {
                writeFile(tmp.filePath(name), readFile(jsonDir() + u"/ex63.source.json"_s));
            }
        };
        put(row.first, row.firstIsZmp);
        put(row.second, row.secondIsZmp);
        writeFile(tmp.filePath(u"c.source.json"_s),
                  readFile(jsonDir() + u"/ex62.source.json"_s)); // unrelated, after
        QStringList errors;
        const auto set = env.load(tmp.path(), {row.second, u"c.source.json"_s, row.first}, errors);
        // LD-11: the first is relative, the second is the full path
        EXPECT_EQ(errors, QStringList{u"Files %1 and %2 have the same GUID"_s.arg(row.first,
                                                                                  tmp.filePath(row.second))})
            << row.first.toStdString() << " / " << row.second.toStdString();
        ASSERT_EQ(set->guids().size(), 2u);
        EXPECT_EQ(set->zmpByGuid(QUuid::fromString(u"{51C1079B-B55D-4DF7-A410-ADC118F53AB1}"_s))->fileName(),
                  row.first);
    }
}

} // namespace
