// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MapTypeGUIConfigList.pas and i_MapTypeGUIConfigList.pas: the three orders (the two stable sorts
// of u_SortFunc.pas, read not ported), the submenu key of ByMapName, the [MapsList] SortOrder key with the
// DV-11 fallback, the language-switch bracket and the snapshots. Spec maptype.md 2.11.5, 4.1, 7.2 (OQ-10),
// DV-11 (ruling OQ-12). Corpus: tests/fixtures/zmp/sas.maps (514 stock ZMPs) and the stock Maps.ini.
#include "cg/core/content_type.h"
#include "cg/core/ini_file.h"
#include "cg/core/language_manager.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/map_type_gui_config_list.h"
#include "cg/maptype/zmp_config.h"
#include "cg/maptype/zmp_file_names_iterator_factory.h"
#include "cg/maptype/zmp_info_set.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>

#include <gtest/gtest.h>

#include <algorithm>
#include <barrier>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "fake_map_type.h"
#include "maptype_test_util.h"
#include "zmp_info_gui_stub.h"

using namespace Qt::StringLiterals;
using cg::core::ContentTypeRegistry;
using cg::core::IniFile;
using cg::core::LanguageManager;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::ProjectionSetFactory;
using cg::maptype::IMapTypePtr;
using cg::maptype::IMapTypeSetPtr;
using cg::maptype::IZmpInfoPtr;
using cg::maptype::MapGuidListPtr;
using cg::maptype::MapTypeGuiConfig;
using cg::maptype::MapTypeGuiConfigList;
using cg::maptype::MapTypeGuiConfigListSortOrder;
using cg::maptype::MapTypeSetBuilderFactory;
using cg::maptype::ZmpConfig;
using cg::maptype::zmpFileNames;
using cg::maptype::ZmpInfoSet;
using cg::maptype::test::buildSet;
using cg::maptype::test::ensureGuiApp;
using cg::maptype::test::FakeMapType;
using cg::maptype::test::fixtureDir;
using cg::maptype::test::iniFrom;
using cg::maptype::test::ZmpGuiStub;

namespace {

constexpr auto kByMapNumber = MapTypeGuiConfigListSortOrder::ByMapNumber;
constexpr auto kByMapName = MapTypeGuiConfigListSortOrder::ByMapName;
constexpr auto kByZmpName = MapTypeGuiConfigListSortOrder::ByZmpName;

QString zmpDir() {
    return fixtureDir() + u"/zmp"_s;
}

// The stock languages: en is index 0, the six .mo codes follow in name order (es fa fr ru tr uk).
// A fresh directory with these empty files is the whole language set the tests need.
struct StockEnv {
    QTemporaryDir langDir;
    std::shared_ptr<LanguageManager> languages;
    ProjectionSetFactory projections{HashSeed{}, std::make_shared<const DatumFactory>(HashSeed{})};
    ZmpConfig zmpConfig;
    cg::vector::AppearanceOfMarkFactory appearanceFactory;
    QString root = zmpDir() + u"/sas.maps"_s;
    std::unique_ptr<ZmpInfoSet> set;
    QStringList loadErrors;

    // The first `count` stock ZMPs as real ZmpInfo objects, in load order (count < 0: all 514).
    explicit StockEnv(int count = -1) {
        ensureGuiApp(); // the one stock 24.bmp that is really a PNG gets the default icon, which draws text
        for (const char* code : {"es", "fa", "fr", "ru", "tr", "uk"}) {
            QFile f(langDir.filePath(QString::fromLatin1(code) + u".mo"_s));
            EXPECT_TRUE(f.open(QIODevice::WriteOnly));
        }
        languages = std::make_shared<LanguageManager>(langDir.path());
        QStringList names = zmpFileNames(root);
        if (count >= 0) {
            names = names.mid(0, count);
        }
        set = std::make_unique<ZmpInfoSet>(
            zmpConfig, *languages, projections, ContentTypeRegistry::standard(), appearanceFactory, nullptr,
            root, names, [this](const QString& message) { loadErrors.append(message); });
    }
};

// A map of the set with the real ZMP `zmp` and the given GUI config (as the main list builds it).
IMapTypePtr mapOf(const IZmpInfoPtr& zmp, std::shared_ptr<MapTypeGuiConfig> gui) {
    return std::make_shared<FakeMapType>(zmp->guid(), std::move(gui), zmp);
}

std::vector<QUuid> listOf(const MapGuidListPtr& list) {
    return list ? *list : std::vector<QUuid>{};
}

// One hand-made map: the name and the submenu of its GUI config, and its pnum. Names and submenus use
// the legacy submenu key rules; the Cyrillic pair is one key up to case (u_MapTypeGUIConfigList.pas:167-183).
struct Item {
    const char* name;
    const char* subMenu;
    int pnum;
};

// Input order. Expected orders are derived from the keys (the Pascal text, not the class):
//   ByMapName keys (lower case, #01 and #02 as the legacy separators):
//     2 "\x01" "a\x02mid"  5 "\x02last"  3 "a\x01\x02beta"  9 "a\x01" "b\x01" "c\x02y"  8 "a\x01" "b\x02a"
//     1 "a\x01" "b\x02zed"  6 "a\x02gamma"  4 "alpha"  0 and 7 "город" (equal ignoring case: input order
//     decides)
//   Items 8 and 9 test the folders-first rule: the file in folder A\B sorts after the file in its subfolder
//   A\B\C. ByMapNumber: pnum 1 (1), 2 (5), 3 (3), 5 (0 then 7: equal pnum keeps input order), 6 (6), 7 (4), 8
//   (2), 10 (8), 11 (9)
const std::vector<Item> kItems = {
    {"город", "", 5},     // 0
    {"Zed", "A\\B", 1},   // 1: folder "A\B" -> key "A\x01" "B\x02" "zed"
    {"Mid", "\\A", 8},    // 2: folder "\A"  -> key "\x01" "A\x02" "mid"
    {"Beta", "A\\", 3},   // 3: folder "A\"  -> key "A\x01\x02" "beta"
    {"alpha", "", 7},     // 4: no folder    -> key "alpha"
    {"last", "\\", 2},    // 5: folder "\"   -> key "\x02" "last"
    {"Gamma", "A", 6},    // 6: folder "A"   -> key "A\x02" "gamma"
    {"Город", "", 5},     // 7: the capital twin of 0
    {"a", "A\\B", 10},    // 8: file in folder A\B
    {"y", "A\\B\\C", 11}, // 9: file in subfolder A\B\C
};

const std::vector<int> kByNumberOrder = {1, 5, 3, 0, 7, 6, 4, 2, 8, 9};
const std::vector<int> kByNameOrder = {2, 5, 3, 9, 8, 1, 6, 4, 0, 7};
const std::vector<int> kZmpOrder = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

// The hand-made maps (kItems) on the first stock ZMPs: the GUI config carries the item's name, submenu
// and pnum; the map's GUID is that of its real ZMP.
struct HandMade {
    StockEnv env{static_cast<int>(kItems.size())};
    std::vector<IZmpInfoPtr> zmps;
    std::vector<std::shared_ptr<MapTypeGuiConfig>> guis;
    std::vector<IMapTypePtr> maps;
    IMapTypeSetPtr set;

    HandMade() {
        const auto& guids = env.set->guids();
        for (std::size_t i = 0; i < kItems.size(); ++i) {
            IZmpInfoPtr zmp = env.set->zmpByGuid(guids[i]);
            auto stub = std::make_shared<ZmpGuiStub>();
            stub->nameEn = QString::fromUtf8(kItems[i].name);
            stub->subMenuEn = QString::fromUtf8(kItems[i].subMenu);
            stub->pnum = kItems[i].pnum;
            auto gui = std::make_shared<MapTypeGuiConfig>(env.languages, stub);
            zmps.push_back(zmp);
            guis.push_back(gui);
            maps.push_back(mapOf(zmp, gui));
        }
        set = buildSet(maps);
    }

    // The GUIDs of the maps at the given item indices, in that order.
    std::vector<QUuid> guidsAt(const std::vector<int>& indices) const {
        std::vector<QUuid> out;
        for (const int i : indices) {
            out.push_back(zmps[static_cast<std::size_t>(i)]->guid());
        }
        return out;
    }

    std::shared_ptr<MapTypeGuiConfigList> list() const {
        return std::make_shared<MapTypeGuiConfigList>(env.languages, set);
    }
};

// Warnings are process-wide: the capture keeps them for the scope of one test and restores the handler.
std::vector<QString>& warnings() {
    static std::vector<QString> messages;
    return messages;
}

void collectWarning(QtMsgType type, const QMessageLogContext&, const QString& message) {
    if (type == QtWarningMsg) {
        warnings().push_back(message);
    }
}

struct WarningCapture {
    QtMessageHandler previous;
    WarningCapture() : previous(qInstallMessageHandler(collectWarning)) { warnings().clear(); }
    ~WarningCapture() { qInstallMessageHandler(previous); }
};

// The legacy text of u_MapTypeGUIConfigList.pas:191 as logged by the port.
int sortOrderWarnings() {
    return static_cast<int>(std::count_if(warnings().begin(), warnings().end(), [](const QString& m) {
        return m.startsWith(u"Unexpected SortOrder type:"_s);
    }));
}

// --- the corpus -----------------------------------------------------------------------------------

QString sectionOf(const QUuid& guid) {
    return guid.toString(QUuid::WithBraces).toUpper();
}

// The 514 stock maps. Each GUI config is built from the ZMP's own GUI record and then read from its Maps.ini
// section, as the main list does at start-up: before the list is built, so that the reads cause no list
// notification.
IMapTypeSetPtr stockMaps(const StockEnv& env, const cg::core::IConfigProviderPtr& mapsIni) {
    std::vector<IMapTypePtr> maps;
    for (const QUuid& guid : env.set->guids()) {
        IZmpInfoPtr zmp = env.set->zmpByGuid(guid);
        auto gui = std::make_shared<MapTypeGuiConfig>(env.languages, zmp->gui());
        gui->readConfig(mapsIni->subItem(sectionOf(guid)));
        maps.push_back(mapOf(zmp, gui));
    }
    return buildSet(maps);
}

// Stock order by pnum as the legacy-written Maps.ini says it, ties by load order.
std::vector<QUuid> stockPnumOrder(const StockEnv& env, const IniFile& mapsIni) {
    std::vector<std::pair<int, std::size_t>> keyed;
    const auto& guids = env.set->guids();
    for (std::size_t i = 0; i < guids.size(); ++i) {
        keyed.emplace_back(mapsIni.readInt(sectionOf(guids[i]), u"pnum", -1), i);
    }
    std::sort(keyed.begin(), keyed.end());
    std::vector<QUuid> out;
    for (const auto& [pnum, index] : keyed) {
        out.push_back(guids[index]);
    }
    return out;
}

std::shared_ptr<cg::core::ConfigProviderIni> openMapsIni() {
    auto opened = cg::core::ConfigProviderIni::open(zmpDir() + u"/Maps.ini"_s);
    EXPECT_TRUE(opened.has_value());
    return opened.has_value() ? opened.value() : nullptr;
}

// --- the orders on the corpus ------------------------------------------------------------------------

TEST(MapTypeGuiConfigListCorpus, ByZmpNameIsTheZmpLoadOrderOfAllStockMaps) {
    CG_SKIP_IF_NO_CORPUS();
    const StockEnv env;
    ASSERT_TRUE(env.loadErrors.isEmpty());
    ASSERT_EQ(env.set->guids().size(), 514u);
    const auto mapsIni = openMapsIni();
    ASSERT_NE(mapsIni, nullptr);
    const auto list = std::make_shared<MapTypeGuiConfigList>(env.languages, stockMaps(env, mapsIni));
    list->setSortOrder(kByZmpName);
    EXPECT_EQ(listOf(list->orderedMapGuidList()), env.set->guids());
}

TEST(MapTypeGuiConfigListCorpus, ByMapNumberIsTheStableOrderOfTheStockPnum) {
    CG_SKIP_IF_NO_CORPUS();
    const StockEnv env;
    const auto mapsIni = openMapsIni();
    ASSERT_NE(mapsIni, nullptr);
    const auto list = std::make_shared<MapTypeGuiConfigList>(env.languages, stockMaps(env, mapsIni));
    // The default is ByMapName; the test selects ByMapNumber through the setter.
    EXPECT_EQ(list->sortOrder(), kByMapName);
    list->setSortOrder(kByMapNumber);
    const auto iniFile = IniFile::fromFile(zmpDir() + u"/Maps.ini"_s);
    ASSERT_TRUE(iniFile.has_value());
    const std::vector<QUuid> expected = stockPnumOrder(env, iniFile.value());
    ASSERT_EQ(expected.size(), 514u);
    EXPECT_EQ(listOf(list->orderedMapGuidList()), expected);
}

// OQ-10: the stock pnum values are the oracle for ByMapName (MapTypesMainList renumbers pnum 1..N in list
// order, u_MapTypesMainList.pas:400-405). On the stock corpus the two orders agree at all 514 positions; a
// mismatch fails.
TEST(MapTypeGuiConfigListCorpus, ByMapNameIsThePermutationOfTheCorpusAndTheStockPnumOrder) {
    CG_SKIP_IF_NO_CORPUS();
    const StockEnv env;
    const auto mapsIni = openMapsIni();
    ASSERT_NE(mapsIni, nullptr);
    const auto list = std::make_shared<MapTypeGuiConfigList>(env.languages, stockMaps(env, mapsIni));
    const std::vector<QUuid> byName = listOf(list->orderedMapGuidList());
    ASSERT_EQ(byName.size(), 514u);
    EXPECT_EQ(QSet<QUuid>(byName.begin(), byName.end()),
              QSet<QUuid>(env.set->guids().begin(), env.set->guids().end()));

    const auto iniFile = IniFile::fromFile(zmpDir() + u"/Maps.ini"_s);
    ASSERT_TRUE(iniFile.has_value());
    EXPECT_EQ(byName, stockPnumOrder(env, iniFile.value()));
}
// One list notification per language switch, although every stock map's name element fires in it.
TEST(MapTypeGuiConfigListCorpus, ALanguageSwitchEmitsExactlyOneListNotification) {
    CG_SKIP_IF_NO_CORPUS();
    const StockEnv env;
    const auto mapsIni = openMapsIni();
    ASSERT_NE(mapsIni, nullptr);
    const auto maps = stockMaps(env, mapsIni);
    const auto list = std::make_shared<MapTypeGuiConfigList>(env.languages, maps);
    int fired = 0;
    const auto subscription = list->changed().subscribe([&fired] { ++fired; });

    std::size_t namesChanged = 0;
    std::vector<QString> before;
    for (int i = 0; i < maps->count(); ++i) {
        before.push_back(maps->item(i)->guiConfig()->name()->value());
    }
    env.languages->setCurrentLanguageCode(u"ru"_s);
    for (int i = 0; i < maps->count(); ++i) {
        namesChanged +=
            maps->item(i)->guiConfig()->name()->value() != before[static_cast<std::size_t>(i)] ? 1 : 0;
    }
    EXPECT_GT(namesChanged, 0u) << "the switch must change some names, or the test proves nothing";
    EXPECT_EQ(fired, 1);

    env.languages->setCurrentLanguageCode(u"en"_s);
    EXPECT_EQ(fired, 2);
}

// --- the submenu key, the SortOrder key, DV-11 -------------------------------------------------------

TEST(MapTypeGuiConfigList, ByMapNameOrdersSubmenusAsFoldersAndIgnoresCase) {
    CG_SKIP_IF_NO_CORPUS();
    HandMade h;
    ASSERT_EQ(h.env.loadErrors.size(), 0);
    const auto list = h.list();
    list->setSortOrder(kByMapName);
    EXPECT_EQ(listOf(list->orderedMapGuidList()), h.guidsAt(kByNameOrder));
}

// SortOrder values of [MapsList]: 0, 1, 2 read as written; 7 and -1 fall back to by-name (DV-11, OQ-12) with
// one warning, and the value is written back unchanged. The stored value is the one read.
struct SortOrderCase {
    int stored;
    std::vector<int> expected;
    int warnings;
};

TEST(MapTypeGuiConfigList, SortOrderKeyOrdersTheList_AndAnOutOfRangeValueFallsBackToByName) {
    CG_SKIP_IF_NO_CORPUS();
    const std::vector<SortOrderCase> cases = {
        {0, kByNumberOrder, 0}, {1, kByNameOrder, 0},  {2, kZmpOrder, 0},
        {7, kByNameOrder, 1},   {-1, kByNameOrder, 1},
    };
    for (const SortOrderCase& c : cases) {
        SCOPED_TRACE(c.stored);
        WarningCapture capture;
        HandMade h;
        const auto list = h.list();
        auto root = iniFrom("[MapsList]\r\nSortOrder=" + QByteArray::number(c.stored) + "\r\n");
        list->readConfig(root->subItem(u"MapsList"_s));
        EXPECT_EQ(static_cast<int>(list->sortOrder()), c.stored);
        EXPECT_EQ(listOf(list->orderedMapGuidList()), h.guidsAt(c.expected));
        EXPECT_EQ(sortOrderWarnings(), c.warnings);

        list->writeConfig(root->orCreateSubItem(u"MapsList"_s));
        EXPECT_EQ(root->file().readInt(u"MapsList", u"SortOrder", -999), c.stored);
    }
}

// --- the language bracket, the snapshots, the children ----------------------------------------------

TEST(MapTypeGuiConfigList, ChildChangeRebuildsOnceAndSetSortOrderNotifiesOnlyOnAChange) {
    CG_SKIP_IF_NO_CORPUS();
    HandMade h;
    const auto list = h.list();
    int fired = 0;
    const auto subscription = list->changed().subscribe([&fired] { ++fired; });

    h.maps[4]->guiConfig()->name()->setValue(QString::fromUtf8("я")); // the key of item 4 now sorts last
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(listOf(list->orderedMapGuidList()), h.guidsAt({2, 5, 3, 9, 8, 1, 6, 0, 7, 4}));

    list->setSortOrder(kByMapName); // already set: no notification
    EXPECT_EQ(fired, 1);
    list->setSortOrder(kByZmpName);
    EXPECT_EQ(fired, 2);
    EXPECT_EQ(listOf(list->orderedMapGuidList()), h.guidsAt(kZmpOrder));
}

TEST(MapTypeGuiConfigList, HotKeyListIsRebuiltWithTheSnapshot) {
    CG_SKIP_IF_NO_CORPUS();
    HandMade h;
    const auto list = h.list();
    EXPECT_EQ(list->hotKeyList()->mapTypeByHotKey(77), nullptr);
    h.guis[3]->setHotKey(77);
    EXPECT_EQ(list->hotKeyList()->mapTypeByHotKey(77), h.maps[3]);
}

// The legacy set of uninitialised proxies is not listed (u_MapTypeGUIConfigList.pas:149-151).
TEST(MapTypeGuiConfigList, UninitializedProxyIsNotListed) {
    CG_SKIP_IF_NO_CORPUS();
    HandMade h;
    auto proxy = std::make_shared<FakeMapType>(h.zmps[1]->guid(), h.guis[1], h.zmps[1]);
    proxy->reset();
    const auto set = buildSet({h.maps[0], proxy, h.maps[2]});
    const auto list = std::make_shared<MapTypeGuiConfigList>(h.env.languages, set);
    list->setSortOrder(kByZmpName);
    EXPECT_EQ(listOf(list->orderedMapGuidList()), h.guidsAt({0, 2}));

    // When every map is an uninitialised proxy nothing is listed, and the list is null (legacy Result = nil).
    proxy->reset();
    const auto onlyProxy = std::make_shared<MapTypeGuiConfigList>(h.env.languages, buildSet({proxy}));
    EXPECT_EQ(onlyProxy->orderedMapGuidList(), nullptr);
}

// A null provider changes nothing; an empty section keeps the value and still notifies (legacy SetChanged).
TEST(MapTypeGuiConfigList, ReadConfigWithNoProviderChangesNothingAndAnEmptySectionKeepsTheValue) {
    CG_SKIP_IF_NO_CORPUS();
    HandMade h;
    const auto list = h.list();
    int fired = 0;
    const auto subscription = list->changed().subscribe([&fired] { ++fired; });

    list->readConfig(nullptr);
    list->writeConfig(nullptr);
    EXPECT_EQ(fired, 0);
    EXPECT_EQ(list->sortOrder(), kByMapName);

    auto root = iniFrom("[MapsList]\r\nx=1\r\n");
    list->readConfig(root->subItem(u"MapsList"_s));
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(list->sortOrder(), kByMapName);
}

// The list subscribes to both language notifiers and removes both when it dies (legacy Destroy, :110-123).
TEST(MapTypeGuiConfigList, DestructionRemovesTheLanguageSubscriptions) {
    CG_SKIP_IF_NO_CORPUS();
    HandMade h;
    const std::size_t beforeBefore = h.env.languages->beforeChanged().subscriberCount();
    const std::size_t beforeAfter = h.env.languages->afterChanged().subscriberCount();
    auto list = h.list();
    EXPECT_EQ(h.env.languages->beforeChanged().subscriberCount(), beforeBefore + 1);
    EXPECT_EQ(h.env.languages->afterChanged().subscriberCount(), beforeAfter + 1);
    list.reset();
    EXPECT_EQ(h.env.languages->beforeChanged().subscriberCount(), beforeBefore);
    EXPECT_EQ(h.env.languages->afterChanged().subscriberCount(), beforeAfter);
    h.env.languages->setCurrentLanguageCode(u"ru"_s); // no list left to notify
}

// --- nil inputs (D-rows, see the handoff) -----------------------------------------------------------

// Legacy reads AMapsSet.Count without a nil check (u_MapTypeGUIConfigList.pas:97): an access violation.
TEST(MapTypeGuiConfigList, NilMapSetGivesAnEmptyListNotAnAccessViolation) {
    CG_SKIP_IF_NO_CORPUS();
    HandMade h;
    const auto list = std::make_shared<MapTypeGuiConfigList>(h.env.languages, nullptr);
    EXPECT_EQ(list->orderedMapGuidList(), nullptr);
    EXPECT_EQ(list->hotKeyList()->mapTypeByHotKey(77), nullptr);
}

// A nil map, a nil GUI config, a listed map with a nil ZMP, or a nil language manager: legacy dereferences
// each one (:98-99, :197, :102). The port throws std::logic_error from the constructor.
TEST(MapTypeGuiConfigList, NilMapGuiConfigZmpOrLanguageManagerThrowsLogicError) {
    CG_SKIP_IF_NO_CORPUS();
    HandMade h;
    auto withNilMap = MapTypeSetBuilderFactory().build(true);
    withNilMap->add(h.maps[0]);
    withNilMap->add(nullptr);
    const IMapTypeSetPtr nilMapSet = withNilMap->makeAndClear();

    const auto noGui = std::make_shared<FakeMapType>(h.zmps[0]->guid(), nullptr, h.zmps[0]);
    const auto noZmp = std::make_shared<FakeMapType>(h.zmps[0]->guid(), h.guis[0], nullptr);

    EXPECT_THROW(std::make_shared<MapTypeGuiConfigList>(h.env.languages, nilMapSet), std::logic_error);
    EXPECT_THROW(std::make_shared<MapTypeGuiConfigList>(h.env.languages, buildSet({noGui})),
                 std::logic_error);
    EXPECT_THROW(std::make_shared<MapTypeGuiConfigList>(h.env.languages, buildSet({noZmp})),
                 std::logic_error);
    EXPECT_THROW(std::make_shared<MapTypeGuiConfigList>(nullptr, h.set), std::logic_error);
}

// --- thread safety (D-row for LD-15; run under --tsan by the gate) ----------------------------------

// Legacy reads the ordered and hot-key fields without the lock that the rebuild writes them under. The port
// publishes both as snapshots: a reader always gets a complete list while the writer rebuilds.
// Two threads on one list: one toggles the sort order (the list's own lock), one changes a child's name
// (the child's lock, then the list's batch token). Both rebuild under rebuildMutex_. A barrier starts every
// round of both threads together (T-0340: the earlier "the two calls overlapped" counter depended on timing
// and failed in loaded full runs; a sleep is no synchronisation, and the overlap itself cannot be forced
// from outside). At the end the published order must equal a fresh list built on the same set with the same
// sort order. Only ThreadSanitizer can fail the data-race side; the rebuild-order side fails on a stale
// publish.
TEST(MapTypeGuiConfigList, ConcurrentSortOrderAndChildChangesEndOnAFreshRebuild) {
    CG_SKIP_IF_NO_CORPUS();
    HandMade h;
    const auto list = h.list();
    constexpr int kRounds = 200;
    std::barrier roundStart(2);
    std::thread sorter([&] {
        for (int i = 0; i < kRounds; ++i) {
            roundStart.arrive_and_wait();
            list->setSortOrder(i % 2 != 0 ? kByMapName : kByZmpName);
        }
    });
    std::thread namer([&] {
        for (int i = 0; i < kRounds; ++i) {
            roundStart.arrive_and_wait();
            h.maps[4]->guiConfig()->name()->setValue(QString::number(i));
        }
    });
    sorter.join();
    namer.join();

    const auto fresh = std::make_shared<MapTypeGuiConfigList>(h.env.languages, h.set);
    fresh->setSortOrder(list->sortOrder());
    EXPECT_EQ(listOf(list->orderedMapGuidList()), listOf(fresh->orderedMapGuidList()));
}
} // namespace
