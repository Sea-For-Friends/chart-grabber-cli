// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MapType.pas:1-506, :555-683, :790-835 (no FEAT id: prerequisite of R-12 MapTypesMainList;
// docs/specs/maptype.md 2.11.6, ticket T-0140). Real objects: the 514 stock ZMPs
// (tests/fixtures/zmp/sas.maps) with the stock Maps.ini (written by legacy), a real
// TileStorageTypeListSimple, TileStorageOfMapType and TileDownloadSubsystem. Scripted: one storage type that
// wraps the real folder storage and answers the abilities, tileFileName and listVersions the test names
// (getTileShowName / nextVersion), and a fake transport (the destruction and wiring tests). Expected values
// are literals taken from the Pascal text.
#include "cg/core/config_provider_ini.h"
#include "cg/core/content_type.h"
#include "cg/core/notifier.h"
#include "cg/core/path_config.h"
#include "cg/geo/local_coord_converter.h"
#include "cg/maptype/layer_draw_config.h"
#include "cg/maptype/map_abilities_config.h"
#include "cg/maptype/map_type.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/map_type_gui_config_list.h"
#include "cg/maptype/map_version_factory_changeable.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/map_version_request_config.h"
#include "cg/maptype/tile_download_request_builder_config.h"
#include "cg/maptype/tile_download_subsystem.h"
#include "cg/maptype/tile_request_task.h"
#include "cg/maptype/zmp_info.h"
#include "cg/maptype/zmp_info_set.h"
#include "cg/net/download_result_factory.h"
#include "cg/net/global_download_config.h"
#include "cg/net/inet_config.h"
#include "cg/net/tile_downloader_config.h"
#include "cg/storage/global_cache_config.h"
#include "cg/storage/simple_tile_storage_config.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_storage_of_map_type.h"
#include "cg/storage/tile_storage_type_list_simple.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <latch>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "maptype_test_util.h"
#include "request_builder_test_util.h"

using namespace Qt::StringLiterals;
using namespace cg;
using namespace cg::maptype;

namespace {

// docs/specs/net.md 7.0: no test may reach the real network (read once, on the first CurlDownloader call).
struct ForceNoExternalNetwork {
    ForceNoExternalNetwork() { qputenv("CG_NET_NO_EXTERNAL_NETWORK", "1"); }
};
const ForceNoExternalNetwork kForceNoExternalNetwork;

constexpr auto kWait = std::chrono::seconds(10);

using Params = std::vector<std::pair<QByteArray, QByteArray>>;

// --- fake transport
// -----------------------------------------------------------------------------------------

class FnDownloader final : public net::IDownloader {
public:
    net::DownloadResult doRequest(const net::DownloadRequest& request, std::stop_token stop) override {
        return fn(request, std::move(stop));
    }
    std::function<net::DownloadResult(const net::DownloadRequest&, std::stop_token)> fn;
};

class SharedDownloaderFactory final : public net::IDownloaderFactory {
public:
    explicit SharedDownloaderFactory(std::shared_ptr<net::IDownloader> d) : downloader_(std::move(d)) {}
    net::IDownloaderPtr build(const net::DownloaderOptions& /*options*/) override { return downloader_; }

private:
    std::shared_ptr<net::IDownloader> downloader_;
};

// Waits for a stop request on `stop` for at most kWait. True when it came.
bool waitForStop(const std::stop_token& stop) {
    std::mutex m;
    std::condition_variable cv;
    bool fired = false;
    const std::stop_callback callback(stop, [&] {
        {
            const std::lock_guard lock(m);
            fired = true;
        }
        cv.notify_all();
    });
    std::unique_lock lock(m);
    return cv.wait_for(lock, kWait, [&] { return fired; });
}

// --- scripted storage
// ---------------------------------------------------------------------------------------

// What the scripted storage answers and what it was asked.
struct Script {
    storage::TileStorageTypeAbilities abilities;
    QString fileName;
    core::Result<storage::MapVersionList> versions{storage::MapVersionList{}};

    struct ListCall {
        geo::PointI xy;
        quint8 zoom = 0;
        storage::MapVersionRequest request;
    };
    struct NameCall {
        geo::PointI xy;
        quint8 zoom = 0;
        storage::MapVersionInfoPtr version;
    };
    std::mutex mutex;
    std::vector<ListCall> listCalls;
    std::vector<NameCall> nameCalls;
};

// Forwards everything to the real storage `inner`, except the three things the script answers.
class ScriptedStorage final : public storage::ITileStorage {
public:
    ScriptedStorage(storage::ITileStoragePtr inner, std::shared_ptr<Script> script)
        : inner_(std::move(inner)), script_(std::move(script)) {}

    storage::TileStorageTypeAbilities typeAbilities() const override { return script_->abilities; }
    storage::StorageStatePtr state() const override { return inner_->state(); }
    core::Changeable& stateChangeable() override { return inner_->stateChangeable(); }
    const geo::IProjectionSet& projectionSet() const override { return inner_->projectionSet(); }
    storage::TilePyramidNotifier& tileNotifier() override { return inner_->tileNotifier(); }
    QString tileFileName(geo::PointI xy, quint8 zoom,
                         const storage::MapVersionInfoPtr& version) const override {
        const std::lock_guard lock(script_->mutex);
        script_->nameCalls.push_back({xy, zoom, version});
        return script_->fileName;
    }
    core::Result<storage::TileInfo> tileInfo(geo::PointI xy, quint8 zoom, const storage::MapVersionInfoPtr& v,
                                             storage::LoadMode mode) const override {
        return inner_->tileInfo(xy, zoom, v, mode);
    }
    core::Result<storage::TileInfo> tileInfoEx(geo::PointI xy, quint8 zoom,
                                               const storage::MapVersionRequest& r,
                                               storage::LoadMode mode) const override {
        return inner_->tileInfoEx(xy, zoom, r, mode);
    }
    core::Result<bool> deleteTile(geo::PointI xy, quint8 zoom, const storage::MapVersionInfoPtr& v) override {
        return inner_->deleteTile(xy, zoom, v);
    }
    core::Result<bool> saveTile(geo::PointI xy, quint8 zoom, const storage::MapVersionInfoPtr& v,
                                storage::TileDate d, QByteArrayView type, const QByteArray& data,
                                bool overwrite) override {
        return inner_->saveTile(xy, zoom, v, d, type, data, overwrite);
    }
    core::Result<storage::MapVersionList>
    listVersions(geo::PointI xy, quint8 zoom, const storage::MapVersionRequest& request) const override {
        const std::lock_guard lock(script_->mutex);
        script_->listCalls.push_back({xy, zoom, request});
        return script_->versions;
    }
    core::Result<storage::TileRectInfoPtr> tileRectInfo(geo::RectI rect, quint8 zoom,
                                                        const storage::MapVersionRequest& r,
                                                        std::stop_token stop) const override {
        return inner_->tileRectInfo(rect, zoom, r, std::move(stop));
    }
    core::Result<storage::EnumTileInfoPtr> scanTiles(bool a, bool b) override {
        return inner_->scanTiles(a, b);
    }

private:
    storage::ITileStoragePtr inner_;
    std::shared_ptr<Script> script_;
};

class ScriptedType final : public storage::ITileStorageType {
public:
    ScriptedType(storage::ITileStorageTypePtr inner, std::shared_ptr<Script> script)
        : inner_(std::move(inner)), script_(std::move(script)) {}
    storage::TileStorageTypeAbilities abilities() const override { return inner_->abilities(); }
    core::PathConfigPtr basePath() const override { return inner_->basePath(); }
    storage::MapVersionFactoryPtr mapVersionFactory() const override { return inner_->mapVersionFactory(); }
    core::Result<storage::ITileStoragePtr>
    build(const storage::TileStorageAbilities& force, geo::ProjectionSetPtr projectionSet,
          const core::ContentTypeInfo& contentType, storage::TilePyramidNotifierPtr notifier,
          const QString& path, std::shared_ptr<storage::TileInfoMemCache> cache) override {
        auto built = inner_->build(force, std::move(projectionSet), contentType, std::move(notifier), path,
                                   std::move(cache));
        if (!built) {
            return built;
        }
        return storage::ITileStoragePtr{std::make_shared<ScriptedStorage>(*built, script_)};
    }

private:
    storage::ITileStorageTypePtr inner_;
    std::shared_ptr<Script> script_;
};

// --- recording providers
// ------------------------------------------------------------------------------------

// Records the keys a map reads, in order; answers "absent" to everything.
class RecordingProvider final : public core::IConfigProvider {
public:
    core::IConfigProviderPtr subItem(QStringView) const override { return nullptr; }
    QVariant value(QStringView id) const override {
        keys.push_back(id.toString());
        return {};
    }
    QByteArray binary(QStringView) const override { return {}; }
    QStringList subItemNames() const override { return {}; }
    QStringList valueNames() const override { return {}; }
    mutable QStringList keys;
};

// Records every write and delete as "w:key" / "d:key", in order, over the whole tree it was handed.
class RecordingWriter final : public core::IConfigWriter {
public:
    explicit RecordingWriter(std::shared_ptr<QStringList> log) : log_(std::move(log)) {}
    core::IConfigProviderPtr subItem(QStringView) const override { return nullptr; }
    QVariant value(QStringView) const override { return {}; }
    QByteArray binary(QStringView) const override { return {}; }
    QStringList subItemNames() const override { return {}; }
    QStringList valueNames() const override { return {}; }
    core::IConfigWriterPtr orCreateSubItem(QStringView id) override {
        log_->push_back(u"s:"_s + id.toString());
        return std::make_shared<RecordingWriter>(log_);
    }
    void deleteSubItem(QStringView id) override { log_->push_back(u"ds:"_s + id.toString()); }
    void deleteValue(QStringView id) override { log_->push_back(u"d:"_s + id.toString()); }
    void deleteValues() override { log_->push_back(u"dv:"_s); }
    void writeValue(QStringView id, const QVariant&) override { log_->push_back(u"w:"_s + id.toString()); }
    void writeBinary(QStringView id, const QByteArray&) override { log_->push_back(u"w:"_s + id.toString()); }

private:
    std::shared_ptr<QStringList> log_;
};

// Index of the first write or delete of `key`; -1 when there is none.
qsizetype firstOp(const QStringList& log, const QString& key) {
    for (qsizetype i = 0; i < log.size(); ++i) {
        if (log[i] == u"w:"_s + key || log[i] == u"d:"_s + key) {
            return i;
        }
    }
    return -1;
}

// --- the rig
// ------------------------------------------------------------------------------------------------

const QByteArray kPng = "\x89PNG-tile-\xd0\x93\xd0\xbe";

QString sectionOf(const QUuid& guid) {
    return guid.toString(QUuid::WithBraces).toUpper();
}

std::shared_ptr<const core::ContentTypeRegistry> registry() {
    return {std::shared_ptr<void>(), &core::ContentTypeRegistry::standard()};
}

class Rig {
public:
    maptype::test::ZmpEnv env;
    QTemporaryDir root;
    core::Notifier gc; // declared before the list: outlives every type
    std::stop_source appClosing;
    std::shared_ptr<core::ILanguageManager> language = std::make_shared<maptype::test::FakeLanguageManager>();
    std::shared_ptr<net::InetConfig> inet = std::make_shared<net::InetConfig>();
    std::shared_ptr<net::GlobalDownloadConfig> download = std::make_shared<net::GlobalDownloadConfig>();
    std::shared_ptr<script::IProjConverterFactory> proj =
        std::make_shared<maptype::test::CountingProjFactory>();
    storage::MapVersionFactoryPtr versions = std::make_shared<storage::MapVersionFactory>();
    std::shared_ptr<storage::GlobalCacheConfig> cacheConfig;
    std::shared_ptr<storage::TileStorageTypeListSimple> realList;
    std::shared_ptr<net::IDownloaderFactory> transport;
    std::vector<std::unique_ptr<ZmpInfoSet>> sets;

    Rig() {
        cacheConfig = std::make_shared<storage::GlobalCacheConfig>(
            core::PathConfig::create(QString(), root.filePath(u"cache"_s), nullptr));
        realList = std::make_shared<storage::TileStorageTypeListSimple>(versions, *cacheConfig, gc);
        auto idle = std::make_shared<FnDownloader>();
        idle->fn = [](const net::DownloadRequest&, std::stop_token) { return net::result::canceled(); };
        transport = std::make_shared<SharedDownloaderFactory>(idle);
        inet->proxy().setUseSystemSettings(false);
    }

    // A throw-away template-only ZMP; `over` replaces the default value of a key or appends the key.
    IZmpInfoPtr zmp(Params over = {}, const QByteArray& extraSections = {}) {
        const int n = ++zmpCount_;
        Params params = {
            {"GUID", QString(u"{D643C5A1-3333-4387-9BE2-DDDF1850%1}"_s.arg(n, 4, 16, QLatin1Char('0')))
                         .toUpper()
                         .toLatin1()},
            {"name", "Map " + QByteArray::number(n)},
            {"NameInCache", "cache" + QByteArray::number(n)},
            {"DefURLBase", "http://127.0.0.1:9/{z}/{x}/{y}.png"},
            {"ContentType", "image/png"},
            {"Ext", ".png"},
            {"projection", "1"},
            {"sradiusa", "6378137"},
            {"sradiusb", "6356752"},
        };
        for (auto& [key, value] : over) {
            bool replaced = false;
            for (auto& existing : params) {
                if (existing.first == key) {
                    existing.second = value;
                    replaced = true;
                }
            }
            if (!replaced) {
                params.emplace_back(key, value);
            }
        }
        const QString name = u"m"_s + QString::number(n) + u".zmp"_s;
        QByteArray text = "[PARAMS]\r\n";
        for (const auto& [key, value] : params) {
            text += key + "=" + value + "\r\n";
        }
        text += extraSections;
        const QString dir = root.filePath(u"zmps/"_s + name);
        EXPECT_TRUE(QDir().mkpath(dir));
        QFile f(dir + u"/params.txt"_s);
        EXPECT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(text);
        f.close();
        return load(root.filePath(u"zmps"_s), name);
    }

    IZmpInfoPtr stockZmp(const QString& relative) {
        return load(maptype::test::fixtureDir() + u"/zmp/sas.maps"_s, relative);
    }

    std::unique_ptr<MapType> make(const IZmpInfoPtr& zmp, const core::IConfigProviderPtr& local = nullptr,
                                  storage::ITileStorageTypeListPtr list = nullptr) {
        if (!list) {
            list = realList;
        }
        return std::make_unique<MapType>(
            language, zmp, versions, cacheConfig, std::move(list), gc, appClosing.get_token(), inet, download,
            transport, registry(), test::testContentConverters(), proj, local, 7, root.filePath(u"logs"_s));
    }

    // One list item (code 2, the stock SAS folder cache) whose storage answers `script`.
    storage::ITileStorageTypeListPtr scriptedList(const std::shared_ptr<Script>& script) const {
        auto type = std::make_shared<ScriptedType>(realList->itemByCode(2)->storageType(), script);
        std::vector<storage::ITileStorageTypeListItemPtr> items{
            std::make_shared<storage::TileStorageTypeListItem>(QUuid(), 2, u"scripted"_s, std::move(type),
                                                               true, true)};
        return std::make_shared<storage::TileStorageTypeList>(std::move(items));
    }

    [[nodiscard]] QStringList cacheFiles() const {
        QStringList out;
        QDirIterator it(root.filePath(u"cache"_s), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            out.append(it.next());
        }
        out.sort();
        return out;
    }

private:
    IZmpInfoPtr load(const QString& rootDir, const QString& name) {
        QStringList errors;
        sets.push_back(env.load(rootDir, {name}, errors));
        EXPECT_TRUE(errors.isEmpty()) << errors.join(u'\n').toStdString();
        const auto guids = sets.back()->guids();
        return guids.empty() ? nullptr : sets.back()->zmpByGuid(guids.front());
    }

    int zmpCount_ = 0;
};

// Params of a map whose cache type is the scripted item (code 2).
Params scripted() {
    return {{"CacheType", "2"}};
}

// --- the corpus
// ---------------------------------------------------------------------------------------------

// Proofs 1, 2 and the stock half of 3: a MapType for each of the 514 stock ZMPs, built from its own Maps.ini
// section, against a real storage type list. Literal counts come from the fixtures: `Ext=.kml` + the
// wikimapia MainStorageContentType in 2 params.txt (Wikimapia/Wiki.zmp,
// _broken_maps/Panoramio/Panoramio_KML.zmp), every other map a bitmap type (512); `CacheType=5` in 4
// (GoogleEarth), `=8` in 1 (GeoCacher), `=9` in 14 (RAM).
TEST(MapTypeCorpus, All514StockZmpsConstructAndKeepTheirKindStorageAndStockSection) {
    CG_SKIP_IF_NO_CORPUS();
    Rig rig;
    const QString zmpRoot = maptype::test::fixtureDir() + u"/zmp/sas.maps"_s;
    QStringList errors;
    const auto set = rig.env.loadAll(zmpRoot, errors);
    ASSERT_TRUE(errors.isEmpty());
    ASSERT_EQ(set->guids().size(), 514u);
    auto opened = core::ConfigProviderIni::open(maptype::test::fixtureDir() + u"/zmp/Maps.ini"_s);
    ASSERT_TRUE(opened.has_value());
    const auto mapsIni = opened.value();

    int rejected = 0;
    int bitmap = 0;
    int kml = 0;
    int bothKinds = 0;
    int googleEarth = 0;
    int geoCacher = 0;
    int ram = 0;
    int defaultCache = 0;
    for (const QUuid& guid : set->guids()) {
        const IZmpInfoPtr zmp = set->zmpByGuid(guid);
        SCOPED_TRACE(zmp->fileName().toStdString());
        const QString section = sectionOf(guid);
        std::unique_ptr<MapType> map;
        try {
            map = rig.make(zmp, mapsIni->subItem(section));
        } catch (const std::exception& e) {
            ++rejected;
            ADD_FAILURE() << e.what();
            continue;
        }
        EXPECT_EQ(map->guid(), guid);
        bitmap += map->isBitmapTiles() ? 1 : 0;
        kml += map->isKmlTiles() ? 1 : 0;
        bothKinds += (map->isBitmapTiles() && map->isKmlTiles()) ? 1 : 0;

        const auto abilities = map->tileStorage()->typeAbilities();
        switch (zmp->storageConfig()->cacheType) {
        case storage::CacheTypeId::Ge:
        case storage::CacheTypeId::GeoCacher: {
            // no list item in the port: no backend, the wrapper's "nothing" answers
            (zmp->storageConfig()->cacheType == storage::CacheTypeId::Ge ? googleEarth : geoCacher)++;
            EXPECT_EQ(abilities.storageClass, storage::StorageClass::Other);
            EXPECT_EQ(abilities.version, storage::VersionSupport::Ignored);
            const auto state = map->tileStorage()->state();
            ASSERT_NE(state, nullptr);
            EXPECT_EQ(*state, storage::StorageState{});
            break;
        }
        case storage::CacheTypeId::Ram:
            ++ram;
            EXPECT_EQ(abilities.storageClass, storage::StorageClass::InMemory);
            break;
        case storage::CacheTypeId::Default:
            // the global default cache type is SQLite (71)
            ++defaultCache;
            EXPECT_EQ(abilities.storageClass, storage::StorageClass::Folder);
            EXPECT_EQ(abilities.version, storage::VersionSupport::MultiVersions);
            break;
        default:
            ADD_FAILURE() << "unexpected cache type in the corpus";
        }

        // saveConfig of a map read from its stock section reproduces the section (keys and their order).
        auto out = maptype::test::iniFrom({});
        map->saveConfig(out->orCreateSubItem(section));
        const auto written = out->subItem(section);
        const auto stock = mapsIni->subItem(section);
        ASSERT_NE(written, nullptr);
        ASSERT_NE(stock, nullptr);
        EXPECT_EQ(written->valueNames(), stock->valueNames());
        for (const QString& key : stock->valueNames()) {
            EXPECT_EQ(written->readString(key), stock->readString(key)) << key.toStdString();
        }
    }
    EXPECT_EQ(rejected, 0);
    EXPECT_EQ(bitmap, 512);
    EXPECT_EQ(kml, 2);
    EXPECT_EQ(bothKinds, 0);
    EXPECT_EQ(googleEarth, 4);
    EXPECT_EQ(geoCacher, 1);
    EXPECT_EQ(ram, 14);
    EXPECT_EQ(defaultCache, 495);
}

// --- the content type: two-step rule and the predicates (:376-381, :669-677)
// -----------------------------------

struct KindRow {
    const char* main; // MainStorageContentType ("" = absent)
    const char* ext;
    const char* expectedType;
    const char* expectedDefaultExt;
    bool kml;
};

TEST(MapTypeContentType, TheMainTypeWinsThenTheExtensionAndTheKindFollowsTheEntry) {
    // u_MapType.pas:376-381: GetInfo(MainContentType) when it is not '', else (or when nil)
    // GetInfoByExt(Ext).
    const std::vector<KindRow> rows = {
        {"", ".gif", "image/gif", ".gif", false},
        {"image/png", ".gif", "image/png", ".png", false},
        {"x/unknown-type", ".png", "image/png", ".png", false},
        {"application/vnd.sas.wikimapia.kml+xml", ".kml", "application/vnd.sas.wikimapia.kml+xml", ".kml",
         true},
        {"application/vnd.sas.wikimapia.kml+xml", ".png", "application/vnd.sas.wikimapia.kml+xml", ".kml",
         true},
        {"image/png", ".kml", "image/png", ".png", false},
        {"", ".kml", "application/vnd.google-earth.kml+xml", ".kml", true},
    };
    Rig rig;
    for (const KindRow& row : rows) {
        SCOPED_TRACE(std::string(row.main) + " | " + row.ext);
        Params over = {{"Ext", row.ext}, {"CacheType", "9"}};
        if (*row.main != '\0') {
            over.emplace_back("MainStorageContentType", row.main);
        }
        const auto map = rig.make(rig.zmp(over));
        ASSERT_NE(map->contentType(), nullptr);
        EXPECT_EQ(map->contentType()->type, QByteArray(row.expectedType));
        EXPECT_EQ(map->contentType()->defaultExt, QByteArray(row.expectedDefaultExt));
        EXPECT_EQ(map->isKmlTiles(), row.kml);
        EXPECT_EQ(map->isBitmapTiles(), !row.kml);
    }
}

// --- the getters hand out what the ZMP and the constructor made
// ----------------------------------------------

TEST(MapTypeGetters, TheZmpAndItsProjectionSetsAreTheMapsOwnAndEveryConfigIsThere) {
    Rig rig;
    // [ViewInfo] gives the ZMP a view projection set of its own (u_ZmpInfo.pas:841-856): Geographic (3)
    const IZmpInfoPtr zmp = rig.zmp({{"CacheType", "9"}}, "[ViewInfo]\r\nprojection=3\r\n");
    ASSERT_NE(zmp->viewProjectionSet(), zmp->projectionSet());
    const auto map = rig.make(zmp);
    EXPECT_EQ(map->zmp(), zmp);
    EXPECT_EQ(map->guid(), zmp->guid());
    EXPECT_EQ(map->projectionSet(), zmp->projectionSet());
    EXPECT_EQ(map->viewProjectionSet(), zmp->viewProjectionSet());
    EXPECT_NE(map->versionFactory(), nullptr);
    EXPECT_NE(map->versionRequestConfig(), nullptr);
    EXPECT_NE(map->versionRequest(), nullptr);
    EXPECT_NE(map->abilities(), nullptr);
    EXPECT_NE(map->storageConfig(), nullptr);
    EXPECT_NE(map->tileDownloadSubsystem(), nullptr);
    EXPECT_NE(map->tileStorage(), nullptr);
    EXPECT_NE(map->guiConfig(), nullptr);
    EXPECT_NE(map->layerDrawConfig(), nullptr);
    EXPECT_NE(map->tileDownloaderConfig(), nullptr);
    EXPECT_NE(map->tileDownloadRequestBuilderConfig(), nullptr);
    EXPECT_EQ(map->cacheBitmap(), nullptr); // bitmap half: T-0148
    EXPECT_EQ(map->cacheVector(), nullptr);
}

// --- saveConfig: the stock shape and the write order (:496-505)
// ----------------------------------------------

constexpr auto kChandracomp = "_Space/sky_chandracomp.zmp";

// An untouched map written into an empty INI is the first section of the stock extract (the first stock map);
// the ZMP's own pnum is "not set" (L-151), so the main list's renumbering is what the test does by hand.
TEST(MapTypeSaveConfig, AnUntouchedStockMapWritesTheStockSectionOfTheLegacyFile) {
    CG_SKIP_IF_NO_CORPUS();
    Rig rig;
    const auto map = rig.make(rig.stockZmp(QString::fromLatin1(kChandracomp)));
    map->guiConfig()->setSortIndex(1);
    auto ini = maptype::test::iniFrom({});
    map->saveConfig(ini->orCreateSubItem(u"{D0439E79-1D2D-4364-A61E-41757BD819AF}"_s));

    QFile f(maptype::test::fixtureDir() + u"/maptype/Maps_stock_extract.ini"_s);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    const QByteArray section = data.left(data.indexOf("\r\n\r\n") + 2).mid(3); // without the BOM
    EXPECT_EQ(ini->file().toBytes(), section);
}

TEST(MapTypeSaveConfig, TheSevenConfigsAreWrittenInTheOrderOfTheWriteListNotTheReadList) {
    Rig rig;
    const auto map = rig.make(rig.zmp({{"CacheType", "9"}}));
    auto log = std::make_shared<QStringList>();
    map->saveConfig(std::make_shared<RecordingWriter>(log));
    // One key of each config; whether it is written or deleted depends on the config, its position does not.
    // :498-504: GUI (pnum), layer draw (LayerZOrder), request builder (URLBase), downloader (Sleep),
    // version request (ShowOtherVersions), storage (CacheType), abilities (UseDwn).
    const QStringList keys = {u"pnum"_s,  u"LayerZOrder"_s,       u"URLBase"_s,
                              u"Sleep"_s, u"ShowOtherVersions"_s, u"CacheType"_s,
                              u"UseDwn"_s};
    qsizetype previous = -1;
    for (const QString& key : keys) {
        const qsizetype at = firstOp(*log, key);
        ASSERT_GE(at, 0) << key.toStdString();
        EXPECT_GT(at, previous) << key.toStdString();
        previous = at;
    }
}

// --- the constructor reads the seven configs in the read order (:368-374) ----------------------------------

TEST(MapTypeConstructor, TheSevenConfigsAreReadInTheOrderOfTheReadList) {
    Rig rig;
    const IZmpInfoPtr zmp = rig.zmp({{"CacheType", "9"}});
    const auto provider = std::make_shared<RecordingProvider>();
    const auto map = rig.make(zmp, provider);
    // GUI (pnum), layer draw (LayerZOrder), storage (CacheType), abilities (CanShowOnSmMap),
    // version request (Version), downloader (AllowUseCookie), request builder (URLBase).
    const QStringList keys = {u"pnum"_s,    u"LayerZOrder"_s,    u"CacheType"_s, u"CanShowOnSmMap"_s,
                              u"Version"_s, u"AllowUseCookie"_s, u"URLBase"_s};
    qsizetype previous = -1;
    for (const QString& key : keys) {
        const qsizetype at = provider->keys.indexOf(key);
        ASSERT_GE(at, 0) << key.toStdString();
        EXPECT_GT(at, previous) << key.toStdString();
        previous = at;
    }
}

TEST(MapTypeConstructor, AStoredVersionDifferentFromTheZmpsIsAppliedAndNothingCrashesWithoutCaches) {
    // The listener of :362-363 is registered before the read of :373; the read changes the version while no
    // cache exists (UseMemCache=0 is the all-null case; with the cache the listener fires before :386 built
    // it too). That firing has no observable effect; what is observed is the stored value and the absence of
    // a crash.
    Rig rig;
    for (const char* useMemCache : {"0", "1"}) {
        SCOPED_TRACE(useMemCache);
        const IZmpInfoPtr zmp =
            rig.zmp({{"CacheType", "9"}, {"Version", "FromZmp"}, {"UseMemCache", useMemCache}});
        const auto ini = maptype::test::iniFrom("[S]\r\nVersion=Stored\xD0\x92\r\n");
        const auto map = rig.make(zmp, ini->subItem(u"S"));
        EXPECT_EQ(map->versionRequestConfig()->version(), u"StoredВ"_s);
        EXPECT_EQ(map->cacheTileInfo() != nullptr, std::string(useMemCache) == "1");
    }
}

// --- the tile-info cache of the map (:386-397, :285-296, :832-835)
// ------------------------------------------

storage::TileInfo someInfo() {
    return storage::TileInfo{};
}

bool hit(storage::TileInfoMemCache& cache, geo::PointI xy, quint8 zoom) {
    return cache.get(xy, zoom, nullptr, storage::LoadMode::AsIs, false).has_value();
}

TEST(MapTypeMemCache, TheCacheExistsOnlyWithUseMemCacheAndTakesTheCapacityOfTheZmp) {
    Rig rig;
    {
        const auto map = rig.make(rig.zmp({{"CacheType", "9"}, {"UseMemCache", "0"}}));
        EXPECT_EQ(map->cacheTileInfo(), nullptr);
        map->clearMemCache(); // legacy: every cache is tested for nil
    }
    const auto map = rig.make(rig.zmp({{"CacheType", "9"}, {"UseMemCache", "1"}, {"MemCacheCapacity", "1"}}));
    const auto cache = map->cacheTileInfo();
    ASSERT_NE(cache, nullptr);
    cache->add({1, 2}, 3, nullptr, someInfo());
    cache->add({4, 5}, 6, nullptr, someInfo());
    // capacity 1: the second entry evicted the first
    EXPECT_FALSE(hit(*cache, {1, 2}, 3));
    EXPECT_TRUE(hit(*cache, {4, 5}, 6));
}

TEST(MapTypeMemCache, ClearMemCacheEmptiesTheTileInfoCache) {
    Rig rig;
    const auto map = rig.make(rig.zmp({{"CacheType", "9"}}));
    const auto cache = map->cacheTileInfo();
    ASSERT_NE(cache, nullptr);
    cache->add({1, 2}, 3, nullptr, someInfo());
    ASSERT_TRUE(hit(*cache, {1, 2}, 3));
    map->clearMemCache();
    EXPECT_FALSE(hit(*cache, {1, 2}, 3));
}

TEST(MapTypeMemCache, AVersionChangeClearsTheCacheAndAnUnchangedVersionDoesNot) {
    Rig rig;
    const auto map = rig.make(rig.zmp({{"CacheType", "9"}, {"Version", "V1"}}));
    const auto cache = map->cacheTileInfo();
    ASSERT_NE(cache, nullptr);
    cache->add({1, 2}, 3, nullptr, someInfo());
    map->versionRequestConfig()->setVersion(u"V1"_s); // same value: no change, no notification
    EXPECT_TRUE(hit(*cache, {1, 2}, 3));
    map->versionRequestConfig()->setVersion(u"V2"_s); // OnVersionChange -> ClearMemCache
    EXPECT_FALSE(hit(*cache, {1, 2}, 3));
}

TEST(MapTypeMemCache, EveryGcTickRunsTheTtlSweepUnlessTheStrategyIsNever) {
    // D row 2 of T-0140: the owner drives clearExpired() from the GC notifier. clearExpired fires
    // clearedByTtl() on every call, except for the strategy Never (3), which returns at once.
    Rig rig;
    for (const auto& [strategy, expectedTicks] : {std::pair{"1", 2}, std::pair{"3", 0}}) {
        SCOPED_TRACE(strategy);
        const auto map = rig.make(rig.zmp({{"CacheType", "9"}, {"MemCacheClearStrategy", strategy}}));
        const auto cache = map->cacheTileInfo();
        ASSERT_NE(cache, nullptr);
        std::atomic<int> ticks{0};
        const core::Subscription sub = cache->clearedByTtl().subscribe([&ticks] { ++ticks; });
        rig.gc.notify();
        rig.gc.notify();
        EXPECT_EQ(ticks.load(), expectedTicks);
    }
}

// --- shortFolderName (:555-558)
// -------------------------------------------------------------------------------

TEST(MapTypeShortFolderName, TheLastFolderOfTheNameInCacheWithOrWithoutTrailingSeparator) {
    // ExtractFileName(ExtractFileDir(IncludeTrailingPathDelimiter(NameInCache))).
    struct Row {
        const char* nameInCache;
        const char* expected;
    };
    const std::vector<Row> rows = {
        {"a/b/", "b"},
        {"a\\b\\", "b"},
        {"b", "b"},
        {"", ""},
        {"x/y/z", "z"},
        {"\xD0\x93\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB4/\xD0\x9A\xD0\xB0\xD1\x80\xD1\x82\xD0\xB0\\",
         "\xD0\x9A\xD0\xB0\xD1\x80\xD1\x82\xD0\xB0"},
        {"a\\b/c", "c"},
    };
    Rig rig;
    for (const Row& row : rows) {
        SCOPED_TRACE(row.nameInCache);
        const auto map = rig.make(rig.zmp({{"CacheType", "9"}, {"NameInCache", row.nameInCache}}));
        EXPECT_EQ(map->shortFolderName().toUtf8(), QByteArray(row.expected));
    }
}

// Deviation (D row of T-0140): ':' is a drive delimiter for Delphi, an ordinary character here.
TEST(MapTypeShortFolderName, ColonIsAnOrdinaryCharacterNotADriveDelimiter) {
    Rig rig;
    const auto a = rig.make(rig.zmp({{"CacheType", "9"}, {"NameInCache", "a:b"}}));
    EXPECT_EQ(a->shortFolderName().toUtf8(), QByteArray("a:b")); // legacy: "b"
    const auto c = rig.make(rig.zmp({{"CacheType", "9"}, {"NameInCache", "C:\\"}}));
    EXPECT_EQ(c->shortFolderName().toUtf8(), QByteArray("C:")); // legacy: ""
}

// --- getTileShowName (:580-597)
// -----------------------------------------------------------------------------

struct ShowRow {
    storage::StorageClass storageClass;
    storage::VersionSupport version;
    const char* storeString; // nullptr = null version
    const char* expected;
};

TEST(MapTypeShowName, OneVectorPerStorageClassAndVersionSupport) {
    using storage::StorageClass;
    using storage::VersionSupport;
    // The content type of the map is image/jpeg -> image/jpg, ".jpg" (the Ext of the ZMP is ".png" and must
    // not be the suffix). Tile x=5, y=7, zoom 9 -> "z10/x5/y7"; the storage's own name is the literal FILE.
    const std::vector<ShowRow> rows = {
        {StorageClass::InSeparateFiles, VersionSupport::Ignored, "V1", "FILE"},
        {StorageClass::InSeparateFiles, VersionSupport::MultiVersions, "V1", "FILE/vV1"},
        {StorageClass::Folder, VersionSupport::Ignored, "V1", "FILE :: z10/x5/y7.jpg"},
        {StorageClass::OneFile, VersionSupport::Stored, "V1", "FILE :: z10/x5/y7.jpg"},
        {StorageClass::Folder, VersionSupport::MultiVersions, "V1", "FILE :: z10/x5/y7.jpg/vV1"},
        {StorageClass::Folder, VersionSupport::MultiVersions, "\xD0\x92\xD0\xB5\xD1\x80",
         "FILE :: z10/x5/y7.jpg/v\xD0\x92\xD0\xB5\xD1\x80"},
        {StorageClass::InMemory, VersionSupport::MultiVersions, "", "FILE :: z10/x5/y7.jpg"},
        {StorageClass::Other, VersionSupport::MultiVersions, nullptr, "FILE :: z10/x5/y7.jpg"},
        {StorageClass::OneFile, VersionSupport::Ignored, nullptr, "FILE :: z10/x5/y7.jpg"},
    };
    Rig rig;
    for (const ShowRow& row : rows) {
        SCOPED_TRACE(std::string(row.expected));
        const auto script = std::make_shared<Script>();
        script->abilities.storageClass = row.storageClass;
        script->abilities.version = row.version;
        script->fileName = u"FILE"_s;
        const auto map = rig.make(rig.zmp({{"CacheType", "2"}, {"MainStorageContentType", "image/jpeg"}}),
                                  nullptr, rig.scriptedList(script));
        storage::MapVersionInfoPtr version;
        if (row.storeString != nullptr) {
            version = rig.versions->byStoreString(QString::fromUtf8(row.storeString));
        }
        // legacy joins with PathDelim (u_MapType.pas:588-595): the native separator, a backslash on Windows
        QString expected = QString::fromUtf8(row.expected);
        expected.replace(u'/', QDir::separator());
        EXPECT_EQ(map->getTileShowName({5, 7}, 9, version), expected);
        // the arguments reach the storage unchanged
        ASSERT_EQ(script->nameCalls.size(), 1u);
        EXPECT_EQ(script->nameCalls[0].xy, (geo::PointI{5, 7}));
        EXPECT_EQ(script->nameCalls[0].zoom, 9);
        EXPECT_EQ(script->nameCalls[0].version, version);
    }
}

// --- nextVersion (:790-830)
// ---------------------------------------------------------------------------------

class VersionRig {
public:
    Rig rig;
    std::shared_ptr<Script> script = std::make_shared<Script>();
    std::unique_ptr<MapType> map;
    storage::MapVersionFactory factory;

    explicit VersionRig(storage::VersionSupport support = storage::VersionSupport::MultiVersions) {
        script->abilities.version = support;
        script->abilities.storageClass = storage::StorageClass::Folder;
        script->fileName = u"FILE"_s;
        map = rig.make(rig.zmp(scripted()), nullptr, rig.scriptedList(script));
    }

    void setVersions(const std::vector<const char*>& names) {
        auto list = std::make_shared<std::vector<storage::MapVersionInfoPtr>>();
        for (const char* name : names) {
            list->push_back(factory.byStoreString(QString::fromUtf8(name)));
        }
        script->versions = storage::MapVersionList{list};
    }

    // The view centred at lon/lat on the map's zoom-5 projection.
    [[nodiscard]] geo::LocalCoordConverterPtr view(double lon, double lat) const {
        const geo::ProjectionPtr projection = map->projectionSet()->zoom(5);
        return viewOn(projection, projection->lonLat2PixelPosFloat({lon, lat}));
    }

    // A view on the geographic (EPSG:4326) projection, which reaches the poles the Mercator map does not.
    [[nodiscard]] static geo::LocalCoordConverterPtr geographicView(double lon, double lat) {
        static const auto datums = std::make_shared<const geo::DatumFactory>(geo::HashSeed{});
        static const geo::ProjectionSetFactory factory{geo::HashSeed{}, datums};
        const geo::ProjectionPtr projection =
            factory.byCode(geo::kProjEpsgGeographic, geo::kTileSplitQuadrate256x256)->zoom(5);
        return viewOn(projection, projection->lonLat2PixelPosFloat({lon, lat}));
    }

    [[nodiscard]] static geo::LocalCoordConverterPtr viewOn(const geo::ProjectionPtr& projection,
                                                            geo::DoublePoint center) {
        return std::make_shared<geo::LocalCoordConverter>(
            core::kNoHash, geo::RectI{0, 0, 256, 256}, geo::RectI{0, 0, 256, 256},
            geo::DoubleRect{0, 0, 256, 256}, center, projection, 1.0, geo::DoublePoint{0, 0});
    }

    // Sets the base version, steps, returns the version the map holds afterwards.
    QString step(const char* base, int stepBy) { return step(base, stepBy, view(30.0, 60.0)); }
    QString step(const char* base, int stepBy, const geo::LocalCoordConverterPtr& at) {
        map->versionRequestConfig()->setVersion(QString::fromUtf8(base));
        EXPECT_EQ(map->versionRequest()->getStatic()->baseVersion->storeString(), QString::fromUtf8(base));
        map->nextVersion(at, stepBy);
        return map->versionRequestConfig()->version();
    }
};

TEST(MapTypeNextVersion, StepsThroughTheListWithAWrapAtBothEndsAndAZeroNotAModuloOnOverflow) {
    // The list as the storage returns it (not sorted): B, A, C. Index = position of the base, + step; >=
    // Count gives 0, < 0 gives Count - 1 (u_MapType.pas:817-824); a base that is not in the list has index
    // -1.
    struct Row {
        const char* base;
        int step;
        const char* expected;
    };
    const std::vector<Row> rows = {
        {"A", 1, "C"},  {"A", -1, "B"}, {"C", 1, "B"},
        {"B", -1, "C"}, {"A", 4, "B"}, // 1 + 4 = 5 >= 3 -> 0 (a modulo would give C)
        {"A", -3, "C"},                // 1 - 3 = -2 < 0 -> 2 (a modulo would give A)
        {"Z", 1, "B"},  {"Z", -1, "C"}, {"Z", 2, "A"},
    };
    VersionRig r;
    r.setVersions({"B", "A", "C"});
    for (const Row& row : rows) {
        SCOPED_TRACE(std::string(row.base) + " " + std::to_string(row.step));
        EXPECT_EQ(r.step(row.base, row.step), QString::fromUtf8(row.expected));
    }
}

TEST(MapTypeNextVersion, AsksTheStorageForTheTileUnderTheViewCentreWithTheNilRequest) {
    VersionRig r;
    r.setVersions({"B", "A", "C"});
    (void)r.step("A", 1);
    // lon 30, lat 60 at zoom 5 (Google): x = floor((30 + 180) / 360 * 32) = 18, y = floor(9.29) = 9.
    ASSERT_EQ(r.script->listCalls.size(), 1u);
    EXPECT_EQ(r.script->listCalls[0].xy, (geo::PointI{18, 9}));
    EXPECT_EQ(r.script->listCalls[0].zoom, 5);
    // legacy passes nil: no base version, show all
    EXPECT_EQ(r.script->listCalls[0].request.baseVersion, nullptr);
    EXPECT_TRUE(r.script->listCalls[0].request.showOtherVersions);
}

TEST(MapTypeNextVersion, AStorageWithoutMultipleVersionsIsNotAskedAndKeepsTheVersion) {
    for (const auto support : {storage::VersionSupport::Ignored, storage::VersionSupport::Stored}) {
        VersionRig r(support);
        r.setVersions({"B", "A", "C"});
        EXPECT_EQ(r.step("A", 1), u"A"_s);
        EXPECT_TRUE(r.script->listCalls.empty());
    }
}

TEST(MapTypeNextVersion, AnEmptyListOrAFailedCallOrNoListKeepsTheVersion) {
    // Legacy reads Item[0] / Item[-1] of an empty list (L row of T-0140); the port keeps the version (D row
    // 1).
    VersionRig r;
    r.script->versions = storage::MapVersionList{std::make_shared<std::vector<storage::MapVersionInfoPtr>>()};
    EXPECT_EQ(r.step("A", 1), u"A"_s);
    EXPECT_EQ(r.step("A", -1), u"A"_s);
    EXPECT_EQ(r.script->listCalls.size(), 2u); // the storage was asked, the answer was empty

    r.script->versions = core::fail(core::ErrorCode::IoError, u"scripted"_s);
    EXPECT_EQ(r.step("A", 1), u"A"_s);
    EXPECT_EQ(r.step("A", -1), u"A"_s);

    r.script->versions = storage::MapVersionList{};
    EXPECT_EQ(r.step("A", 1), u"A"_s);
    EXPECT_EQ(r.script->listCalls.size(), 5u);
}

TEST(MapTypeNextVersion, AViewCentreOutsideTheProjectionIsNotAskedAndAnEmptyViewIsIgnored) {
    VersionRig r;
    r.setVersions({"B", "A", "C"});
    // lat 89 is on the geographic view but north of the Mercator range (u_MapType.pas:803 CheckLonLatPos)
    EXPECT_EQ(r.step("A", 1, VersionRig::geographicView(30.0, 89.0)), u"A"_s);
    EXPECT_TRUE(r.script->listCalls.empty());
    r.map->nextVersion(nullptr, 1);
    EXPECT_EQ(r.map->versionRequestConfig()->version(), u"A"_s);
    EXPECT_TRUE(r.script->listCalls.empty());
}

// --- the invariant of 2.11.5
// --------------------------------------------------------------------------------

TEST(MapTypeInList, APlainMapIsListedWithItsOwnGuiConfigAndOrderedByItsStoredNumber) {
    // MapTypeGuiConfigList takes the "dynamic_cast gives null -> the map is listed" branch for a real,
    // non-proxy map; its constructor throws on a null GUI config or ZMP. pnum is read from the map's own INI
    // section, so the number order (Zeta=1, Alpha=2) differs from the name order (Alpha, Zeta) and from the
    // insertion order.
    Rig rig;
    const auto ini = maptype::test::iniFrom("[Alpha]\r\npnum=2\r\n[Zeta]\r\npnum=1\r\n");
    const IZmpInfoPtr alphaZmp = rig.zmp({{"name", "Alpha"}, {"CacheType", "9"}});
    const IZmpInfoPtr zetaZmp = rig.zmp({{"name", "Zeta"}, {"CacheType", "9"}});
    const std::shared_ptr<MapType> alpha = rig.make(alphaZmp, ini->subItem(u"Alpha"));
    const std::shared_ptr<MapType> zeta = rig.make(zetaZmp, ini->subItem(u"Zeta"));
    ASSERT_NE(alpha->guiConfig(), nullptr);
    ASSERT_NE(zeta->guiConfig(), nullptr);
    ASSERT_NE(alpha->zmp(), nullptr);
    ASSERT_EQ(dynamic_cast<const IMapTypeProxy*>(alpha.get()), nullptr);

    const auto set = maptype::test::buildSet({alpha, zeta});
    MapTypeGuiConfigList list(rig.language, set);
    list.setSortOrder(MapTypeGuiConfigListSortOrder::ByMapNumber);
    EXPECT_EQ(*list.orderedMapGuidList(), (std::vector<QUuid>{zetaZmp->guid(), alphaZmp->guid()}));
    list.setSortOrder(MapTypeGuiConfigListSortOrder::ByMapName);
    EXPECT_EQ(*list.orderedMapGuidList(), (std::vector<QUuid>{alphaZmp->guid(), zetaZmp->guid()}));
}

// --- the subsystem is the map's own and goes with it (3.1 R1-R4)
// --------------------------------------------

// A tile downloaded through the map's subsystem lands in the map's storage: the subsystem got the map's
// storage, its content type (image/png) and its configs.
TEST(MapTypeDownload, ATileDownloadedThroughTheMapIsStoredInTheMapsStorage) {
    Rig rig;
    auto fake = std::make_shared<FnDownloader>();
    fake->fn = [](const net::DownloadRequest&, std::stop_token) {
        return net::result::ok(200, "HTTP/1.1 200 OK\r\n", "image/png", kPng);
    };
    rig.transport = std::make_shared<SharedDownloaderFactory>(fake);
    const auto map = rig.make(rig.zmp(scripted()));
    std::latch finished(1);
    const auto notifier = std::make_shared<TileRequestTaskFinishNotifier>(
        [&finished](const ITileRequestTask&, const ITileRequestResultPtr&) { finished.count_down(); });
    const auto subsystem = map->tileDownloadSubsystem();
    const auto task = subsystem->getRequestTask({}, {}, notifier, geo::PointI{3, 7}, 5, nullptr, false);
    ASSERT_NE(task, nullptr);
    subsystem->download(task);
    finished.wait();
    const QStringList files = rig.cacheFiles();
    ASSERT_EQ(files.size(), 1);
    QFile f(files.front());
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(f.readAll(), kPng);
    const auto info = map->tileStorage()->tileInfo({3, 7}, 5, nullptr, storage::LoadMode::WithoutData);
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->isExists());
}

// R1 under the sanitizers: the map is destroyed while a transfer is in flight. The fake transport holds the
// transfer until its stop token fires, so the stop can only come from the destruction; the worker is joined
// before the map's storage and configs go (the destructor returns only after it), the task is finished
// exactly once, nothing is stored, and the saver has unsubscribed from the storage state by then.
TEST(MapTypeDestruction, DestroyingTheMapWhileATransferIsInFlightJoinsTheWorkerAndFinishesTheTaskOnce) {
    Rig rig;
    std::latch inFlight(1);
    std::atomic<int> transfers{0};
    auto fake = std::make_shared<FnDownloader>();
    fake->fn = [&](const net::DownloadRequest&, std::stop_token stop) {
        ++transfers;
        inFlight.count_down();
        return waitForStop(stop) ? net::result::canceled()
                                 : net::result::ok(200, "HTTP/1.1 200 OK\r\n", "image/png", kPng);
    };
    rig.transport = std::make_shared<SharedDownloaderFactory>(fake);
    auto map = rig.make(rig.zmp(scripted()));
    const auto storage = map->tileStorage();
    EXPECT_EQ(storage->stateChangeable().changed().subscriberCount(), 1u); // the saver listens

    std::atomic<int> finishes{0};
    const auto notifier = std::make_shared<TileRequestTaskFinishNotifier>(
        [&finishes](const ITileRequestTask&, const ITileRequestResultPtr&) { ++finishes; });
    const auto task =
        map->tileDownloadSubsystem()->getRequestTask({}, {}, notifier, geo::PointI{3, 7}, 5, nullptr, false);
    ASSERT_NE(task, nullptr);
    map->tileDownloadSubsystem()->download(task);
    inFlight
        .wait(); // the overlap: the transfer is inside the transport while the test thread destroys the map
    map.reset();
    EXPECT_EQ(transfers.load(), 1);
    EXPECT_EQ(finishes.load(), 1);
    EXPECT_TRUE(rig.cacheFiles().isEmpty());
    EXPECT_EQ(storage->stateChangeable().changed().subscriberCount(), 0u);
}

// What the destructor body of :488-492 and the owner's subscriptions leave behind: nothing reaches the freed
// map from a config or the GC notifier that outlives it. Under ASan a dangling `this` would be reported; the
// values show that the callbacks are gone. (The version subscription is also a member that is destroyed with
// the map, so removing it from the destructor body is unobservable.)
TEST(MapTypeDestruction, ANotificationAfterTheMapIsGoneTouchesNothing) {
    Rig rig;
    auto map = rig.make(rig.zmp({{"CacheType", "9"}, {"Version", "V1"}}));
    const auto cache = map->cacheTileInfo();
    const auto config = map->versionRequestConfig();
    ASSERT_NE(cache, nullptr);
    std::atomic<int> ticks{0};
    const core::Subscription sub = cache->clearedByTtl().subscribe([&ticks] { ++ticks; });
    cache->add({1, 2}, 3, nullptr, someInfo());
    map.reset();
    config->setVersion(u"V2"_s); // would call clearMemCache() of the freed map
    EXPECT_TRUE(hit(*cache, {1, 2}, 3));
    rig.gc.notify(); // would run the TTL sweep
    EXPECT_EQ(ticks.load(), 0);
}

TEST(MapTypeDestruction, DestroyingAMapRightAfterConstructionFiftyTimesIsClean) {
    Rig rig;
    const IZmpInfoPtr zmp = rig.zmp({{"CacheType", "9"}});
    for (int round = 0; round < 50; ++round) {
        const auto map = rig.make(zmp);
        EXPECT_NE(map->tileDownloadSubsystem(), nullptr);
    }
    EXPECT_EQ(rig.gc.subscriberCount(), 0u); // no GC subscription of a map outlives it
}

} // namespace
