// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-031. Ports i_TileStorageAbilities.pas, u_TileStorageAbilities.pas;
// docs/specs/storage.md §2.3, :242-297 (per-type table, T-0021A review finding 4 / Tech Lead
// ruling 2026-09-19, T-0021A rework)
#include "cg/storage/tile_storage_abilities.h"

#include <gtest/gtest.h>

using cg::storage::StorageClass;
using cg::storage::TileStorageAbilities;
using cg::storage::TileStorageKind;
using cg::storage::tileStorageTypeAbilities;
using cg::storage::VersionSupport;

namespace {

TEST(TileStorageAbilities, NoneIsReadOnlyEvenThoughItCannotRead) {
    // TTileStorageAbilities.IsReadOnly does not look at AllowRead -- a no-access storage is also
    // "read only".
    const TileStorageAbilities none = TileStorageAbilities::none();
    EXPECT_FALSE(none.allowRead);
    EXPECT_TRUE(none.isReadOnly());
}

TEST(TileStorageAbilities, ReadOnlyFactoryAllowsOnlyReadAndScan) {
    const TileStorageAbilities ro = TileStorageAbilities::readOnly();
    EXPECT_TRUE(ro.allowRead);
    EXPECT_TRUE(ro.allowScan);
    EXPECT_FALSE(ro.allowAdd);
    EXPECT_FALSE(ro.allowDelete);
    EXPECT_FALSE(ro.allowReplace);
    EXPECT_TRUE(ro.isReadOnly());
}

TEST(TileStorageAbilities, WriteOnlyFactoryAllowsOnlyWriteBits) {
    const TileStorageAbilities wo = TileStorageAbilities::writeOnly();
    EXPECT_FALSE(wo.allowRead);
    EXPECT_FALSE(wo.allowScan);
    EXPECT_TRUE(wo.allowAdd);
    EXPECT_TRUE(wo.allowDelete);
    EXPECT_TRUE(wo.allowReplace);
    EXPECT_FALSE(wo.isReadOnly());
}

TEST(TileStorageAbilities, ReadWriteFactoryAllowsEverythingAndIsNotReadOnly) {
    const TileStorageAbilities rw = TileStorageAbilities::readWrite();
    EXPECT_TRUE(rw.allowRead);
    EXPECT_TRUE(rw.allowScan);
    EXPECT_TRUE(rw.allowAdd);
    EXPECT_TRUE(rw.allowDelete);
    EXPECT_TRUE(rw.allowReplace);
    EXPECT_FALSE(rw.isReadOnly());
}

TEST(TileStorageAbilities, IsReadOnlyBecomesFalseWhenAnySingleWriteBitIsSet) {
    TileStorageAbilities a = TileStorageAbilities::none();
    a.allowAdd = true;
    EXPECT_FALSE(a.isReadOnly());

    TileStorageAbilities b = TileStorageAbilities::none();
    b.allowDelete = true;
    EXPECT_FALSE(b.isReadOnly());

    TileStorageAbilities c = TileStorageAbilities::none();
    c.allowReplace = true;
    EXPECT_FALSE(c.isReadOnly());
}

TEST(TileStorageAbilities, RestrictedByIsPairwiseAnd) {
    const TileStorageAbilities a = TileStorageAbilities::readWrite();
    const TileStorageAbilities b{true, false, true, false, true};
    const TileStorageAbilities r = a.restrictedBy(b, false);
    EXPECT_TRUE(r.allowRead);
    EXPECT_FALSE(r.allowScan);
    EXPECT_TRUE(r.allowAdd);
    EXPECT_FALSE(r.allowDelete);
    EXPECT_TRUE(r.allowReplace);
}

TEST(TileStorageAbilities, RestrictedByForceReadOnlyClearsOnlyWriteBits) {
    const TileStorageAbilities a = TileStorageAbilities::readWrite();
    const TileStorageAbilities r = a.restrictedBy(TileStorageAbilities::readWrite(), true);
    EXPECT_TRUE(r.allowRead);
    EXPECT_TRUE(r.allowScan);
    EXPECT_FALSE(r.allowAdd);
    EXPECT_FALSE(r.allowDelete);
    EXPECT_FALSE(r.allowReplace);
    EXPECT_TRUE(r.isReadOnly());
}

TEST(TileStorageAbilities, RestrictedByWithNoneOtherProducesNoAccess) {
    const TileStorageAbilities a = TileStorageAbilities::readWrite();
    const TileStorageAbilities r = a.restrictedBy(TileStorageAbilities::none(), false);
    EXPECT_FALSE(r.allowRead);
    EXPECT_FALSE(r.allowScan);
    EXPECT_FALSE(r.allowAdd);
    EXPECT_FALSE(r.allowDelete);
    EXPECT_FALSE(r.allowReplace);
}

TEST(TileStorageAbilities, RestrictedByReadWriteOtherIsIdentityWhenNotForced) {
    const TileStorageAbilities a = TileStorageAbilities::readWrite();
    const TileStorageAbilities r = a.restrictedBy(TileStorageAbilities::readWrite(), false);
    EXPECT_TRUE(r.allowRead && r.allowScan && r.allowAdd && r.allowDelete && r.allowReplace);
}

// --- tileStorageTypeAbilities(): the per-type table, storage.md :242-297, one row per legacy
// u_TileStorageType*.pas constructor ------------------------------------------------------------

TEST(TileStorageTypeAbilities, FolderSimpleIsReadWriteIgnoredVersionInSeparateFiles) {
    // u_TileStorageTypeFileSystemSimple.pas -- all 9 folder types.
    const auto a = tileStorageTypeAbilities(TileStorageKind::FolderSimple);
    EXPECT_EQ(a.base, TileStorageAbilities::readWrite());
    EXPECT_EQ(a.version, VersionSupport::Ignored);
    EXPECT_FALSE(a.supportsMixedContentTypes);
    EXPECT_EQ(a.storageClass, StorageClass::InSeparateFiles);
}

TEST(TileStorageTypeAbilities, Sqlite3NativeIsAlwaysMultiVersions) {
    // u_TileStorageTypeSQLite.pas:87-99; the type list always constructs it with IsVersioned =
    // True -- this is the load-bearing row the review flagged (finding 4).
    const auto a = tileStorageTypeAbilities(TileStorageKind::Sqlite3Native);
    EXPECT_EQ(a.base, TileStorageAbilities::readWrite());
    EXPECT_EQ(a.version, VersionSupport::MultiVersions);
    EXPECT_TRUE(a.supportsMixedContentTypes);
    EXPECT_EQ(a.storageClass, StorageClass::Folder);
}

TEST(TileStorageTypeAbilities, SqliteFileIsReadWriteIgnoredVersionOneFile) {
    // MBTiles / OsmAnd / Locus / RMaps -- u_TileStorageTypeSQLiteFile.pas:83-96, AFormatId <>
    // sfOruxMaps.
    const auto a = tileStorageTypeAbilities(TileStorageKind::SqliteFile);
    EXPECT_EQ(a.base, TileStorageAbilities::readWrite());
    EXPECT_EQ(a.version, VersionSupport::Ignored);
    EXPECT_FALSE(a.supportsMixedContentTypes);
    EXPECT_EQ(a.storageClass, StorageClass::OneFile);
}

TEST(TileStorageTypeAbilities, OruxMapsIsReadOnly) {
    // Same ctor, AFormatId = sfOruxMaps -- the other load-bearing row the review flagged.
    const auto a = tileStorageTypeAbilities(TileStorageKind::OruxMaps);
    EXPECT_EQ(a.base, TileStorageAbilities::readOnly());
    EXPECT_TRUE(a.base.isReadOnly());
    EXPECT_EQ(a.version, VersionSupport::Ignored);
    EXPECT_FALSE(a.supportsMixedContentTypes);
    EXPECT_EQ(a.storageClass, StorageClass::OneFile);
}

TEST(TileStorageTypeAbilities, RamIsReadWriteMinusScan) {
    // u_TileStorageTypeInRAM.pas:54-60 -- CTileStorageReadWrite - [tsatScan].
    const auto a = tileStorageTypeAbilities(TileStorageKind::Ram);
    EXPECT_TRUE(a.base.allowRead);
    EXPECT_FALSE(a.base.allowScan);
    EXPECT_TRUE(a.base.allowAdd);
    EXPECT_TRUE(a.base.allowDelete);
    EXPECT_TRUE(a.base.allowReplace);
    EXPECT_EQ(a.version, VersionSupport::Stored);
    EXPECT_TRUE(a.supportsMixedContentTypes);
    EXPECT_EQ(a.storageClass, StorageClass::InMemory);
}

TEST(TileStorageTypeAbilities, ArchiveBaseIsCallerSuppliedNotFixed) {
    // u_TileStorageTypeArchive.pas:82-107 takes ATileStorageAbilities as a constructor parameter
    // rather than fixing it -- the table's "supplied by the caller" row.
    const auto a = tileStorageTypeAbilities(TileStorageKind::Archive);
    EXPECT_EQ(a.base, TileStorageAbilities::none());
    EXPECT_EQ(a.version, VersionSupport::Ignored);
    EXPECT_TRUE(a.supportsMixedContentTypes);
    EXPECT_EQ(a.storageClass, StorageClass::OneFile);
}

} // namespace
