// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: docs/specs/net.md §2.16, test plan §7.9
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/net/inet_config.h"
#include "cg/net/tile_downloader_config.h"

#include <QPoint>
#include <QString>

#include <gtest/gtest.h>

#include <memory>

using cg::core::ConfigProviderIni;
using cg::core::IniFile;
using cg::net::InetConfig;
using cg::net::TileDownloaderConfig;
using cg::net::TileDownloaderSettings;

namespace {

TileDownloaderSettings makeZmpDefaults() {
    TileDownloaderSettings zmp;
    zmp.enabled = true;
    zmp.allowUseCookie = false;
    zmp.waitIntervalMs = 100;
    zmp.maxConnectToServerCount = 4;
    zmp.ignoreMimeType = false;
    zmp.detectMimeType = true;
    zmp.expectedMimeTypes = "image/jpg,image/png";
    zmp.defaultMimeType = "image/jpg";
    zmp.iteratorSubRectSize = QPoint(2, 2);
    zmp.restartDownloadOnMemCacheTTL = true;
    return zmp;
}

} // namespace

TEST(TileDownloaderConfig, ZmpDefaultsAreTheBaseline) {
    const auto inet = std::make_shared<InetConfig>();
    const TileDownloaderSettings zmp = makeZmpDefaults();
    const TileDownloaderConfig config(inet, zmp);

    EXPECT_EQ(config.enabled(), zmp.enabled);
    EXPECT_EQ(config.allowUseCookie(), zmp.allowUseCookie);
    EXPECT_EQ(config.waitIntervalMs(), zmp.waitIntervalMs);
    EXPECT_EQ(config.maxConnectToServerCount(), zmp.maxConnectToServerCount);
    EXPECT_EQ(config.ignoreMimeType(), zmp.ignoreMimeType);
    EXPECT_EQ(config.detectMimeType(), zmp.detectMimeType);
    EXPECT_EQ(config.expectedMimeTypes(), zmp.expectedMimeTypes);
    EXPECT_EQ(config.defaultMimeType(), zmp.defaultMimeType);
    EXPECT_EQ(config.iteratorSubRectSize(), zmp.iteratorSubRectSize);
    EXPECT_EQ(config.restartDownloadOnMemCacheTTL(), zmp.restartDownloadOnMemCacheTTL);

    const auto s = config.settings();
    EXPECT_EQ(s->waitIntervalMs, zmp.waitIntervalMs);
    ASSERT_NE(s->inet, nullptr);
}

TEST(TileDownloaderConfig, LocalIniOverridesSubset) {
    auto file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    const auto section = root->orCreateSubItem(u"Map1");
    section->writeBool(u"AllowUseCookie", true);
    section->writeBool(u"IgnoreContentType", true);
    section->writeBool(u"DetectContentType", false);
    section->writeString(u"DefaultContentType", QStringLiteral("image/png"));
    section->writeString(u"ContentType", QStringLiteral("image/png"));
    section->writeInt(u"Sleep", 250);
    section->writeInt(u"MaxConnectToServerCount", 8);
    section->writeBool(u"RestartDownloadOnMemCacheTTL", false);
    // Neither of these is ever read from the local INI (spec table) -- present only to prove that.
    section->writeBool(u"UseDwn", false);

    const auto inet = std::make_shared<InetConfig>();
    const TileDownloaderSettings zmp = makeZmpDefaults();
    TileDownloaderConfig config(inet, zmp);
    config.readConfig(root->subItem(u"Map1"));

    EXPECT_TRUE(config.allowUseCookie());
    EXPECT_TRUE(config.ignoreMimeType());
    EXPECT_FALSE(config.detectMimeType());
    EXPECT_EQ(config.defaultMimeType(), QByteArray("image/png"));
    EXPECT_EQ(config.expectedMimeTypes(), QByteArray("image/png"));
    EXPECT_EQ(config.waitIntervalMs(), 250u);
    EXPECT_EQ(config.maxConnectToServerCount(), 8u);
    EXPECT_FALSE(config.restartDownloadOnMemCacheTTL());

    // enabled and iteratorSubRectSize are ZMP-only: the stray "UseDwn" key changes nothing.
    EXPECT_EQ(config.enabled(), zmp.enabled);
    EXPECT_EQ(config.iteratorSubRectSize(), zmp.iteratorSubRectSize);
}

TEST(TileDownloaderConfig, MaxConnectClampedTo64) {
    const auto inet = std::make_shared<InetConfig>();
    TileDownloaderConfig config(inet, makeZmpDefaults());
    config.setMaxConnectToServerCount(1000);
    EXPECT_EQ(config.maxConnectToServerCount(), 64u);
}

TEST(TileDownloaderConfig, MaxConnectZeroBecomesOne) {
    const auto inet = std::make_shared<InetConfig>();
    TileDownloaderConfig config(inet, makeZmpDefaults());
    config.setMaxConnectToServerCount(0);
    EXPECT_EQ(config.maxConnectToServerCount(), 1u);
}

TEST(TileDownloaderConfig, SleepHasNoClamp) {
    const auto inet = std::make_shared<InetConfig>();
    TileDownloaderConfig config(inet, makeZmpDefaults());
    config.setWaitIntervalMs(999999999u);
    EXPECT_EQ(config.waitIntervalMs(), 999999999u);
}

TEST(TileDownloaderConfig, EnabledCannotBeRaisedAboveZmp) {
    const auto inet = std::make_shared<InetConfig>();
    TileDownloaderSettings zmp = makeZmpDefaults();
    zmp.enabled = false; // disabled in the ZMP
    TileDownloaderConfig config(inet, zmp);
    ASSERT_FALSE(config.enabled());
    config.setEnabled(true);
    EXPECT_FALSE(config.enabled()); // coerced back down
}

TEST(TileDownloaderConfig, WriteOnlyPersistsSleepAndMaxConnect) {
    const auto inet = std::make_shared<InetConfig>();
    const TileDownloaderSettings zmp = makeZmpDefaults();
    TileDownloaderConfig config(inet, zmp);

    config.setAllowUseCookie(!zmp.allowUseCookie);
    config.setIgnoreMimeType(!zmp.ignoreMimeType);
    config.setDetectMimeType(!zmp.detectMimeType);
    config.setDefaultMimeType("image/tiff");
    config.setExpectedMimeTypes("image/tiff");
    config.setRestartDownloadOnMemCacheTTL(!zmp.restartDownloadOnMemCacheTTL);
    config.setWaitIntervalMs(zmp.waitIntervalMs + 500);
    config.setMaxConnectToServerCount(zmp.maxConnectToServerCount + 10);

    auto file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    const auto section = root->orCreateSubItem(u"Map1");
    config.writeConfig(section);

    const QStringList keys = file->keyNames(u"Map1");
    EXPECT_EQ(keys.size(), 2);
    EXPECT_TRUE(file->hasKey(u"Map1", u"Sleep"));
    EXPECT_TRUE(file->hasKey(u"Map1", u"MaxConnectToServerCount"));
    EXPECT_FALSE(file->hasKey(u"Map1", u"AllowUseCookie"));
    EXPECT_FALSE(file->hasKey(u"Map1", u"IgnoreContentType"));
    EXPECT_FALSE(file->hasKey(u"Map1", u"DetectContentType"));
    EXPECT_FALSE(file->hasKey(u"Map1", u"DefaultContentType"));
    EXPECT_FALSE(file->hasKey(u"Map1", u"ContentType"));
    EXPECT_FALSE(file->hasKey(u"Map1", u"RestartDownloadOnMemCacheTTL"));
}

TEST(TileDownloaderConfig, WriteDeletesKeyWhenEqualToDefault) {
    const auto inet = std::make_shared<InetConfig>();
    const TileDownloaderSettings zmp = makeZmpDefaults();
    TileDownloaderConfig config(inet, zmp);

    auto file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    const auto section = root->orCreateSubItem(u"Map1");

    config.setWaitIntervalMs(zmp.waitIntervalMs + 500);
    config.writeConfig(section);
    ASSERT_TRUE(file->hasKey(u"Map1", u"Sleep"));

    config.setWaitIntervalMs(zmp.waitIntervalMs);  // back to the ZMP baseline
    config.writeConfig(section);                   // same section, second save
    EXPECT_FALSE(file->hasKey(u"Map1", u"Sleep")); // deleted, not left stale
}
