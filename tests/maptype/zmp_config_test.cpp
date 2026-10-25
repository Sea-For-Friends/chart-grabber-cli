// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-002. Ports i_ZmpConfig.pas, u_ZmpConfig.pas; docs/specs/maptype.md §2.6.9.
#include "cg/maptype/zmp_config.h"

#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include "maptype_test_util.h"

using cg::maptype::ZmpConfig;
using cg::maptype::test::iniFrom;
using namespace Qt::StringLiterals;

namespace {

TEST(ZmpConfig, DefaultsAreTheLegacyConstants) {
    const ZmpConfig c;
    EXPECT_EQ(c.maxConnectToServerCount(), 4U);
    EXPECT_TRUE(c.useMemCache());
    EXPECT_EQ(c.memCacheCapacity(), 100);
    EXPECT_EQ(c.memCacheTtl(), 60000U);
    EXPECT_EQ(c.memCacheClearStrategy(), 1);
}

TEST(ZmpConfig, SettersNotifyOnlyWhenTheValueDiffers) {
    ZmpConfig c;
    int n = 0;
    const auto sub = c.changed().subscribe([&] { ++n; });
    c.setMaxConnectToServerCount(4);
    c.setUseMemCache(true);
    c.setMemCacheCapacity(100);
    c.setMemCacheTtl(60000);
    c.setMemCacheClearStrategy(1);
    EXPECT_EQ(n, 0);
    c.setMemCacheTtl(1);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(c.memCacheTtl(), 1U);
}

TEST(ZmpConfig, WriteEmitsAllFiveKeysUnconditionallyAndReadRestoresThem) {
    const auto root = iniFrom({});
    ZmpConfig c;
    c.writeConfig(root->orCreateSubItem(u"ZmpDefaultParams"));
    const auto written = root->subItem(u"ZmpDefaultParams");
    ASSERT_NE(written, nullptr);
    EXPECT_EQ(written->valueNames(),
              (QStringList{u"MaxConnectToServerCount"_s, u"UseMemCache"_s, u"MemCacheCapacity"_s,
                           u"MemCacheTTL"_s, u"MemCacheClearStrategy"_s})); // legacy write order
    EXPECT_EQ(written->readInt(u"MaxConnectToServerCount", -1), 4);
    EXPECT_TRUE(written->readBool(u"UseMemCache", false));
    EXPECT_EQ(written->readInt(u"MemCacheCapacity", -1), 100);
    EXPECT_EQ(written->readInt(u"MemCacheTTL", -1), 60000);
    EXPECT_EQ(written->readInt(u"MemCacheClearStrategy", -1), 1);

    ZmpConfig other;
    other.setMaxConnectToServerCount(9);
    other.setUseMemCache(false);
    other.setMemCacheCapacity(7);
    other.setMemCacheTtl(5);
    other.setMemCacheClearStrategy(2);
    other.writeConfig(root->orCreateSubItem(u"Other"));
    ZmpConfig back;
    back.readConfig(root->subItem(u"Other"));
    EXPECT_EQ(back.maxConnectToServerCount(), 9U);
    EXPECT_FALSE(back.useMemCache());
    EXPECT_EQ(back.memCacheCapacity(), 7);
    EXPECT_EQ(back.memCacheTtl(), 5U);
    EXPECT_EQ(back.memCacheClearStrategy(), 2);
}

TEST(ZmpConfig, ReadWithProviderAlwaysNotifiesEvenWithoutChange) {
    ZmpConfig c;
    int n = 0;
    const auto sub = c.changed().subscribe([&] { ++n; });
    c.readConfig(nullptr); // legacy: a nil provider changes nothing and does not notify
    EXPECT_EQ(n, 0);
    c.readConfig(iniFrom("[S]\r\n")->subItem(u"S"));
    EXPECT_EQ(n, 1);
    EXPECT_EQ(c.maxConnectToServerCount(), 4U);
}

TEST(ZmpConfig, NegativeIniValueWrapsIntoTheCardinalFields) {
    // ReadInteger returns Integer; the Cardinal fields receive it unchanged (legacy L-123).
    ZmpConfig c;
    c.readConfig(iniFrom("[S]\r\nMaxConnectToServerCount=-1\r\nMemCacheTTL=-2\r\n")->subItem(u"S"));
    EXPECT_EQ(c.maxConnectToServerCount(), 0xFFFFFFFFU);
    EXPECT_EQ(c.memCacheTtl(), 0xFFFFFFFEU);
}

} // namespace
