// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-034 (per-map half). Ports i_SimpleTileStorageConfig.pas,
// u_SimpleTileStorageConfig.pas, u_SimpleTileStorageConfigStatic.pas; docs/specs/storage.md
// §2.11 (:868-918).
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/storage/simple_tile_storage_config.h"

#include <QByteArray>
#include <QString>

#include <gtest/gtest.h>

#include <memory>

using cg::core::ConfigProviderIni;
using cg::core::IniFile;
using cg::storage::CacheTypeId;
using cg::storage::SimpleTileStorageConfig;
using cg::storage::SimpleTileStorageConfigStatic;
using cg::storage::SimpleTileStorageConfigStaticPtr;
using cg::storage::TileStorageAbilities;

namespace {

SimpleTileStorageConfigStaticPtr
makeDefaults(TileStorageAbilities abilities = TileStorageAbilities::readWrite(),
             CacheTypeId cacheType = CacheTypeId::Sqlite, QString nameInCache = QStringLiteral("tiles")) {
    return std::make_shared<const SimpleTileStorageConfigStatic>(SimpleTileStorageConfigStatic{
        cacheType, std::move(nameInCache), QByteArray("image/png"), QByteArray(".png"), abilities});
}

} // namespace

TEST(SimpleTileStorageConfig, SnapshotMatchesConstructorDefaults) {
    const auto defaults = makeDefaults();
    const SimpleTileStorageConfig config(defaults);
    const auto snap = config.snapshot();
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->cacheType, CacheTypeId::Sqlite);
    EXPECT_EQ(snap->nameInCache, QStringLiteral("tiles"));
    EXPECT_EQ(snap->mainContentType, QByteArray("image/png"));
    EXPECT_EQ(snap->tileFileExt, QByteArray(".png"));
    EXPECT_EQ(snap->abilities, TileStorageAbilities::readWrite());
    EXPECT_FALSE(config.readOnly());

    // MemCache fields (u_ZmpConfig.pas:70-73): initialised from the defaults, carried into the
    // snapshot.
    EXPECT_TRUE(snap->useMemCache);
    EXPECT_EQ(snap->memCacheCapacity, 100);
    EXPECT_EQ(snap->memCacheTtl, std::chrono::milliseconds(60'000));
    EXPECT_EQ(snap->memCacheClearStrategy, cg::storage::ClearStrategy::ByYoungest);
    EXPECT_TRUE(config.useMemCache());
    EXPECT_EQ(config.memCacheCapacity(), 100);
    EXPECT_EQ(config.memCacheTtl(), std::chrono::milliseconds(60'000));
    EXPECT_EQ(config.memCacheClearStrategy(), cg::storage::ClearStrategy::ByYoungest);
}

// Four setter rules, table-driven (u_SimpleTileStorageConfig.pas:360-438): allowRead/allowScan
// narrow only by the ZMP default; allowAdd/allowDelete/allowReplace additionally narrow by
// !isReadOnly.
TEST(SimpleTileStorageConfig, FourSetterRulesNarrowButNeverWiden) {
    struct Case {
        const char* name;
        bool defaultAllows;
        bool readOnly;
        bool requested;
        bool expected;
    };
    // Rule for allowRead/allowScan: defaultAllows && requested (readOnly irrelevant).
    const Case readScanCases[] = {
        {"defaultTrueRequestTrue", true, false, true, true},
        {"defaultFalseRequestTrue", false, false, true, false},
        {"defaultTrueRequestFalse", true, false, false, false},
        {"defaultTrueReadOnlyRequestTrue", true, true, true, true}, // read/scan ignore isReadOnly
    };
    for (const auto& c : readScanCases) {
        TileStorageAbilities abilities = TileStorageAbilities::none();
        abilities.allowRead = c.defaultAllows;
        abilities.allowScan = c.defaultAllows;
        // isReadOnly() only depends on add/delete/replace, so give it independent write bits.
        abilities.allowAdd = !c.readOnly;
        SimpleTileStorageConfig config(makeDefaults(abilities));
        config.setAllowRead(c.requested);
        config.setAllowScan(c.requested);
        EXPECT_EQ(config.allowRead(), c.expected) << c.name;
        EXPECT_EQ(config.allowScan(), c.expected) << c.name;
    }

    // Rule for allowAdd/allowDelete/allowReplace: defaultAllows && !isReadOnly && requested.
    const Case writeCases[] = {
        {"defaultTrueNotReadOnlyRequestTrue", true, false, true, true},
        {"defaultFalseNotReadOnlyRequestTrue", false, false, true, false},
        {"defaultTrueReadOnlyRequestTrue", true, true, true, false}, // isReadOnly forces false
        {"defaultTrueNotReadOnlyRequestFalse", true, false, false, false},
    };
    for (const auto& c : writeCases) {
        TileStorageAbilities abilities = TileStorageAbilities::readWrite();
        abilities.allowAdd = c.defaultAllows;
        abilities.allowDelete = c.defaultAllows;
        abilities.allowReplace = c.defaultAllows;
        SimpleTileStorageConfig config(makeDefaults(abilities));
        config.setReadOnly(c.readOnly);
        config.setAllowAdd(c.requested);
        config.setAllowDelete(c.requested);
        config.setAllowReplace(c.requested);
        EXPECT_EQ(config.allowAdd(), c.expected) << c.name;
        EXPECT_EQ(config.allowDelete(), c.expected) << c.name;
        EXPECT_EQ(config.allowReplace(), c.expected) << c.name;
    }
}

TEST(SimpleTileStorageConfig, SameValueWritesNothingAndDoesNotNotify) {
    SimpleTileStorageConfig config(makeDefaults());
    int notifications = 0;
    const auto sub = config.changed().subscribe([&] { ++notifications; });

    config.setNameInCache(config.nameInCache()); // same value
    config.setAllowRead(config.allowRead());     // same value
    config.setUseMemCache(config.useMemCache());
    config.setMemCacheCapacity(config.memCacheCapacity());
    config.setMemCacheTtl(config.memCacheTtl());
    config.setMemCacheClearStrategy(config.memCacheClearStrategy());
    EXPECT_EQ(notifications, 0);

    config.setNameInCache(QStringLiteral("other"));
    EXPECT_EQ(notifications, 1);

    config.setMemCacheCapacity(200);
    EXPECT_EQ(notifications, 2);
}

TEST(SimpleTileStorageConfig, SetCacheTypeIsANoOpWhenEitherSideIsGoogleEarth) {
    // Default is Ge: switching away from it is rejected.
    {
        SimpleTileStorageConfig config(makeDefaults(TileStorageAbilities::readWrite(), CacheTypeId::Ge));
        int notifications = 0;
        const auto sub = config.changed().subscribe([&] { ++notifications; });
        config.setCacheType(CacheTypeId::Sqlite);
        EXPECT_EQ(config.cacheType(), CacheTypeId::Ge);
        EXPECT_EQ(notifications, 0);
    }
    // Default is not Ge: switching TO Ge is rejected.
    {
        SimpleTileStorageConfig config(makeDefaults(TileStorageAbilities::readWrite(), CacheTypeId::Sqlite));
        config.setCacheType(CacheTypeId::Ge);
        EXPECT_EQ(config.cacheType(), CacheTypeId::Sqlite);
        // A non-Ge->non-Ge switch still works normally.
        config.setCacheType(CacheTypeId::Bdb);
        EXPECT_EQ(config.cacheType(), CacheTypeId::Bdb);
    }
}

TEST(SimpleTileStorageConfig, EmptyNameInCacheForcesReadOnly) {
    SimpleTileStorageConfig config(makeDefaults()); // ZMP default is read/write, name = "tiles"
    ASSERT_FALSE(config.readOnly());
    config.setNameInCache(QString());
    EXPECT_TRUE(config.readOnly());
    EXPECT_FALSE(config.allowAdd()); // narrowed by the now-true isReadOnly
    EXPECT_FALSE(config.allowDelete());
    EXPECT_FALSE(config.allowReplace());

    // Legacy quirk, kept verbatim (u_SimpleTileStorageConfig.pas:476-488): SetNameInCache
    // re-invokes SetIsReadOnly(FIsReadOnly), i.e. with the STALE isReadOnly value as the "OR"
    // operand, so a name becoming non-empty again does NOT auto-clear a read-only forced by the
    // earlier empty name -- it stays sticky until an explicit setReadOnly(false) call.
    config.setNameInCache(QStringLiteral("tiles"));
    EXPECT_TRUE(config.readOnly());

    config.setReadOnly(false);
    EXPECT_FALSE(config.readOnly());
    // Another sticky quirk: setReadOnly(false)'s cascade re-applies allowAdd_'s OWN current
    // (already-narrowed-to-false) value, not any earlier intent, so it stays false -- only an
    // explicit setAllowAdd(true) restores it.
    EXPECT_FALSE(config.allowAdd());
    config.setAllowAdd(true);
    EXPECT_TRUE(config.allowAdd());
}

TEST(SimpleTileStorageConfig, ReadWriteRoundTripVerbatimKeysWriteOnlyIfDiffers) {
    auto file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    const auto section = root->orCreateSubItem(u"zzzMap");
    section->writeInt(u"CacheType", static_cast<qint32>(CacheTypeId::Bdb));
    section->writeString(u"NameInCache", QStringLiteral("custom"));
    section->writeBool(u"IsReadOnly", true);
    section->writeBool(u"AllowRead", false); // differs from the ZMP default (true): gets written
    section->writeBool(u"AllowScan", false);
    section->writeBool(u"AllowAdd", false);
    section->writeBool(u"AllowDelete", false);
    section->writeBool(u"AllowReplace", false);

    SimpleTileStorageConfig config(makeDefaults());
    config.readConfig(root->subItem(u"zzzMap"));

    EXPECT_EQ(config.cacheType(), CacheTypeId::Bdb);
    EXPECT_EQ(config.nameInCache(), QStringLiteral("custom"));
    EXPECT_TRUE(config.readOnly());
    EXPECT_FALSE(config.allowRead());
    EXPECT_FALSE(config.allowScan());

    // Changed values: every key round-trips verbatim.
    auto outFile = std::make_shared<IniFile>();
    const auto outRoot = ConfigProviderIni::wrap(outFile);
    const auto outSection = outRoot->orCreateSubItem(u"zzzMap");
    config.writeConfig(outSection);
    EXPECT_EQ(outSection->readInt(u"CacheType", -1), static_cast<qint32>(CacheTypeId::Bdb));
    EXPECT_EQ(outSection->readString(u"NameInCache", QString()), QStringLiteral("custom"));
    EXPECT_TRUE(outSection->readBool(u"IsReadOnly", false));
    EXPECT_FALSE(outSection->readBool(u"AllowRead", true));
    EXPECT_FALSE(outSection->readBool(u"AllowScan", true));
    EXPECT_EQ(outFile->keyNames(u"zzzMap").size(), 8); // exactly the eight §2.11 keys

    // Reset back to the ZMP defaults: doWriteConfig() must delete every key instead of writing it.
    SimpleTileStorageConfig defaultConfig(makeDefaults());
    auto defaultFile = std::make_shared<IniFile>();
    const auto defaultRoot = ConfigProviderIni::wrap(defaultFile);
    const auto defaultSection = defaultRoot->orCreateSubItem(u"defMap");
    defaultConfig.writeConfig(defaultSection);
    EXPECT_EQ(defaultFile->keyNames(u"defMap").size(), 0);
}
