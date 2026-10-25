// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-002. Ports u_MapAbilitiesConfig.pas, u_MapAbilitiesConfigStatic.pas;
// docs/specs/maptype.md §2.7, §2.7.1, §7.2 item 6.
#include "cg/maptype/map_abilities_config.h"

#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <memory>

#include "maptype_test_util.h"

using cg::maptype::MapAbilitiesConfig;
using cg::maptype::MapAbilitiesConfigStatic;
using cg::maptype::test::iniFrom;
using namespace Qt::StringLiterals;

namespace {

std::shared_ptr<const MapAbilitiesConfigStatic> defaults(bool show, bool download) {
    return std::make_shared<const MapAbilitiesConfigStatic>(MapAbilitiesConfigStatic{show, download});
}

TEST(MapAbilitiesConfig, UnchangedElementWritesNoKeyAndDeletesStaleOnes) {
    // §7.2 item 6: constructed from the ZMP values and saved unchanged -> nothing in the section.
    const auto root = iniFrom("[{G}]\r\nCanShowOnSmMap=0\r\nUseDwn=0\r\nKeep=1\r\n");
    MapAbilitiesConfig c(defaults(true, true));
    c.writeConfig(root->orCreateSubItem(u"{G}"));
    EXPECT_EQ(root->subItem(u"{G}")->valueNames(), QStringList{QStringLiteral("Keep")});
}

TEST(MapAbilitiesConfig, ChangedValuesAreWrittenAndReadBack) {
    const auto root = iniFrom({});
    MapAbilitiesConfig c(defaults(true, true));
    c.setIsShowOnSmMap(false);
    c.setUseDownload(false);
    c.writeConfig(root->orCreateSubItem(u"S"));
    EXPECT_EQ(root->subItem(u"S")->valueNames(),
              (QStringList{u"CanShowOnSmMap"_s, u"UseDwn"_s})); // legacy order
    EXPECT_FALSE(root->subItem(u"S")->readBool(u"CanShowOnSmMap", true));
    EXPECT_FALSE(root->subItem(u"S")->readBool(u"UseDwn", true));

    MapAbilitiesConfig back(defaults(true, true));
    back.readConfig(root->subItem(u"S"));
    EXPECT_FALSE(back.isShowOnSmMap());
    EXPECT_FALSE(back.useDownload());
}

TEST(MapAbilitiesConfig, ZmpThatForbidsDownloadCannotBeSwitchedOn) {
    // SetUseDownload stores `Def.UseDownload and AValue`, also when the value comes from Maps.ini.
    MapAbilitiesConfig c(defaults(true, false));
    c.setUseDownload(true);
    EXPECT_FALSE(c.useDownload());
    c.readConfig(iniFrom("[S]\r\nUseDwn=1\r\n")->subItem(u"S"));
    EXPECT_FALSE(c.useDownload());
}

TEST(MapAbilitiesConfig, GetStaticFollowsTheValuesBeforeSubscribersRun) {
    MapAbilitiesConfig c(defaults(true, true));
    EXPECT_EQ(*c.getStatic(), (MapAbilitiesConfigStatic{true, true}));
    std::shared_ptr<const MapAbilitiesConfigStatic> seen;
    const auto sub = c.changed().subscribe([&] { seen = c.getStatic(); });
    c.setIsShowOnSmMap(false);
    ASSERT_NE(seen, nullptr);
    EXPECT_EQ(*seen, (MapAbilitiesConfigStatic{false, true}));
}

TEST(MapAbilitiesConfig, SetterNotifiesOnlyOnChangeButReadAlwaysDoes) {
    MapAbilitiesConfig c(defaults(true, true));
    int n = 0;
    const auto sub = c.changed().subscribe([&] { ++n; });
    c.setIsShowOnSmMap(true);
    c.setUseDownload(true);
    EXPECT_EQ(n, 0);
    c.readConfig(iniFrom("[S]\r\n")->subItem(u"S"));
    EXPECT_EQ(n, 1);
    c.readConfig(nullptr);
    EXPECT_EQ(n, 1);
}

} // namespace
