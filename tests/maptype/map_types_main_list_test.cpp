// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MapTypesMainList.pas (TMapTypesMainList, EMapTypesNoMaps), ticket T-0150, docs/specs/maptype.md
// 2.11.7. FEAT-NET-001 (loadMaps) and FEAT-NET-030 (renumbering, saveMaps): contributes to both, closes
// neither. Real objects: the 514 stock ZMPs (tests/fixtures/zmp/sas.maps) with the stock Maps.ini, real
// MapType objects with their download subsystems, real RAM tile storages (CacheType=9) for nextMapWithTile, a
// real TileStorageTypeListSimple. Scripted: an idle transport (no request is ever sent), a content-type
// registry that lacks a type (a MapType whose constructor throws), and a writer that logs the order of its
// calls. Expected values are literals taken from the Pascal text (u_MapTypesMainList.pas line numbers in the
// comments).
#include "cg/core/config_provider_ini.h"
#include "cg/core/content_type.h"
#include "cg/core/notifier.h"
#include "cg/core/path_config.h"
#include "cg/geo/local_coord_converter.h"
#include "cg/maptype/map_type.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/map_type_gui_config_list.h"
#include "cg/maptype/map_type_set.h"
#include "cg/maptype/map_types_main_list.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/zmp_file_names_iterator_factory.h"
#include "cg/maptype/zmp_info.h"
#include "cg/maptype/zmp_info_set.h"
#include "cg/net/global_download_config.h"
#include "cg/net/inet_config.h"
#include "cg/storage/global_cache_config.h"
#include "cg/storage/tile_date.h"
#include "cg/storage/tile_storage.h"
#include "cg/storage/tile_storage_type_list_simple.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "fake_map_type.h"
#include "maptype_test_util.h"
#include "request_builder_test_util.h"

using namespace Qt::StringLiterals;
using namespace cg;
using namespace cg::maptype;

namespace {

// docs/specs/net.md 7.0: no test may reach the real network.
struct ForceNoExternalNetwork {
    ForceNoExternalNetwork() { qputenv("CG_NET_NO_EXTERNAL_NETWORK", "1"); }
};
const ForceNoExternalNetwork kForceNoExternalNetwork;

// u_ResStrings.pas:83 and :108
constexpr auto kTextNoMaps = "Unable to load map(s)! Aborting...";
constexpr auto kTextMainMapNotExists = "You need at least one MAP among your ZMP files";
// MapType's own message for a content type its registry lacks (map_type.cpp)
constexpr auto kTextContentTypeMissing = "MapType: content type not in the registry";

// The default main maps of u_MapTypesMainList.pas:183-200
const QByteArray kGoogle = "{CBA03063-23D9-4FA4-931A-9182B98644B1}";
const QByteArray kOsm = "{D643C5A1-4331-4387-9BE2-DDDF185C117E}";
const QByteArray kYandex = "{8238C84A-D37E-45E1-A735-FBCFBCD4168C}";
// Other maps: letters in the GUIDs, so that the upper-case form of the section names is observable.
const QByteArray kMapA = "{AB0000A1-0000-0000-0000-0000000000A1}";
const QByteArray kMapB = "{AB0000B2-0000-0000-0000-0000000000B2}";
const QByteArray kLayer = "{AB0000C3-0000-0000-0000-0000000000C3}";
const QByteArray kMapC = "{AB0000D4-0000-0000-0000-0000000000D4}";
const QByteArray kMapD = "{AB0000E5-0000-0000-0000-0000000000E5}";
const QByteArray kMapAbsent = "{AB0000F6-0000-0000-0000-0000000000F6}";

QUuid uuid(const QByteArray& braced) {
    return QUuid(QString::fromLatin1(braced));
}

// --- the rig
// ------------------------------------------------------------------------------------------------

class IdleDownloaderFactory final : public net::IDownloaderFactory {
public:
    net::IDownloaderPtr build(const net::DownloaderOptions& /*options*/) override { return downloader_; }

private:
    std::shared_ptr<net::IDownloader> downloader_ = std::make_shared<maptype::test::ScriptedDownloader>();
};

// A registry that knows only the JPEG entry of the stock table (the one with extension ".jpg").
std::shared_ptr<const core::ContentTypeRegistry> jpegOnlyRegistry() {
    auto registry = std::make_shared<core::ContentTypeRegistry>();
    const core::ContentTypeInfo* jpeg = core::ContentTypeRegistry::standard().infoByExt(".jpg");
    EXPECT_NE(jpeg, nullptr);
    if (jpeg != nullptr) {
        EXPECT_TRUE(registry->addByType(jpeg->type, *jpeg).has_value());
        EXPECT_TRUE(registry->addByExt(".jpg", *jpeg).has_value());
    }
    return registry;
}

std::shared_ptr<const core::ContentTypeRegistry> standardRegistry() {
    return {std::shared_ptr<void>(), &core::ContentTypeRegistry::standard()};
}

// One synthetic ZMP: a template-only map with a RAM cache (CacheType=9, no disk).
struct ZSpec {
    QByteArray guid;
    QByteArray name;
    bool layer = false;
    bool jpeg = false; // else png
};

class Env {
public:
    maptype::test::ZmpEnv zmpEnv;
    QTemporaryDir root;
    core::Notifier gc; // declared before everything a map holds: outlives every map
    std::stop_source appClosing;
    std::shared_ptr<core::ILanguageManager> language = std::make_shared<maptype::test::FakeLanguageManager>();
    std::shared_ptr<net::InetConfig> inet = std::make_shared<net::InetConfig>();
    std::shared_ptr<net::GlobalDownloadConfig> download = std::make_shared<net::GlobalDownloadConfig>();
    std::shared_ptr<script::IProjConverterFactory> proj =
        std::make_shared<maptype::test::CountingProjFactory>();
    storage::MapVersionFactoryPtr versions = std::make_shared<storage::MapVersionFactory>();
    std::shared_ptr<storage::GlobalCacheConfig> cacheConfig;
    std::shared_ptr<storage::TileStorageTypeListSimple> typeList;
    std::shared_ptr<net::IDownloaderFactory> transport = std::make_shared<IdleDownloaderFactory>();
    std::shared_ptr<const core::ContentTypeRegistry> registry = standardRegistry();
    std::vector<std::shared_ptr<ZmpInfoSet>> sets;

    Env() {
        cacheConfig = std::make_shared<storage::GlobalCacheConfig>(
            core::PathConfig::create(QString(), root.filePath(u"cache"_s), nullptr));
        typeList = std::make_shared<storage::TileStorageTypeListSimple>(versions, *cacheConfig, gc);
        inet->proxy().setUseSystemSettings(false);
    }

    // Writes the ZMPs as template-only map folders and loads them as one ZMP set (in the given order).
    std::shared_ptr<ZmpInfoSet> makeSet(const std::vector<ZSpec>& specs) {
        QStringList names;
        int n = 0;
        for (const ZSpec& spec : specs) {
            ++n;
            const QString name = u"m"_s + QString::number(n) + u".zmp"_s;
            QByteArray text =
                "[PARAMS]\r\nGUID=" + spec.guid + "\r\nname=" + spec.name + "\r\nNameInCache=cache" +
                QByteArray::number(n) + "\r\nDefURLBase=http://127.0.0.1:9/{z}/{x}/{y}" +
                (spec.jpeg ? ".jpg" : ".png") + "\r\nContentType=" + (spec.jpeg ? "image/jpg" : "image/png") +
                "\r\nExt=" + (spec.jpeg ? ".jpg" : ".png") +
                "\r\nprojection=1\r\nsradiusa=6378137\r\nsradiusb=6356752\r\nCacheType=9\r\n";
            if (spec.layer) {
                text += "asLayer=1\r\n";
            }
            const QString dir = root.filePath(u"zmps/"_s + name);
            EXPECT_TRUE(QDir().mkpath(dir));
            QFile f(dir + u"/params.txt"_s);
            EXPECT_TRUE(f.open(QIODevice::WriteOnly));
            f.write(text);
            f.close();
            names.append(name);
        }
        QStringList errors;
        sets.push_back(zmpEnv.load(root.filePath(u"zmps"_s), names, errors));
        EXPECT_TRUE(errors.isEmpty()) << errors.join(u'\n').toStdString();
        return sets.back();
    }

    std::shared_ptr<ZmpInfoSet> stockSet(const QStringList& relativeNames, const QString& zmpRoot) {
        QStringList errors;
        sets.push_back(zmpEnv.load(zmpRoot, relativeNames, errors));
        EXPECT_TRUE(errors.isEmpty());
        return sets.back();
    }

    static std::unique_ptr<MapTypesMainList> newList(const std::shared_ptr<const IZmpInfoSet>& set) {
        return std::make_unique<MapTypesMainList>(std::make_shared<MapTypeSetBuilderFactory>(), set);
    }

    // The messages showError received go to `errors`.
    void load(MapTypesMainList& list, const core::IConfigProviderPtr& local,
              const core::IConfigProviderPtr& order, QStringList& errors, const std::stop_token& token) {
        list.loadMaps(language, versions, cacheConfig, typeList, gc, token, inet, download, transport,
                      registry, test::testContentConverters(), proj, 7, root.filePath(u"logs"_s), local,
                      order, [&errors](const QString& message) { errors.append(message); });
    }
    void load(MapTypesMainList& list, const core::IConfigProviderPtr& local,
              const core::IConfigProviderPtr& order, QStringList& errors) {
        load(list, local, order, errors, appClosing.get_token());
    }
};

// A Maps.ini of sections `[GUID]` carrying the given pnum values (and optionally Enabled=0 for one section).
std::shared_ptr<core::ConfigProviderIni> localIni(const std::vector<std::pair<QByteArray, int>>& pnums,
                                                  const QByteArray& disabledGuid = {}) {
    QByteArray text;
    for (const auto& [guid, pnum] : pnums) {
        text += "[" + guid + "]\r\npnum=" + QByteArray::number(pnum) + "\r\n";
        if (guid == disabledGuid) {
            text += "Enabled=0\r\n";
        }
    }
    return maptype::test::iniFrom(text);
}

// The [MapsList] section with SortOrder=0 (ByMapNumber): the order of the pnum values, so that the tests give
// the order.
core::IConfigProviderPtr byNumber() {
    return maptype::test::iniFrom("[MapsList]\r\nSortOrder=0\r\n")->subItem(u"MapsList");
}

std::vector<QUuid> orderOf(const MapTypesMainList& list) {
    const MapGuidListPtr ordered = list.guiConfigList()->orderedMapGuidList();
    return ordered ? *ordered : std::vector<QUuid>{};
}

// Logs the order of the calls over a whole writer tree: "s:<section>" for a sub-item, "w:<key>" for a write.
class OrderWriter final : public core::IConfigWriter {
public:
    OrderWriter(core::IConfigWriterPtr inner, std::shared_ptr<QStringList> log)
        : inner_(std::move(inner)), log_(std::move(log)) {}
    core::IConfigProviderPtr subItem(QStringView id) const override { return inner_->subItem(id); }
    QVariant value(QStringView id) const override { return inner_->value(id); }
    QByteArray binary(QStringView id) const override { return inner_->binary(id); }
    QStringList subItemNames() const override { return inner_->subItemNames(); }
    QStringList valueNames() const override { return inner_->valueNames(); }
    core::IConfigWriterPtr orCreateSubItem(QStringView id) override {
        log_->push_back(u"s:"_s + id.toString());
        return std::make_shared<OrderWriter>(inner_->orCreateSubItem(id), log_);
    }
    void deleteSubItem(QStringView id) override { inner_->deleteSubItem(id); }
    void deleteValue(QStringView id) override { inner_->deleteValue(id); }
    void deleteValues() override { inner_->deleteValues(); }
    void writeValue(QStringView id, const QVariant& v) override {
        log_->push_back(u"w:"_s + id.toString());
        inner_->writeValue(id, v);
    }
    void writeBinary(QStringView id, const QByteArray& v) override {
        log_->push_back(u"w:"_s + id.toString());
        inner_->writeBinary(id, v);
    }

private:
    core::IConfigWriterPtr inner_;
    std::shared_ptr<QStringList> log_;
};

// The five-entry list of the nextMapWithTile and save tests, in pnum order: A, B, LAYER, C, D (indices 0..4).
const std::vector<ZSpec> kFiveMaps = {
    {kMapA, "Map A", false, false}, {kMapB, "Map B", false, false}, {kLayer, "Layer", true, false},
    {kMapC, "Map C", false, false}, {kMapD, "Map D", false, false},
};
// Given unsorted and with gaps; order by number: B(7) A(50) D(120) LAYER(200) C(300).
const std::vector<std::pair<QByteArray, int>> kFivePnums = {
    {kMapA, 50}, {kMapB, 7}, {kLayer, 200}, {kMapC, 300}, {kMapD, 120},
};

// --- proof 1: the corpus ----------------------------------------------------------------------------------

// Literal counts: `asLayer=1` occurs in 190 of the 514 params.txt (one line each, none in a second section),
// so 324 maps and 190 layers.
TEST(MapTypesMainListCorpus, TheStock514ZmpsGiveTheMapsAndTheLayersAndNoError) {
    CG_SKIP_IF_NO_CORPUS();
    Env env;
    const QString zmpRoot = maptype::test::fixtureDir() + u"/zmp/sas.maps"_s;
    const auto set = env.stockSet(zmpFileNames(zmpRoot), zmpRoot);
    ASSERT_EQ(set->guids().size(), 514u);
    auto opened = core::ConfigProviderIni::open(maptype::test::fixtureDir() + u"/zmp/Maps.ini"_s);
    ASSERT_TRUE(opened.has_value());
    const auto stockIni = opened.value();

    const auto list = Env::newList(set);
    QStringList errors;
    env.load(*list, stockIni, maptype::test::iniFrom({}), errors);
    EXPECT_TRUE(errors.isEmpty()) << errors.join(u'\n').toStdString();
    ASSERT_NE(list->fullMapsSet(), nullptr);
    EXPECT_EQ(list->fullMapsSet()->count(), 514);
    ASSERT_NE(list->mapsSet(), nullptr);
    EXPECT_EQ(list->mapsSet()->count(), 324);
    ASSERT_NE(list->layersSet(), nullptr);
    EXPECT_EQ(list->layersSet()->count(), 190);
    EXPECT_EQ(list->fullMapsSetChangeable()->getStatic(), list->fullMapsSet()); // the same object
    EXPECT_EQ(list->defaultMainMapGuid(), uuid(kGoogle));                       // :183-200
    EXPECT_EQ(list->guiConfigList()->orderedMapGuidList()->size(), 514u);

    // OQ-10: the stock pnum is an oracle for the default ByMapName order; mismatches are reported, not
    // failed.
    int mismatches = 0;
    std::string firstMismatches;
    for (const QUuid& guid : orderOf(*list)) {
        const auto section = stockIni->subItem(guid.toString(QUuid::WithBraces).toUpper());
        const int stock = section ? section->readInt(u"pnum", -1) : -1;
        const int now = list->fullMapsSet()->mapTypeByGuid(guid)->guiConfig()->sortIndex();
        if (stock != now) {
            if (++mismatches <= 40) {
                firstMismatches += guid.toString(QUuid::WithBraces).toStdString() +
                                   " stock=" + std::to_string(stock) + " port=" + std::to_string(now) + "\n";
            }
        }
    }
    RecordProperty("OQ10_pnum_mismatches", mismatches);
    std::cout << "[OQ-10] pnum mismatches against the stock Maps.ini: " << mismatches << " of 514\n"
              << firstMismatches;
}

// --- proof 2: the default and the first main map
// --------------------------------------------------------------

struct DefaultRow {
    const char* label;
    std::vector<std::tuple<QByteArray, bool, int>> maps; // guid, layer, pnum
    QByteArray expectedDefault;
    QByteArray expectedFirst;
};

TEST(MapTypesMainListDefault, TheFirstOfTheThreeThatIsAMapElseTheFirstMapOfTheMenuOrder) {
    // pnum gives the menu order (SortOrder=0). A default that exists only as a LAYER is not chosen (:194 asks
    // FMapsSet); with none of the three the first non-layer entry of the ordered list is (:202-217).
    const std::vector<DefaultRow> rows = {
        {"all three, Yandex listed first",
         {{kYandex, false, 10}, {kOsm, false, 20}, {kGoogle, false, 30}, {kMapA, false, 40}},
         kGoogle,
         kYandex},
        {"no Google: the second",
         {{kYandex, false, 10}, {kOsm, false, 20}, {kMapA, false, 40}},
         kOsm,
         kYandex},
        {"only Yandex of the three", {{kMapA, false, 10}, {kYandex, false, 20}}, kYandex, kMapA},
        {"none: the first map of the order, a layer in front of it",
         {{kLayer, true, 5}, {kMapB, false, 30}, {kMapA, false, 20}},
         kMapA,
         kMapA},
        {"Google only as a layer is not chosen", {{kGoogle, true, 1}, {kMapA, false, 20}}, kMapA, kMapA},
        {"Google only as a layer, OSM a map",
         {{kGoogle, true, 1}, {kOsm, false, 20}, {kMapA, false, 10}},
         kOsm,
         kMapA},
    };
    for (const DefaultRow& row : rows) {
        SCOPED_TRACE(row.label);
        Env env;
        std::vector<ZSpec> specs;
        std::vector<std::pair<QByteArray, int>> pnums;
        int n = 0;
        for (const auto& [guid, layer, pnum] : row.maps) {
            specs.push_back({guid, "Map " + QByteArray::number(++n), layer, false});
            pnums.emplace_back(guid, pnum);
        }
        const auto list = Env::newList(env.makeSet(specs));
        QStringList errors;
        env.load(*list, localIni(pnums), byNumber(), errors);
        ASSERT_TRUE(errors.isEmpty());
        EXPECT_EQ(list->defaultMainMapGuid(), uuid(row.expectedDefault));
        EXPECT_EQ(list->firstMainMapGuid(), uuid(row.expectedFirst));
    }
}

// --- proof 3: the renumbering -----------------------------------------------------------------------------

TEST(MapTypesMainListRenumbering, PnumIsOneToNInMenuOrderAndTheListNotifiesOnceForTheWholeRun) {
    Env env;
    const auto list = Env::newList(env.makeSet(kFiveMaps));

    // The notifier of the list exists only inside loadMaps; the SortOrder read (during readConfig of the
    // list) is the first moment its pointer is available, so the subscription is made from there.
    class SubscribingProvider final : public core::IConfigProvider {
    public:
        SubscribingProvider(core::IConfigProviderPtr inner, const MapTypesMainList& list)
            : inner_(std::move(inner)), list_(list) {}
        core::IConfigProviderPtr subItem(QStringView id) const override { return inner_->subItem(id); }
        QVariant value(QStringView id) const override {
            if (!subscribed_ && list_.guiConfigList()) {
                subscribed_ = true;
                sub_ = list_.guiConfigList()->changed().subscribe([this] { ++notifications; });
            }
            return inner_->value(id);
        }
        QByteArray binary(QStringView id) const override { return inner_->binary(id); }
        QStringList subItemNames() const override { return inner_->subItemNames(); }
        QStringList valueNames() const override { return inner_->valueNames(); }
        mutable std::atomic<int> notifications{0};

    private:
        core::IConfigProviderPtr inner_;
        const MapTypesMainList& list_;
        mutable bool subscribed_ = false;
        mutable core::Subscription sub_;
    };
    const auto order = std::make_shared<SubscribingProvider>(byNumber(), *list);

    QStringList errors;
    env.load(*list, localIni(kFivePnums), order, errors);
    ASSERT_TRUE(errors.isEmpty());

    const std::vector<QUuid> expectedOrder = {uuid(kMapB), uuid(kMapA), uuid(kMapD), uuid(kLayer),
                                              uuid(kMapC)};
    EXPECT_EQ(orderOf(*list), expectedOrder);
    // :397-408: 1..N in the order of the list, layers included
    const std::vector<int> expectedIndex = {1, 2, 3, 4, 5};
    for (std::size_t i = 0; i < expectedOrder.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(list->fullMapsSet()->mapTypeByGuid(expectedOrder[i])->guiConfig()->sortIndex(),
                  expectedIndex[i]);
    }
    // One notification of the readConfig of the list (SetChanged at the end of DoReadConfig) plus ONE for the
    // five pnum assignments made under the write lock (:400-408). Without the lock it is one per changed map.
    EXPECT_EQ(order->notifications.load(), 2);
}

// --- proof 4: saveMaps and the fixed point
// -------------------------------------------------------------------

TEST(MapTypesMainListSave, OneSectionPerMapInMenuOrderThenTheListAndSaveLoadSaveIsAFixedPoint) {
    Env env;
    const auto list = Env::newList(env.makeSet(kFiveMaps));
    QStringList errors;
    env.load(*list, localIni(kFivePnums, kMapD), byNumber(), errors);
    ASSERT_TRUE(errors.isEmpty());

    // order of the calls (:495-506): a section per map of the ordered list (layers included), then the list
    auto localOut = maptype::test::iniFrom({});
    auto listOut = maptype::test::iniFrom({});
    const auto log = std::make_shared<QStringList>();
    list->saveMaps(std::make_shared<OrderWriter>(localOut, log),
                   std::make_shared<OrderWriter>(listOut->orCreateSubItem(u"MapsList"), log));
    QStringList sections;
    for (const QString& entry : *log) {
        if (entry.startsWith(u"s:"_s)) {
            sections.append(entry.mid(2));
        }
    }
    const QStringList expectedSections = {
        u"{AB0000B2-0000-0000-0000-0000000000B2}"_s, u"{AB0000A1-0000-0000-0000-0000000000A1}"_s,
        u"{AB0000E5-0000-0000-0000-0000000000E5}"_s, u"{AB0000C3-0000-0000-0000-0000000000C3}"_s,
        u"{AB0000D4-0000-0000-0000-0000000000D4}"_s};
    EXPECT_EQ(sections, expectedSections);
    const qsizetype sortOrderAt = log->indexOf(u"w:SortOrder"_s);
    ASSERT_GE(sortOrderAt, 0);
    EXPECT_GT(sortOrderAt, log->lastIndexOf(u"s:{AB0000D4-0000-0000-0000-0000000000D4}"_s));
    // the content: the renumbered pnum, the disabled map
    EXPECT_EQ(localOut->subItem(expectedSections[0])->readInt(u"pnum", -1), 1);
    EXPECT_EQ(localOut->subItem(expectedSections[2])->readInt(u"pnum", -1), 3);
    EXPECT_FALSE(localOut->subItem(expectedSections[2])->readBool(u"Enabled", true));
    EXPECT_EQ(listOut->subItem(u"MapsList")->readInt(u"SortOrder", -1), 0);

    // fixed point: load what was written, write again
    const auto second = Env::newList(env.makeSet(kFiveMaps));
    env.load(*second, localOut, listOut->subItem(u"MapsList"), errors);
    ASSERT_TRUE(errors.isEmpty());
    auto localAgain = maptype::test::iniFrom({});
    auto listAgain = maptype::test::iniFrom({});
    second->saveMaps(localAgain, listAgain->orCreateSubItem(u"MapsList"));
    EXPECT_FALSE(localOut->file().toBytes().isEmpty());
    EXPECT_EQ(localAgain->file().toBytes(), localOut->file().toBytes());
    EXPECT_EQ(listAgain->file().toBytes(), listOut->file().toBytes());
}

// --- proof 5: the failures
// ------------------------------------------------------------------------------------

TEST(MapTypesMainListFailures, OneBrokenZmpIsReportedOnceAndTheOthersLoad) {
    Env env;
    env.registry =
        jpegOnlyRegistry(); // the png map's constructor throws (its content type is not registered)
    const auto list = Env::newList(
        env.makeSet({{kMapA, "A", false, true}, {kMapB, "B", false, false}, {kMapC, "C", false, true}}));
    QStringList errors;
    env.load(*list, maptype::test::iniFrom({}), maptype::test::iniFrom({}), errors);
    EXPECT_EQ(errors, QStringList{QString::fromLatin1(kTextContentTypeMissing)});
    EXPECT_EQ(list->fullMapsSet()->count(), 2);
    EXPECT_NE(list->fullMapsSet()->mapTypeByGuid(uuid(kMapA)), nullptr);
    EXPECT_EQ(list->fullMapsSet()->mapTypeByGuid(uuid(kMapB)), nullptr);
    EXPECT_NE(list->fullMapsSet()->mapTypeByGuid(uuid(kMapC)), nullptr);
    EXPECT_EQ(list->mapsSet()->count(), 2);
}

TEST(MapTypesMainListFailures, NoZmpIsTheNoMapsErrorWithTheLiteralText) {
    Env env;
    const auto list = Env::newList(env.makeSet({}));
    QStringList errors;
    try {
        env.load(*list, maptype::test::iniFrom({}), maptype::test::iniFrom({}), errors);
        FAIL() << "no exception";
    } catch (const MapTypesNoMapsError& e) {
        EXPECT_STREQ(e.what(), kTextNoMaps); // :287-289
    }
    EXPECT_TRUE(errors.isEmpty());
}

TEST(MapTypesMainListFailures, OnlyLayersIsAPlainRuntimeErrorNotTheNoMapsError) {
    Env env;
    const auto list = Env::newList(env.makeSet({{kLayer, "L1", true, false}, {kMapA, "L2", true, false}}));
    QStringList errors;
    try {
        env.load(*list, maptype::test::iniFrom({}), maptype::test::iniFrom({}), errors);
        FAIL() << "no exception";
    } catch (const MapTypesNoMapsError&) {
        FAIL() << "the layers-only abort must not be the NoMaps error";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), kTextMainMapNotExists); // :291-293
    }
    EXPECT_TRUE(errors.isEmpty());
}

TEST(MapTypesMainListFailures, EveryMapFailingIsReportedForEachThenTheNoMapsError) {
    Env env;
    env.registry = std::make_shared<core::ContentTypeRegistry>(); // knows nothing: every constructor throws
    const auto list = Env::newList(
        env.makeSet({{kMapA, "A", false, false}, {kMapB, "B", false, true}, {kLayer, "L", true, false}}));
    QStringList errors;
    try {
        env.load(*list, maptype::test::iniFrom({}), maptype::test::iniFrom({}), errors);
        FAIL() << "no exception";
    } catch (const MapTypesNoMapsError& e) {
        EXPECT_STREQ(e.what(), kTextNoMaps); // :378-380
    }
    EXPECT_EQ(errors.size(), 3);
}

TEST(MapTypesMainListFailures, TheOnlyMapFailingWhileALayerLoadsIsTheMainMapAbort) {
    Env env;
    env.registry = jpegOnlyRegistry();
    const auto list = Env::newList(env.makeSet({{kMapA, "A", false, false}, {kLayer, "L", true, true}}));
    QStringList errors;
    try {
        env.load(*list, maptype::test::iniFrom({}), maptype::test::iniFrom({}), errors);
        FAIL() << "no exception";
    } catch (const MapTypesNoMapsError&) {
        FAIL() << "a layer was built: the NoMaps abort must not fire";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), kTextMainMapNotExists); // :382-384
    }
    EXPECT_EQ(errors.size(), 1);
}

// --- proof 6: nextMapWithTile
// ----------------------------------------------------------------------------------

// The view: Google projection, zoom 5, centred at 30.0 E 60.0 N. The tile there (top-left rounded, legacy
// :466-472): x = floor((30+180)/360*32) = floor(18.67) = 18, y = floor(9.29) = 9.
geo::LocalCoordConverterPtr viewAt30E60N() {
    const geo::ProjectionPtr projection = maptype::test::googleProjectionSet()->zoom(5);
    return std::make_shared<geo::LocalCoordConverter>(
        core::kNoHash, geo::RectI{0, 0, 256, 256}, geo::RectI{0, 0, 256, 256},
        geo::DoubleRect{0, 0, 256, 256}, projection->lonLat2PixelPosFloat({30.0, 60.0}), projection, 1.0,
        geo::DoublePoint{0, 0});
}

void giveTile(const IMapTypePtr& map, geo::PointI xy, quint8 zoom) {
    const auto saved = map->tileStorage()->saveTile(xy, zoom, nullptr, storage::tileDateNow(), "image/png",
                                                    QByteArray("tile"), true);
    ASSERT_TRUE(saved.has_value());
}

struct StepRow {
    const char* label;
    int tiles;    // bit i: the i-th entry of the order (A B LAYER C D) has the tile
    int disabled; // bit i: the i-th entry is disabled
    int active;   // index in the order; -1 = null; -2 = a map that is not in the list
    int step;
    int expected; // index in the order; -1 = null
};

TEST(MapTypesMainListNext, StepsThroughTheOrderSkippingWhatHasNoTileWrappingAtBothEnds) {
    // Order (pnum): A=0 B=1 LAYER=2 C=3 D=4. A tile is given where the row says (at the tile of the view).
    constexpr int kAll = 0b11011; // A, B, C, D (not the layer)
    const std::vector<StepRow> rows = {
        {"+1", kAll, 0, 0, +1, 1},
        {"+1 skips the layer", kAll, 0, 1, +1, 3},
        {"+1 wraps at the end", kAll, 0, 4, +1, 0},
        {"-1 wraps at the start", kAll, 0, 0, -1, 4},
        {"-1 skips the layer", kAll, 0, 3, -1, 1},
        {"+3 from 3 goes to 0, not modulo 1 (LD-21, :456-461)", kAll, 0, 3, +3, 0},
        {"-3 from 1 goes to 4, not modulo 3", kAll, 0, 1, -3, 4},
        {"step 0 is null (:432-435)", kAll, 0, 1, 0, -1},
        {"a disabled map is skipped", kAll, 0b01000, 1, +1, 4},
        {"a map without the tile is skipped", 0b10001, 0, 0, +1, 4},
        {"the active map after a full circle", 0b00010, 0, 1, +1, 1},
        {"the active map is not in the list, +1", kAll, 0, -2, +1, 0},
        {"the active map is not in the list, -1", kAll, 0, -2, -1, 4},
        {"a layer as the active map", kAll, 0, 2, +1, 3},
        {"no map has the tile", 0, 0, 0, +1, -1},
    };
    const std::vector<QByteArray> order = {kMapA, kMapB, kLayer, kMapC, kMapD};
    for (const StepRow& row : rows) {
        SCOPED_TRACE(row.label);
        Env env;
        QStringList errors;
        const std::vector<std::pair<QByteArray, int>> pnums = {
            {kMapA, 10}, {kMapB, 20}, {kLayer, 30}, {kMapC, 40}, {kMapD, 50}};
        const auto ordered = Env::newList(env.makeSet(kFiveMaps));
        env.load(*ordered, localIni(pnums), byNumber(), errors);
        ASSERT_TRUE(errors.isEmpty());
        for (int i = 0; i < 5; ++i) {
            const IMapTypePtr map =
                ordered->fullMapsSet()->mapTypeByGuid(uuid(order[static_cast<std::size_t>(i)]));
            ASSERT_NE(map, nullptr);
            if ((row.tiles & (1 << i)) != 0) {
                giveTile(map, {18, 9}, 5);
            }
            if ((row.disabled & (1 << i)) != 0) {
                map->guiConfig()->setEnabled(false);
            }
        }
        IMapTypePtr active;
        if (row.active >= 0) {
            active = ordered->fullMapsSet()->mapTypeByGuid(uuid(order[static_cast<std::size_t>(row.active)]));
        } else if (row.active == -2) {
            active = maptype::test::fakeMap(uuid(kMapAbsent));
        }
        const IMapTypePtr found = ordered->nextMapWithTile(viewAt30E60N(), active, row.step);
        if (row.expected < 0) {
            EXPECT_EQ(found, nullptr);
        } else {
            ASSERT_NE(found, nullptr);
            EXPECT_EQ(found->guid(), uuid(order[static_cast<std::size_t>(row.expected)]));
        }
    }
}

TEST(MapTypesMainListNext, TheTileAskedIsTheTopLeftRoundedOneAtTheZoomOfTheView) {
    Env env;
    const auto list = Env::newList(env.makeSet(kFiveMaps));
    QStringList errors;
    const std::vector<std::pair<QByteArray, int>> pnums = {
        {kMapA, 10}, {kMapB, 20}, {kLayer, 30}, {kMapC, 40}, {kMapD, 50}};
    env.load(*list, localIni(pnums), byNumber(), errors);
    ASSERT_TRUE(errors.isEmpty());
    const auto map = [&](const QByteArray& g) { return list->fullMapsSet()->mapTypeByGuid(uuid(g)); };
    // Each map has a tile at a wrong place only: the nearest tile (19, 9), the ceiling (19, 10), the
    // neighbour zoom (18, 9, 4), the swapped coordinates (9, 18).
    giveTile(map(kMapA), {19, 9}, 5);
    giveTile(map(kMapB), {19, 10}, 5);
    giveTile(map(kMapC), {18, 9}, 4);
    giveTile(map(kMapD), {9, 18}, 5);
    EXPECT_EQ(list->nextMapWithTile(viewAt30E60N(), maptype::test::fakeMap(uuid(kMapAbsent)), +1), nullptr);
    // the right place, on the third map
    giveTile(map(kMapC), {18, 9}, 5);
    const IMapTypePtr found =
        list->nextMapWithTile(viewAt30E60N(), maptype::test::fakeMap(uuid(kMapAbsent)), +1);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->guid(), uuid(kMapC));
}

// --- proof 8: destruction (run it under --tsan)
// --------------------------------------------------------------------

// Proves: no hang and a quiet TSan with 514 live download subsystems, and that the list owns the maps (the
// weak pointer expires, no GC subscription survives). It does not prove an interleaving: no thread of the
// test races the destruction.
TEST(MapTypesMainListDestruction, TheCorpusListIsDestroyedAfterTheAppClosingStopRequest) {
    CG_SKIP_IF_NO_CORPUS();
    Env env;
    const QString zmpRoot = maptype::test::fixtureDir() + u"/zmp/sas.maps"_s;
    const auto set = env.stockSet(zmpFileNames(zmpRoot), zmpRoot);
    auto list = Env::newList(set);
    QStringList errors;
    env.load(*list, maptype::test::iniFrom({}), maptype::test::iniFrom({}), errors);
    ASSERT_TRUE(errors.isEmpty());
    const std::weak_ptr<IMapType> weak = list->fullMapsSet()->item(0);
    EXPECT_FALSE(weak.expired());
    env.appClosing.request_stop();
    list.reset();
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(env.gc.subscriberCount(), 0u);
}

TEST(MapTypesMainListDestruction, FiveRoundsOfLoadAndDestroyWithTwentyMapsAreClean) {
    CG_SKIP_IF_NO_CORPUS();
    Env env;
    const QString zmpRoot = maptype::test::fixtureDir() + u"/zmp/sas.maps"_s;
    QStringList names = zmpFileNames(zmpRoot);
    ASSERT_GE(names.size(), 20);
    names = names.mid(0, 20);
    const auto set = env.stockSet(names, zmpRoot);
    for (int round = 0; round < 5; ++round) {
        SCOPED_TRACE(round);
        std::stop_source closing;
        auto list = Env::newList(set);
        QStringList errors;
        env.load(*list, maptype::test::iniFrom({}), maptype::test::iniFrom({}), errors, closing.get_token());
        ASSERT_TRUE(errors.isEmpty());
        EXPECT_EQ(list->fullMapsSet()->count(), static_cast<int>(set->guids().size()));
        if (round % 2 == 0) {
            closing.request_stop(); // an even round fires the stop first, an odd one destroys without it
        }
        list.reset();
        EXPECT_EQ(env.gc.subscriberCount(), 0u);
    }
}

} // namespace
