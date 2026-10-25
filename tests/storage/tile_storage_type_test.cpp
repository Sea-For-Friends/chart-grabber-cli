// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-001, FEAT-STO-026. Ports u_TileStorageTypeBase.pas (GetStorageAbilitiesByConfig
// + BuildStorage's StorageConfig.ini lookup), u_TileStorageTypeFileSystemSimple.pas,
// u_TileStorageTypeInRAM.pas; docs/specs/storage.md §2.12 (:919-951), §2.13 (intro), §2.14 (intro).
//
// Test-count ruling (T-0021F3b, "port, don't multiply"): each type reports legacy's fixed
// abilities (two thin TESTs, the per-field table lives with tileStorageTypeAbilities() itself in
// tile_storage_abilities_test.cpp) and one "builds a working storage" case per type -- the
// filesystem one is also the ticket's own read-only-StorageConfig.ini case.
#include "cg/core/content_type.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/storage/codecs/sas_tile_name_codec.h"
#include "cg/storage/file_system_tile_storage_type.h"
#include "cg/storage/map_version.h"
#include "cg/storage/ram_tile_storage_type.h"
#include "cg/storage/tile_date.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_pyramid_notifier.h"
#include "cg/storage/tile_storage_type.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

using cg::core::ContentTypeInfo;
using cg::core::ContentTypeRegistry;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::PointI;
using cg::geo::ProjectionSetFactory;
using cg::geo::ProjectionSetPtr;
using cg::storage::applyStorageConfigIni;
using cg::storage::ClearStrategy;
using cg::storage::FileSystemTileStorageType;
using cg::storage::fromPosixSeconds;
using cg::storage::ITileStoragePtr;
using cg::storage::LoadMode;
using cg::storage::MapVersionFactory;
using cg::storage::RamTileStorageType;
using cg::storage::SasTileNameCodec;
using cg::storage::StorageClass;
using cg::storage::TileInfoMemCache;
using cg::storage::TilePyramidNotifier;
using cg::storage::TileStorageAbilities;
using cg::storage::TileStorageKind;
using cg::storage::tileStorageTypeAbilities;
using cg::storage::VersionSupport;

namespace {

void writeFile(const QString& path, const QString& contents) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate)) << path.toStdString();
    QTextStream out(&f);
    out << contents;
}

ProjectionSetPtr makeProjectionSet() {
    static const auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    static const ProjectionSetFactory factory{HashSeed{}, datums};
    return factory.byCode(kProjEpsgGoogle, kTileSplitQuadrate256x256);
}

const ContentTypeInfo& jpegContentType() {
    const ContentTypeInfo* info = ContentTypeRegistry::standard().infoByType(QByteArrayView("image/jpg"));
    return *info;
}

TEST(ApplyStorageConfigIni, NoIniFilePresentReturnsBaseNarrowedOnlyByForced) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString mapPath = QDir(dir.path()).filePath(QStringLiteral("nonexistent_map"));

    const TileStorageAbilities base = TileStorageAbilities::readWrite();
    const TileStorageAbilities forced{true, true, true, false, true}; // no delete
    const TileStorageAbilities result = applyStorageConfigIni(base, forced, StorageClass::OneFile, mapPath);

    EXPECT_EQ(result, base.restrictedBy(forced, false));
    EXPECT_FALSE(result.allowDelete);
    EXPECT_TRUE(result.allowAdd);
}

TEST(ApplyStorageConfigIni, OneFileIniNarrowsByCommonSection) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString mapPath = QDir(dir.path()).filePath(QStringLiteral("map.sqlitedb"));
    writeFile(mapPath + QStringLiteral(".StorageConfig.ini"),
              QStringLiteral("[Common]\r\nAllowDelete=0\r\n"));

    const TileStorageAbilities result = applyStorageConfigIni(
        TileStorageAbilities::readWrite(), TileStorageAbilities::readWrite(), StorageClass::OneFile, mapPath);

    EXPECT_TRUE(result.allowRead);
    EXPECT_TRUE(result.allowScan);
    EXPECT_TRUE(result.allowAdd);
    EXPECT_FALSE(result.allowDelete); // narrowed by the ini
    EXPECT_TRUE(result.allowReplace);
}

TEST(ApplyStorageConfigIni, CommonSectionIsReadOnlyForceClearsTheThreeWriteBits) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString mapPath = QDir(dir.path()).filePath(QStringLiteral("map.sqlitedb"));
    writeFile(mapPath + QStringLiteral(".StorageConfig.ini"), QStringLiteral("[Common]\r\nIsReadOnly=1\r\n"));

    const TileStorageAbilities result = applyStorageConfigIni(
        TileStorageAbilities::readWrite(), TileStorageAbilities::readWrite(), StorageClass::OneFile, mapPath);

    EXPECT_TRUE(result.allowRead);
    EXPECT_TRUE(result.allowScan);
    EXPECT_FALSE(result.allowAdd);
    EXPECT_FALSE(result.allowDelete);
    EXPECT_FALSE(result.allowReplace);
    EXPECT_TRUE(result.isReadOnly());
}

TEST(ApplyStorageConfigIni, FolderStorageClassConcatenatesFileNameWithoutADot) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // u_TileStorageTypeBase.pas: tstcFolder/tstcInSeparateFiles => APath + 'StorageConfig.ini',
    // `path` already ending with a separator.
    const QString cachePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
    ASSERT_TRUE(QDir().mkpath(cachePath));
    writeFile(cachePath + QStringLiteral("StorageConfig.ini"), QStringLiteral("[Common]\r\nAllowAdd=0\r\n"));

    const TileStorageAbilities result =
        applyStorageConfigIni(TileStorageAbilities::readWrite(), TileStorageAbilities::readWrite(),
                              StorageClass::Folder, cachePath);

    EXPECT_FALSE(result.allowAdd);
    EXPECT_TRUE(result.allowDelete);
}

TEST(ApplyStorageConfigIni, InMemoryStorageClassNeverLooksForAnIniFile) {
    // No path composition happens at all for InMemory/Other -- only the forced pass narrows.
    const TileStorageAbilities result =
        applyStorageConfigIni(TileStorageAbilities::readWrite(), TileStorageAbilities::readOnly(),
                              StorageClass::InMemory, QStringLiteral("/does/not/matter"));

    EXPECT_TRUE(result.allowRead);
    EXPECT_TRUE(result.allowScan);
    EXPECT_FALSE(result.allowAdd);
    EXPECT_FALSE(result.allowDelete);
    EXPECT_FALSE(result.allowReplace);
}

} // namespace

// u_TileStorageTypeFileSystemSimple.pas:73-91 -- TTileStorageTypeAbilities.Create(
// CTileStorageReadWrite, tstvsVersionIgnored, False, stsUnicode, tstcInSeparateFiles).
TEST(FileSystemTileStorageType, AdvertisesLegacyAbilities) {
    const FileSystemTileStorageType type(std::make_shared<SasTileNameCodec>(),
                                         std::make_shared<MapVersionFactory>(), nullptr);
    const auto abilities = type.abilities();

    EXPECT_EQ(abilities.base, TileStorageAbilities::readWrite());
    EXPECT_EQ(abilities.version, VersionSupport::Ignored);
    EXPECT_FALSE(abilities.supportsMixedContentTypes);
    EXPECT_EQ(abilities.storageClass, StorageClass::InSeparateFiles);
}

// u_TileStorageTypeInRAM.pas:73-80 -- TTileStorageTypeAbilities.Create(
// CTileStorageReadWrite - [tsatScan], tstvsVersionStored, True, stsUnicode, tstcInMemory).
TEST(RamTileStorageType, AdvertisesLegacyAbilities) {
    const RamTileStorageType type(std::make_shared<MapVersionFactory>(), nullptr);
    const auto abilities = type.abilities();

    EXPECT_TRUE(abilities.base.allowRead);
    EXPECT_FALSE(abilities.base.allowScan); // CTileStorageReadWrite - [tsatScan] (legacy defect L-31:
                                            // RamTileStorage::scanTiles itself always works)
    EXPECT_TRUE(abilities.base.allowAdd);
    EXPECT_TRUE(abilities.base.allowDelete);
    EXPECT_TRUE(abilities.base.allowReplace);
    EXPECT_EQ(abilities.version, VersionSupport::Stored);
    EXPECT_TRUE(abilities.supportsMixedContentTypes);
    EXPECT_EQ(abilities.storageClass, StorageClass::InMemory);
}

// Ticket's own acceptance case: a filesystem type with a read-only StorageConfig.ini builds a
// storage that refuses a save.
TEST(FileSystemTileStorageType, BuildsAStorageThatRefusesSaveUnderReadOnlyStorageConfigIni) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString cachePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
    ASSERT_TRUE(QDir().mkpath(cachePath));
    writeFile(cachePath + QStringLiteral("StorageConfig.ini"),
              QStringLiteral("[Common]\r\nIsReadOnly=1\r\n"));

    FileSystemTileStorageType type(std::make_shared<SasTileNameCodec>(),
                                   std::make_shared<MapVersionFactory>(), nullptr);
    const auto built = type.build(TileStorageAbilities::readWrite(), makeProjectionSet(), jpegContentType(),
                                  std::make_shared<TilePyramidNotifier>(), cachePath, nullptr);

    ASSERT_TRUE(built.has_value());
    const ITileStoragePtr& storage = *built;
    ASSERT_NE(storage, nullptr);
    EXPECT_TRUE(storage->state()->readAccess);
    EXPECT_FALSE(storage->state()->addAccess); // narrowed to read-only by StorageConfig.ini

    const auto saved = storage->saveTile(PointI{0, 0}, 1, nullptr, fromPosixSeconds(1700000000),
                                         QByteArrayView("image/jpg"), QByteArray("tile bytes"), true);
    ASSERT_TRUE(saved.has_value());
    EXPECT_FALSE(*saved); // refused: addAccess is false
}

// "Builds a working storage" case for the RAM type: build() forwards a real mem cache through to
// RamTileStorage (u_TileStorageTypeInRAM.pas:98-107) and the result actually saves/loads a tile.
// Also covers the rework's blocker finding: two storages built by the same type with the SAME
// caller-supplied notifier deliver their save notification to one subscriber registered on that
// notifier (u_TileStorageOfMapType.pas:214,285,327 -- subscribers survive a backend swap because
// the facade passes one notifier into every build()).
TEST(RamTileStorageType, BuildsAWorkingStorage) {
    RamTileStorageType type(std::make_shared<MapVersionFactory>(), nullptr);
    auto memCache =
        std::make_shared<TileInfoMemCache>(100, std::chrono::milliseconds(60'000), ClearStrategy::Never);
    const auto sharedNotifier = std::make_shared<TilePyramidNotifier>();

    const auto built = type.build(TileStorageAbilities::readWrite(), makeProjectionSet(), jpegContentType(),
                                  sharedNotifier, QString(), memCache);

    ASSERT_TRUE(built.has_value());
    const ITileStoragePtr& storage = *built;
    ASSERT_NE(storage, nullptr);

    const auto built2 = type.build(TileStorageAbilities::readWrite(), makeProjectionSet(), jpegContentType(),
                                   sharedNotifier, QString(), memCache);
    ASSERT_TRUE(built2.has_value());
    const ITileStoragePtr& storage2 = *built2;
    ASSERT_NE(storage2, nullptr);

    int notified = 0;
    const auto subscription = sharedNotifier->subscribe([&notified](const auto&) { ++notified; });

    const PointI xy{3, 7};
    constexpr quint8 zoom = 5;
    const QByteArray data("ram tile bytes");
    const auto saved = storage->saveTile(xy, zoom, nullptr, fromPosixSeconds(1700000000),
                                         QByteArrayView("image/jpg"), data, true);
    ASSERT_TRUE(saved.has_value());
    EXPECT_TRUE(*saved);
    EXPECT_EQ(notified, 1); // the subscriber registered on the shared notifier saw storage's save

    const auto saved2 = storage2->saveTile(xy, zoom, nullptr, fromPosixSeconds(1700000000),
                                           QByteArrayView("image/jpg"), data, true);
    ASSERT_TRUE(saved2.has_value());
    EXPECT_TRUE(*saved2);
    EXPECT_EQ(notified, 2); // and storage2's save too -- same notifier, one subscriber

    const auto info = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithData);
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->isExists());
    EXPECT_EQ(info->data, data);
}
