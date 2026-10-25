// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-001, FEAT-STO-036. Ports c_CacheTypeCodes.pas, u_TileStorageTypeListSimple.pas;
// docs/specs/storage.md §2.10.4 (:775-867)
#include "cg/geo/geo_types.h"
#include "cg/storage/cache_type.h"
#include "cg/storage/tile_name_codec.h"

#include <QDir>
#include <QSet>
#include <QString>
#include <QUuid>

#include <gtest/gtest.h>

using cg::geo::PointI;
using cg::storage::cacheTypeById;
using cg::storage::CacheTypeDescriptor;
using cg::storage::CacheTypeId;
using cg::storage::cacheTypes;
using cg::storage::codecForCacheType;

namespace {

TEST(CacheType, TableHasExactlyTwentyTwoRowsInLegacyRegistrationOrder) {
    const std::vector<CacheTypeDescriptor>& table = cacheTypes();
    ASSERT_EQ(table.size(), 22u);
    // u_TileStorageTypeListSimple.pas:TTileStorageTypeListSimple.Create registration order.
    const CacheTypeId expectedOrder[] = {CacheTypeId::Sas,          CacheTypeId::Gmv,
                                         CacheTypeId::Es,           CacheTypeId::Gm,
                                         CacheTypeId::GmAux,        CacheTypeId::GmBing,
                                         CacheTypeId::OsmAnd,       CacheTypeId::Mobac,
                                         CacheTypeId::Tms,          CacheTypeId::Bdb,
                                         CacheTypeId::BdbVersioned, CacheTypeId::Sqlite,
                                         CacheTypeId::GeoCacher,    CacheTypeId::Ge,
                                         CacheTypeId::GeTerrain,    CacheTypeId::Dbms,
                                         CacheTypeId::Ram,          CacheTypeId::SqliteMbTiles,
                                         CacheTypeId::SqliteOsmAnd, CacheTypeId::SqliteLocus,
                                         CacheTypeId::SqliteRMaps,  CacheTypeId::SqliteOruxMaps};
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(table[i].id, expectedOrder[i]) << "row " << i;
    }
}

TEST(CacheType, EveryUuidIsPresentAndNonNull) {
    for (const CacheTypeDescriptor& d : cacheTypes()) {
        EXPECT_FALSE(d.uuid.isNull()) << static_cast<int>(d.id);
    }
}

TEST(CacheType, AllUuidsUniqueExceptTheDocumentedBdbPair) {
    QSet<QUuid> seen;
    int duplicates = 0;
    for (const CacheTypeDescriptor& d : cacheTypes()) {
        if (seen.contains(d.uuid)) {
            ++duplicates;
        }
        seen.insert(d.uuid);
    }
    // Bdb and BdbVersioned are the ONE legacy-verbatim duplicate (u_TileStorageTypeListSimple.pas
    // passes CTileStorageTypeBerkeleyDB to both list items).
    EXPECT_EQ(duplicates, 1);
}

TEST(CacheType, BdbAndBdbVersionedShareOneUuidVerbatim) {
    const CacheTypeDescriptor* bdb = cacheTypeById(CacheTypeId::Bdb);
    const CacheTypeDescriptor* bdbVersioned = cacheTypeById(CacheTypeId::BdbVersioned);
    ASSERT_NE(bdb, nullptr);
    ASSERT_NE(bdbVersioned, nullptr);
    EXPECT_EQ(bdb->uuid, bdbVersioned->uuid);
    EXPECT_EQ(bdb->uuid, QUuid(QStringLiteral("{3DBF81CD-9356-40EB-9778-DE4D98E5BE61}")));
}

// T-0333: Bdb and BdbVersioned joined the list when the BerkeleyDB backend was removed (owner 2026-10-21).
TEST(CacheType, SupportedOnLinuxIsFalseOnlyForGeGeTerrainGeoCacherDbmsBdbBdbVersioned) {
    for (const CacheTypeDescriptor& d : cacheTypes()) {
        const bool expectedUnsupported = d.id == CacheTypeId::Ge || d.id == CacheTypeId::GeTerrain ||
                                         d.id == CacheTypeId::GeoCacher || d.id == CacheTypeId::Dbms ||
                                         d.id == CacheTypeId::Bdb || d.id == CacheTypeId::BdbVersioned;
        EXPECT_EQ(!d.supportedOnLinux, expectedUnsupported) << static_cast<int>(d.id);
    }
}

TEST(CacheType, PathIdentAndDefaultFolderAreBothPresentOrBothAbsent) {
    for (const CacheTypeDescriptor& d : cacheTypes()) {
        EXPECT_EQ(d.pathIdent == nullptr, d.defaultFolder == nullptr) << static_cast<int>(d.id);
    }
    // Only Ram and SqliteOruxMaps have no [PathToCache] entry.
    EXPECT_EQ(cacheTypeById(CacheTypeId::Ram)->pathIdent, nullptr);
    EXPECT_EQ(cacheTypeById(CacheTypeId::SqliteOruxMaps)->pathIdent, nullptr);
}

TEST(CacheType, FourGmTilesRowsShareOnePathIdentAndDefaultFolder) {
    // GMTiles is the [PathToCache] key for four different cache types (rows 4-7); OsmAnd+ really
    // does default into cache_gmt, not into a folder of its own.
    for (const CacheTypeId id :
         {CacheTypeId::Gm, CacheTypeId::GmAux, CacheTypeId::GmBing, CacheTypeId::OsmAnd}) {
        const CacheTypeDescriptor* d = cacheTypeById(id);
        ASSERT_NE(d, nullptr) << static_cast<int>(id);
        EXPECT_STREQ(d->pathIdent, "GMTiles");
        EXPECT_STREQ(d->defaultFolder, "cache_gmt");
    }
}

TEST(CacheType, DbmsDefaultFolderKeepsLegacyBackslashVerbatim) {
    // c_CacheTypeCodes.pas: c_File_Cache_Default_DBMS = 'SASGIS_DBMS\$' -- legacy wins over the
    // spec table's forward slash (docs/specs/storage.md :798); see docs/handoffs/T-0021D.md.
    const CacheTypeDescriptor* dbms = cacheTypeById(CacheTypeId::Dbms);
    ASSERT_NE(dbms, nullptr);
    EXPECT_STREQ(dbms->defaultFolder, "SASGIS_DBMS\\$");
}

TEST(CacheType, PathCompositionJoinsGlobalRootDefaultFolderAndMapSubfolder) {
    // FEAT-STO-036: <global cache root>/<defaultFolder or override>/<map.NameInCache>.
    const CacheTypeDescriptor* sas = cacheTypeById(CacheTypeId::Sas);
    ASSERT_NE(sas, nullptr);
    const QString composed =
        QDir(QDir(QStringLiteral("/cache-root")).filePath(QString::fromLatin1(sas->defaultFolder)))
            .filePath(QStringLiteral("mymap"));
    EXPECT_EQ(composed, QStringLiteral("/cache-root/cache/mymap"));
}

TEST(CacheType, CodecForCacheTypeCoversAllNineFolderIds) {
    const CacheTypeId folderIds[] = {CacheTypeId::Sas,   CacheTypeId::Gmv,    CacheTypeId::Es,
                                     CacheTypeId::Gm,    CacheTypeId::GmAux,  CacheTypeId::GmBing,
                                     CacheTypeId::Mobac, CacheTypeId::OsmAnd, CacheTypeId::Tms};
    for (const CacheTypeId id : folderIds) {
        EXPECT_NE(codecForCacheType(id), nullptr) << static_cast<int>(id);
    }
}

TEST(CacheType, CodecForCacheTypeGivesSqliteTheRegionCodecFormula) {
    // Sqlite has the region codec (storage.md §2.10.2), spot-checked at Z = 0 (matches the
    // cache_sqlite/map/z1/0/0/0.0.sqlitedb fixture's formula, T-0021C).
    for (const CacheTypeId id : {CacheTypeId::Sqlite}) {
        const cg::storage::ITileNameCodecPtr codec = codecForCacheType(id);
        ASSERT_NE(codec, nullptr) << static_cast<int>(id);
        EXPECT_EQ(codec->tilePath(PointI(0, 0), 0), QStringLiteral("z1/0/0/0.0")) << static_cast<int>(id);
    }
}

TEST(CacheType, CodecForCacheTypeIsNullForNonFolderNonRegionIds) {
    const CacheTypeId nonFolder[] = {CacheTypeId::GeoCacher,    CacheTypeId::Ge,
                                     CacheTypeId::GeTerrain,    CacheTypeId::Dbms,
                                     CacheTypeId::Bdb,          CacheTypeId::BdbVersioned,
                                     CacheTypeId::Ram,          CacheTypeId::SqliteMbTiles,
                                     CacheTypeId::SqliteOsmAnd, CacheTypeId::SqliteLocus,
                                     CacheTypeId::SqliteRMaps,  CacheTypeId::SqliteOruxMaps};
    for (const CacheTypeId id : nonFolder) {
        EXPECT_EQ(codecForCacheType(id), nullptr) << static_cast<int>(id);
    }
    EXPECT_EQ(codecForCacheType(CacheTypeId::Invalid), nullptr);
    EXPECT_EQ(codecForCacheType(CacheTypeId::Default), nullptr);
}

} // namespace
