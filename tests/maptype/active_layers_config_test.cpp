// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// T-0145: ActiveLayersConfig (u_ActiveLayersConfig.pas). Expectations are literals taken from the Pascal.
#include "cg/maptype/active_layers_config.h"

#include <QByteArray>
#include <QUuid>

#include <gtest/gtest.h>

#include <memory>

#include "maptype_test_util.h"

using namespace cg::maptype;
using cg::core::GuidSetStatic;
using cg::maptype::test::iniFrom;

namespace {

const QUuid kG1(u"{11111111-0000-0000-0000-000000000001}");
const QUuid kG2(u"{22222222-0000-0000-0000-000000000002}");
const QUuid kG3(u"{33333333-0000-0000-0000-000000000003}");
const QUuid kG4(u"{44444444-0000-0000-0000-000000000004}");
const QUuid kG5(u"{55555555-0000-0000-0000-000000000005}");
const QUuid kG6(u"{66666666-0000-0000-0000-000000000006}");

struct Counted {
    ActiveLayersConfig config;
    int notifications = 0;
    cg::core::Subscription sub;
    Counted() : sub(config.changed().subscribe([this] { ++notifications; })) {}
};

bool has(const cg::core::IGuidSetStaticPtr& set, const QUuid& g) {
    return set && set->isExists(g);
}

} // namespace

TEST(ActiveLayersConfig, ReadSelectsPrefixedKeysOnlyAndAccumulates) {
    Counted c;
    EXPECT_EQ(c.config.layerGuids(), nullptr);
    const QByteArray text = "[S]\r\n"
                            "Layer0={11111111-0000-0000-0000-000000000001}\r\n"
                            "Layer1={22222222-0000-0000-0000-000000000002}\r\n"
                            "layer7={33333333-0000-0000-0000-000000000003}\r\n"
                            "LayerX=garbage\r\n"
                            "Layer3={00000000-0000-0000-0000-000000000000}\r\n"
                            "LayerFoo={44444444-0000-0000-0000-000000000004}\r\n"
                            "Other={55555555-0000-0000-0000-000000000005}\r\n"
                            "Lay={55555555-0000-0000-0000-000000000005}\r\n"
                            "[T]\r\n"
                            "Layer0={66666666-0000-0000-0000-000000000006}\r\n"
                            "Layer1={11111111-0000-0000-0000-000000000001}\r\n";
    auto ini = iniFrom(text);
    c.config.readConfig(ini->subItem(u"S"));
    auto set = c.config.layerGuids();
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->count(), 4);
    EXPECT_TRUE(has(set, kG1));
    EXPECT_TRUE(has(set, kG2));
    EXPECT_TRUE(has(set, kG3));
    EXPECT_TRUE(has(set, kG4)); // L row: any key with the prefix is read
    EXPECT_FALSE(has(set, kG5));
    EXPECT_EQ(c.notifications, 1); // the whole read is one batch

    c.config.readConfig(ini->subItem(u"T")); // never cleared: accumulates, the duplicate is not added twice
    set = c.config.layerGuids();
    EXPECT_EQ(set->count(), 5);
    EXPECT_TRUE(has(set, kG6));
    EXPECT_EQ(c.notifications, 2);

    c.config.readConfig(nullptr); // null provider: nothing
    EXPECT_EQ(c.config.layerGuids()->count(), 5);
    EXPECT_EQ(c.notifications, 2);
}

TEST(ActiveLayersConfig, WriteDeletesEveryPrefixedKeyThenWritesInSetOrder) {
    ActiveLayersConfig config;
    config.setLayerGuids(GuidSetStatic::createAndSort({kG3, kG1, kG2})); // given unsorted
    auto ini = iniFrom("[S]\r\nKeep=1\r\nLayer5={66666666-0000-0000-0000-000000000006}\r\n"
                       "LayerFoo=x\r\nlayer9=y\r\nLay=z\r\n");
    config.writeConfig(ini->orCreateSubItem(u"S"));
    EXPECT_EQ(ini->file().toBytes().toStdString(), "[S]\r\nKeep=1\r\nLay=z\r\n"
                                                   "Layer0={11111111-0000-0000-0000-000000000001}\r\n"
                                                   "Layer1={22222222-0000-0000-0000-000000000002}\r\n"
                                                   "Layer2={33333333-0000-0000-0000-000000000003}\r\n");
}

TEST(ActiveLayersConfig, WriteOfANullSetOnlyDeletes) {
    ActiveLayersConfig config;
    auto ini = iniFrom("[S]\r\nKeep=1\r\nLayer0={11111111-0000-0000-0000-000000000001}\r\nLayerFoo=x\r\n");
    config.writeConfig(ini->orCreateSubItem(u"S"));
    EXPECT_EQ(ini->file().toBytes().toStdString(), "[S]\r\nKeep=1\r\n");
}

// D row of T-0145: a null writer returns (legacy: access violation).
TEST(ActiveLayersConfig, NullWriterIsIgnored) {
    ActiveLayersConfig config;
    config.setLayerGuids(GuidSetStatic::createAndSort({kG1}));
    config.writeConfig(nullptr);
    EXPECT_EQ(config.layerGuids()->count(), 1);
}

TEST(ActiveLayersConfig, NotificationCounts) {
    Counted c;
    c.config.setLayerGuids(GuidSetStatic::createAndSort({kG1}));
    EXPECT_EQ(c.notifications, 1);

    c.config.invertLayerSelectionByGuid(kG2);
    EXPECT_EQ(c.notifications, 2);
    EXPECT_TRUE(has(c.config.layerGuids(), kG2));
    c.config.invertLayerSelectionByGuid(kG2);
    EXPECT_EQ(c.notifications, 3);
    ASSERT_EQ(c.config.layerGuids()->count(), 1); // back to the original
    EXPECT_EQ(c.config.layerGuids()->item(0), kG1);

    c.config.selectLayerByGuid(kG1); // exists: no change
    EXPECT_EQ(c.notifications, 3);
    c.config.unSelectLayerByGuid(kG3); // missing: no change
    EXPECT_EQ(c.notifications, 3);

    c.config.selectLayerByGuid(kG3);
    EXPECT_EQ(c.notifications, 4);
    EXPECT_TRUE(has(c.config.layerGuids(), kG3));
    c.config.unSelectLayerByGuid(kG3);
    EXPECT_EQ(c.notifications, 5);
    EXPECT_FALSE(has(c.config.layerGuids(), kG3));

    const auto same = c.config.layerGuids();
    c.config.setLayerGuids(same); // same pointer: still notifies
    EXPECT_EQ(c.notifications, 6);
    c.config.setLayerGuids(nullptr);
    EXPECT_EQ(c.config.layerGuids(), nullptr);
    EXPECT_EQ(c.notifications, 7);
}

TEST(ActiveLayersConfig, InvertAndSelectOnAnUnsetElementCreateTheSet) {
    Counted a;
    a.config.selectLayerByGuid(kG4);
    EXPECT_EQ(a.notifications, 1);
    EXPECT_TRUE(has(a.config.layerGuids(), kG4));
    Counted b;
    b.config.invertLayerSelectionByGuid(kG5);
    EXPECT_TRUE(has(b.config.layerGuids(), kG5));
    Counted d;
    d.config.unSelectLayerByGuid(kG5); // null set: nothing
    EXPECT_EQ(d.notifications, 0);
    EXPECT_EQ(d.config.layerGuids(), nullptr);
}
