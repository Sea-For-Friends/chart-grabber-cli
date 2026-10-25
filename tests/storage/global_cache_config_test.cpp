// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-036. Ports i_GlobalCacheConfig.pas, u_GlobalCacheConfig.pas;
// docs/specs/storage.md §2.10.4's "Path composition" (:846-867)
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/storage/global_cache_config.h"

#include <QByteArray>
#include <QString>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

using cg::core::ConfigProviderIni;
using cg::core::IniFile;
using cg::core::PathConfig;
using cg::core::PathConfigPtr;
using cg::storage::CacheTypeId;
using cg::storage::GlobalCacheConfig;

namespace {

PathConfigPtr makeAbsoluteRoot() {
    return PathConfig::create(QString(), QStringLiteral("/tmp/cg-cache-root"), nullptr);
}

TEST(GlobalCacheConfig, PathForComposesGlobalRootAndDefaultFolder) {
    GlobalCacheConfig config(makeAbsoluteRoot());
    const auto sasPath = config.pathFor(CacheTypeId::Sas);
    ASSERT_NE(sasPath, nullptr);
    EXPECT_EQ(sasPath->fullPath(), QStringLiteral("/tmp/cg-cache-root/cache"));
}

TEST(GlobalCacheConfig, GmTilesRowsShareOnePathConfigInstance) {
    // u_GlobalCacheConfig.pas has a single FGMTilesPath field for Gm/GmAux/GmBing/OsmAnd+ --
    // setting one moves all four together.
    GlobalCacheConfig config(makeAbsoluteRoot());
    const auto gm = config.pathFor(CacheTypeId::Gm);
    const auto gmAux = config.pathFor(CacheTypeId::GmAux);
    const auto gmBing = config.pathFor(CacheTypeId::GmBing);
    const auto osmAnd = config.pathFor(CacheTypeId::OsmAnd);
    ASSERT_NE(gm, nullptr);
    EXPECT_EQ(gm, gmAux);
    EXPECT_EQ(gm, gmBing);
    EXPECT_EQ(gm, osmAnd);
    gm->setPath(QStringLiteral("custom_gmt"));
    EXPECT_EQ(osmAnd->path(), QStringLiteral("custom_gmt"));
}

TEST(GlobalCacheConfig, RamAndOruxMapsHaveNoPath) {
    GlobalCacheConfig config(makeAbsoluteRoot());
    EXPECT_EQ(config.pathFor(CacheTypeId::Ram), nullptr);
    EXPECT_EQ(config.pathFor(CacheTypeId::SqliteOruxMaps), nullptr);
}

TEST(GlobalCacheConfig, DefaultDefCacheIsSqlite) {
    const GlobalCacheConfig config(makeAbsoluteRoot());
    EXPECT_EQ(config.defCache(), CacheTypeId::Sqlite);
}

TEST(GlobalCacheConfig, SetDefCacheAcceptsAWhitelistedId) {
    GlobalCacheConfig config(makeAbsoluteRoot());
    config.setDefCache(CacheTypeId::Bdb);
    EXPECT_EQ(config.defCache(), CacheTypeId::Bdb);
}

TEST(GlobalCacheConfig, SetDefCacheSilentlyRejectsGmBingAndOsmAnd) {
    // u_GlobalCacheConfig.pas:SetDefCache's whitelist does NOT include GM_Bing (42) or OsmAnd
    // (44), even though they are ordinary Linux-supported folder types -- docs/specs/storage.md
    // :864-865 undercounts this (only names Ge/GeTerrain/GeoCacher/SqliteOruxMaps as rejected).
    GlobalCacheConfig config(makeAbsoluteRoot());
    config.setDefCache(CacheTypeId::GmBing);
    EXPECT_EQ(config.defCache(), CacheTypeId::Sqlite);
    config.setDefCache(CacheTypeId::OsmAnd);
    EXPECT_EQ(config.defCache(), CacheTypeId::Sqlite);
}

TEST(GlobalCacheConfig, SetDefCacheRejectsGeGeTerrainGeoCacherAndOruxMaps) {
    GlobalCacheConfig config(makeAbsoluteRoot());
    for (const CacheTypeId id :
         {CacheTypeId::Ge, CacheTypeId::GeTerrain, CacheTypeId::GeoCacher, CacheTypeId::SqliteOruxMaps}) {
        config.setDefCache(id);
        EXPECT_EQ(config.defCache(), CacheTypeId::Sqlite) << static_cast<int>(id);
    }
}

TEST(GlobalCacheConfig, ReadConfigAppliesDefCacheAndPerTypePathsFromTheIni) {
    auto ini = IniFile::fromBytes(QByteArrayLiteral("[VIEW]\r\n"
                                                    "DefCache=6\r\n" // Bdb
                                                    "[PathToCache]\r\n"
                                                    "SASC=my_sas_cache\r\n"));
    ASSERT_TRUE(ini.has_value());
    const auto provider = ConfigProviderIni::wrap(std::make_shared<IniFile>(std::move(ini.value())));

    GlobalCacheConfig config(makeAbsoluteRoot());
    config.readConfig(provider);

    EXPECT_EQ(config.defCache(), CacheTypeId::Bdb);
    EXPECT_EQ(config.pathFor(CacheTypeId::Sas)->path(), QStringLiteral("my_sas_cache"));
}

TEST(GlobalCacheConfig, ReadConfigIgnoresAnOutOfWhitelistDefCacheValue) {
    auto ini = IniFile::fromBytes(QByteArrayLiteral("[VIEW]\r\nDefCache=42\r\n")); // GmBing
    ASSERT_TRUE(ini.has_value());
    const auto provider = ConfigProviderIni::wrap(std::make_shared<IniFile>(std::move(ini.value())));

    GlobalCacheConfig config(makeAbsoluteRoot());
    config.readConfig(provider);

    EXPECT_EQ(config.defCache(), CacheTypeId::Sqlite); // whitelist rejects 42, keeps the default
}

} // namespace
