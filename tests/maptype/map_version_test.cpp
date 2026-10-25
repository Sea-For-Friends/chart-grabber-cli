// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-002. Ports u_MapVersionConfig.pas, u_MapVersionRequestConfig.pas,
// u_MapVersionFactoryChangeable.pas, u_MapVersionChangeable.pas, u_MapVersionRequestChangeable.pas;
// docs/specs/maptype.md §2.7 (Version / ShowOtherVersions), §2.9, §7.2 item 6; legacy-defects L-152.
#include "cg/maptype/map_version_changeable.h"
#include "cg/maptype/map_version_config.h"
#include "cg/maptype/map_version_factory_changeable.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/map_version_request_config.h"

#include <QByteArray>
#include <QString>

#include <gtest/gtest.h>

#include <memory>

#include "maptype_test_util.h"

using namespace cg::maptype;
using cg::maptype::test::iniFrom;
using namespace Qt::StringLiterals;

namespace {

// The section text written for `config` over a section that already holds `before`.
template <class Config> QByteArray writeOver(const Config& config, const QByteArray& before = {}) {
    auto ini = iniFrom("[S]\r\n" + before);
    config.writeConfig(ini->orCreateSubItem(u"S"));
    return ini->file().toBytes();
}

TEST(MapVersionRequestConfig, WritesVersionOnlyWhenItDiffersAndShowOtherVersionsAlways) {
    MapVersionRequestConfig untouched(u"v1"_s);
    EXPECT_EQ(writeOver(untouched, "Version=old\r\n"), "[S]\r\nShowOtherVersions=1\r\n");
    MapVersionRequestConfig changed(u"v1"_s);
    changed.setVersion(u"v2"_s);
    changed.setShowOtherVersions(false);
    EXPECT_EQ(writeOver(changed), "[S]\r\nVersion=v2\r\nShowOtherVersions=0\r\n");
}

TEST(MapVersionRequestConfig, ReadDefaultsToTrueAndAlwaysNotifiesForANonNullProvider) {
    MapVersionRequestConfig c(u"v1"_s);
    int n = 0;
    const auto sub = c.changed().subscribe([&] { ++n; });
    c.readConfig(iniFrom("[S]\r\n")->subItem(u"S"));
    EXPECT_EQ(n, 1);
    EXPECT_TRUE(c.showOtherVersions());
    EXPECT_EQ(c.version(), u"v1"_s);
    c.readConfig(nullptr);
    EXPECT_EQ(n, 1);
    c.readConfig(iniFrom("[S]\r\nVersion=v3\r\nShowOtherVersions=0\r\n")->subItem(u"S"));
    EXPECT_EQ(n, 2);
    EXPECT_EQ(c.getStatic()->version, u"v3"_s);
    EXPECT_FALSE(c.getStatic()->showOtherVersions);
}

TEST(MapVersionConfig, WritesOrDeletesVersionAgainstTheZmpDefault) {
    MapVersionConfig c(u"v1"_s);
    EXPECT_EQ(writeOver(c, "Version=x\r\n"), "[S]\r\n");
    c.readConfig(iniFrom("[S]\r\nVersion=v9\r\n")->subItem(u"S"));
    EXPECT_EQ(c.version(), u"v9"_s);
    EXPECT_EQ(writeOver(c), "[S]\r\nVersion=v9\r\n");
}

TEST(MapVersionRequestChangeable, RebuildsAndNotifiesOnEveryConfigChange) {
    auto config = std::make_shared<MapVersionRequestConfig>(u"v1"_s);
    auto factory =
        std::make_shared<MapVersionFactoryChangeable>(std::make_shared<cg::storage::MapVersionFactory>());
    MapVersionRequestChangeable c(config, factory);
    ASSERT_NE(c.getStatic(), nullptr);
    EXPECT_EQ(c.getStatic()->baseVersion->storeString(), u"v1"_s);
    EXPECT_TRUE(c.getStatic()->showOtherVersions);
    int n = 0;
    const auto sub = c.changed().subscribe([&] { ++n; });
    config->setVersion(u"v2"_s);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(c.getStatic()->baseVersion->storeString(), u"v2"_s);
    config->readConfig(iniFrom("[S]\r\n")->subItem(u"S")); // unchanged, still notifies
    EXPECT_EQ(n, 2);
}

TEST(MapVersionFactoryChangeable, ANonNullFactoryIsTheSameClassAndIgnored) {
    auto first = std::make_shared<cg::storage::MapVersionFactory>();
    MapVersionFactoryChangeable f(first);
    int n = 0;
    const auto sub = f.changed().subscribe([&] { ++n; });
    f.setFactory(std::make_shared<cg::storage::MapVersionFactory>());
    EXPECT_EQ(n, 0);
    EXPECT_EQ(f.getStatic(), first);
}

// Reached through the storage-side interface: nullptr replaces and notifies once.
TEST(MapVersionFactoryChangeable, SetFactoryThroughTheInternalInterface) {
    auto first = std::make_shared<cg::storage::MapVersionFactory>();
    MapVersionFactoryChangeable f(first);
    cg::storage::IMapVersionFactoryChangeableInternal& internal = f;
    int n = 0;
    const auto sub = f.changed().subscribe([&] { ++n; });
    internal.setFactory(std::make_shared<cg::storage::MapVersionFactory>());
    EXPECT_EQ(n, 0);
    EXPECT_EQ(f.getStatic(), first);
    internal.setFactory(nullptr);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(f.getStatic(), nullptr);
}

// L-152: the version is compared with the never-assigned static, so getStatic() stays null and
// every config change notifies.
TEST(MapVersionChangeable, StaticIsNeverAssignedAndEveryChangeNotifies) {
    auto config = std::make_shared<MapVersionConfig>(u"v1"_s);
    auto factory =
        std::make_shared<MapVersionFactoryChangeable>(std::make_shared<cg::storage::MapVersionFactory>());
    MapVersionChangeable c(config, factory);
    int n = 0;
    const auto sub = c.changed().subscribe([&] { ++n; });
    config->setVersion(u"v2"_s);
    config->setVersion(u"v2"_s); // no config change: no notification
    config->setVersion(u"v1"_s);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(c.getStatic(), nullptr);
}

} // namespace
