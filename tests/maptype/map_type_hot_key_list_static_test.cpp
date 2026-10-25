// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-030 (prerequisite slice, held open). Ports MapType/u_MapTypeHotKeyListStatic.pas
// and i_MapTypeHotKeyListStatic.pas; spec maptype.md section 2.11.5 (hot-key part). The stock
// DefHotKey values come from tests/fixtures/zmp/sas.maps/**/params.txt (the 514 stock ZMPs).
#include "cg/core/config_provider_ini.h"
#include "cg/core/language_manager.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/map_type_hot_key_list_static.h"
#include "cg/maptype/map_type_set.h"

#include <QDirIterator>
#include <QTemporaryDir>
#include <QUuid>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <vector>

#include "fake_map_type.h"
#include "maptype_test_util.h"
#include "zmp_info_gui_stub.h"

using cg::maptype::IMapTypePtr;
using cg::maptype::MapTypeHotKeyListStatic;
using cg::maptype::MapTypeSetBuilderFactory;
using cg::maptype::test::buildSet;
using cg::maptype::test::fakeMap;
using cg::maptype::test::fakeMapWithGui;
using cg::maptype::test::guidN;
using cg::maptype::test::ZmpGuiStub;

namespace {

// A map whose GUI config carries the hot key and the enabled flag (the rest is the fixed stub).
struct Env {
    QTemporaryDir dir; // no .mo files: the stub's names are never looked up
    std::shared_ptr<cg::core::LanguageManager> languages =
        std::make_shared<cg::core::LanguageManager>(dir.path());

    IMapTypePtr map(const QUuid& guid, quint16 hotKey, bool enabled = true) const {
        auto zmp = std::make_shared<ZmpGuiStub>();
        zmp->key = hotKey;
        zmp->on = enabled;
        return fakeMapWithGui(guid, std::make_shared<cg::maptype::MapTypeGuiConfig>(languages, zmp));
    }
};

// Every stock params.txt in fixture order, as (GUID, DefHotKey). The set test iterates the same way.
struct StockEntry {
    QUuid guid;
    quint16 defHotKey;
};

std::vector<StockEntry> stockEntries() {
    std::vector<StockEntry> out;
    QDirIterator it(cg::maptype::test::fixtureDir() + QStringLiteral("/zmp/sas.maps"),
                    {QStringLiteral("params.txt")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto ini = cg::core::ConfigProviderIni::open(it.next());
        if (!ini) {
            ADD_FAILURE() << "unreadable params.txt";
            continue;
        }
        const auto params = (*ini)->subItem(u"PARAMS");
        out.push_back(
            {params->readUuid(u"GUID", QUuid()), static_cast<quint16>(params->readInt(u"DefHotKey", 0))});
    }
    return out;
}

// Each non-zero stock hot key resolves to the first enabled map (here all enabled, as the stock
// maps have no Enabled key) in ZMP order; the expected map is found by a plain scan of the list
// the test built, not by the class under test.
TEST(MapTypeHotKeyListStatic, EveryStockHotKeyResolvesToTheFirstEnabledMapInZmpOrder) {
    CG_SKIP_IF_NO_CORPUS();
    const Env env;
    const std::vector<StockEntry> entries = stockEntries();
    ASSERT_EQ(entries.size(), 514u);
    std::vector<IMapTypePtr> maps;
    for (const StockEntry& e : entries) {
        maps.push_back(env.map(e.guid, e.defHotKey));
    }
    const MapTypeHotKeyListStatic list(buildSet(maps));

    int checked = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const quint16 key = entries[i].defHotKey;
        if (key == 0) {
            continue;
        }
        IMapTypePtr expected;
        for (std::size_t j = 0; j < entries.size() && expected == nullptr; ++j) {
            if (entries[j].defHotKey == key) {
                expected = maps[j];
            }
        }
        EXPECT_EQ(list.mapTypeByHotKey(key), expected) << "key " << key << " at entry " << i;
        ++checked;
    }
    EXPECT_EQ(checked, 19); // the 19 stock maps with a non-zero DefHotKey
    EXPECT_EQ(list.mapTypeByHotKey(0), nullptr);
}

// Disabled maps are not listed, so a later enabled map with the same key wins; key 0 is never listed;
// the first of several enabled maps with one key keeps it.
TEST(MapTypeHotKeyListStatic, FirstEnabledMapWinsAndDisabledOrZeroKeyMapsAreNotListed) {
    const Env env;
    const auto disabledA = env.map(guidN(1), 77, false);
    const auto enabledB = env.map(guidN(2), 77);
    const auto zeroKeyC = env.map(guidN(3), 0);
    const auto disabledD = env.map(guidN(4), 5, false);
    const auto enabledE = env.map(guidN(5), 77);
    const MapTypeHotKeyListStatic list(buildSet({disabledA, enabledB, zeroKeyC, disabledD, enabledE}));

    EXPECT_EQ(list.mapTypeByHotKey(77), enabledB);
    EXPECT_EQ(list.mapTypeByHotKey(5), nullptr);
    EXPECT_EQ(list.mapTypeByHotKey(0), nullptr);
    EXPECT_EQ(list.mapTypeByHotKey(78), nullptr);
}

// Legacy reads AMapsSet.Count on a nil set (access violation, spec maptype.md section 2.11.5 "a null
// mapsSet is legal"); the port builds an empty list. Deviation row in docs/parity/deviations.md.
TEST(MapTypeHotKeyListStatic, NullSetIsAnEmptyList) {
    const MapTypeHotKeyListStatic list(nullptr);
    EXPECT_EQ(list.mapTypeByHotKey(77), nullptr);
    EXPECT_EQ(list.mapTypeByHotKey(0), nullptr);
}

// A set built with allowNil may hold a null map, and any map may have no GUI config: legacy
// dereferences both (access violation at u_MapTypeHotKeyListStatic.pas:66-67). The port throws
// std::logic_error. The list is then not built at all.
TEST(MapTypeHotKeyListStatic, NullMapOrNullGuiConfigInTheSetThrows) {
    const Env env;
    auto withNullMap = MapTypeSetBuilderFactory().build(true);
    withNullMap->add(env.map(guidN(1), 77));
    withNullMap->add(nullptr);
    EXPECT_THROW(MapTypeHotKeyListStatic(withNullMap->makeAndClear()), std::logic_error);

    EXPECT_THROW(MapTypeHotKeyListStatic(buildSet({fakeMap(guidN(2))})), std::logic_error);
}

} // namespace
