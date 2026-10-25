// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-026. Ports u_TileStorageInRAM.pas; docs/specs/storage.md §2.14 (:1092-1100).
//
// Test-count ruling (tests/conventions/tests-port-dont-multiply): legacy has no DUnit suite for
// this class, so this is the ticket's own minimal list: save/info/load/delete round trip, TNE,
// versions honoured (two versions of one tile), scan refused, a null mem-cache constructor
// rejected, the content-type compatibility rule (one compatible + one incompatible row), and the
// add/delete access gates.
#include "cg/core/content_type.h"
#include "cg/core/hash.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/storage/map_version.h"
#include "cg/storage/ram_tile_storage.h"
#include "cg/storage/tile_date.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_pyramid_notifier.h"

#include <QByteArray>
#include <QByteArrayView>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stop_token>
#include <vector>

#include "storage_state_changeable_check.h"

using cg::core::ContentKind;
using cg::core::ContentTypeInfo;
using cg::core::ErrorCode;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::PointI;
using cg::geo::ProjectionSetFactory;
using cg::geo::ProjectionSetPtr;
using cg::storage::ClearStrategy;
using cg::storage::fromPosixSeconds;
using cg::storage::LoadMode;
using cg::storage::MapVersionFactory;
using cg::storage::MapVersionInfo;
using cg::storage::MapVersionInfoPtr;
using cg::storage::MapVersionRequest;
using cg::storage::RamTileStorage;
using cg::storage::RamTileStorageMemCacheRequired;
using cg::storage::TileDate;
using cg::storage::TileInfoMemCache;
using cg::storage::TileInfoType;
using cg::storage::TilePyramidNotifier;
using cg::storage::TileStorageAbilities;
using cg::storage::TileStorageKind;
using cg::storage::tileStorageTypeAbilities;

namespace {

ProjectionSetPtr makeProjectionSet() {
    static const auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    static const ProjectionSetFactory factory{HashSeed{}, datums};
    return factory.byCode(kProjEpsgGoogle, kTileSplitQuadrate256x256);
}

std::shared_ptr<TileInfoMemCache> makeMemCache() {
    return std::make_shared<TileInfoMemCache>(100, std::chrono::milliseconds(60'000), ClearStrategy::Never);
}

MapVersionInfoPtr version(const QString& v) {
    return std::make_shared<const MapVersionInfo>(v, cg::core::kNoHash);
}

std::unique_ptr<RamTileStorage>
makeStorage(TileStorageAbilities resolvedAbilities,
            ContentTypeInfo mainContentType = ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap},
            std::shared_ptr<TileInfoMemCache> memCache = nullptr) {
    return std::make_unique<RamTileStorage>(
        tileStorageTypeAbilities(TileStorageKind::Ram), resolvedAbilities,
        std::make_shared<MapVersionFactory>(), makeProjectionSet(), std::make_shared<TilePyramidNotifier>(),
        std::move(mainContentType), memCache ? std::move(memCache) : makeMemCache());
}

} // namespace

TEST(RamTileStorage, SaveLoadDeleteRoundTrip) {
    auto storage = makeStorage(tileStorageTypeAbilities(TileStorageKind::Ram).base);

    const PointI xy{3, 7};
    constexpr quint8 zoom = 5;
    const QByteArray data("ram tile bytes");
    const TileDate loadDate = fromPosixSeconds(1700000000);

    auto saved = storage->saveTile(xy, zoom, nullptr, loadDate, QByteArrayView("image/jpg"), data, false);
    ASSERT_TRUE(saved.has_value());
    EXPECT_TRUE(*saved);

    EXPECT_EQ(storage->tileFileName(xy, zoom, nullptr), QStringLiteral(":: RAM"));

    auto info = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithData);
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->isExists());
    EXPECT_EQ(info->data, data);
    EXPECT_EQ(info->contentType, QByteArray("image/jpg"));
    EXPECT_EQ(info->loadDate, loadDate);

    // Without overwrite, a second save of an existing tile is refused (Result{false}, not an error).
    auto refused = storage->saveTile(xy, zoom, nullptr, loadDate, QByteArrayView("image/jpg"), data, false);
    ASSERT_TRUE(refused.has_value());
    EXPECT_FALSE(*refused);

    auto deleted = storage->deleteTile(xy, zoom, nullptr);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_TRUE(*deleted);

    auto afterDelete = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithoutData);
    ASSERT_TRUE(afterDelete.has_value());
    EXPECT_EQ(afterDelete->type, TileInfoType::NotExists);
}

TEST(RamTileStorage, TneWriteAndReadIsTneExists) {
    auto storage = makeStorage(tileStorageTypeAbilities(TileStorageKind::Ram).base);

    const PointI xy{1, 1};
    constexpr quint8 zoom = 2;
    const TileDate loadDate = fromPosixSeconds(1700000000);

    // Empty content type + null data => TNE (ITileStorage::saveTile contract).
    auto saved = storage->saveTile(xy, zoom, nullptr, loadDate, QByteArrayView(), QByteArray(), false);
    ASSERT_TRUE(saved.has_value());
    EXPECT_TRUE(*saved);

    auto info = storage->tileInfo(xy, zoom, nullptr, LoadMode::AsIs);
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->isExistsTne());
    EXPECT_EQ(info->size, 0u);
}

TEST(RamTileStorage, VersionsHonouredTwoVersionsOfOneTile) {
    auto storage = makeStorage(tileStorageTypeAbilities(TileStorageKind::Ram).base);

    const PointI xy{4, 4};
    constexpr quint8 zoom = 3;
    const MapVersionInfoPtr v1 = version(QStringLiteral("v1"));
    const MapVersionInfoPtr v2 = version(QStringLiteral("v2"));
    const TileDate loadDate = fromPosixSeconds(1700000000);

    auto s1 =
        storage->saveTile(xy, zoom, v1, loadDate, QByteArrayView("image/jpg"), QByteArray("v1-bytes"), false);
    ASSERT_TRUE(s1.has_value());
    EXPECT_TRUE(*s1);
    auto s2 =
        storage->saveTile(xy, zoom, v2, loadDate, QByteArrayView("image/jpg"), QByteArray("v2-bytes"), false);
    ASSERT_TRUE(s2.has_value());
    EXPECT_TRUE(*s2);

    auto info1 = storage->tileInfo(xy, zoom, v1, LoadMode::WithData);
    ASSERT_TRUE(info1.has_value());
    EXPECT_TRUE(info1->isExists());
    EXPECT_EQ(info1->data, QByteArray("v1-bytes"));

    auto info2 = storage->tileInfo(xy, zoom, v2, LoadMode::WithData);
    ASSERT_TRUE(info2.has_value());
    EXPECT_EQ(info2->data, QByteArray("v2-bytes"));

    // A third, never-saved version is a genuine miss, not aliased onto v1/v2.
    auto info3 = storage->tileInfo(xy, zoom, version(QStringLiteral("v3")), LoadMode::WithData);
    ASSERT_TRUE(info3.has_value());
    EXPECT_EQ(info3->type, TileInfoType::NotExists);

    // tileInfoEx forwards MapVersionRequest::baseVersion (GetTileInfoEx's `AVersionInfo.BaseVersion`).
    MapVersionRequest request;
    request.baseVersion = v2;
    auto infoEx = storage->tileInfoEx(xy, zoom, request, LoadMode::WithData);
    ASSERT_TRUE(infoEx.has_value());
    EXPECT_EQ(infoEx->data, QByteArray("v2-bytes"));
}

TEST(RamTileStorage, ScanTilesReturnsMemCacheEnumerator) {
    // u_TileStorageInRAM.pas:335-340 has no access gate at all: ScanTiles always returns
    // `FTileInfoMemCache.GetEnum(...)`. The RAM type's abilities fixing `allowScan=false`
    // (tile_storage_abilities.cpp) is only what the RAM storage TYPE advertises to callers that
    // consult abilities; it does not gate this method.
    auto storage = makeStorage(tileStorageTypeAbilities(TileStorageKind::Ram).base);

    const PointI xy{6, 6};
    constexpr quint8 zoom = 4;
    const QByteArray data("scan me");
    auto saved = storage->saveTile(xy, zoom, nullptr, fromPosixSeconds(1700000000),
                                   QByteArrayView("image/jpg"), data, false);
    ASSERT_TRUE(saved.has_value());
    EXPECT_TRUE(*saved);

    auto scanned = storage->scanTiles(/*ignoreTne=*/false, /*ignoreMultiVersionTiles=*/false);
    ASSERT_TRUE(scanned.has_value());
    ASSERT_NE(*scanned, nullptr);

    bool found = false;
    cg::storage::TileInfo info;
    PointI outXy;
    quint8 outZoom = 0;
    while ((*scanned)->next(info, outXy, outZoom)) {
        if (outXy == xy && outZoom == zoom) {
            found = true;
            EXPECT_TRUE(info.isExists());
            EXPECT_EQ(info.data, data);
        }
    }
    EXPECT_TRUE(found);
}

TEST(RamTileStorage, TileRectInfoOverTheCapIsNullNotAnError) {
    // u_TileStorageInRAM.pas:229: a rect over 2048 tiles leaves Result = nil; spec §2.6 note 6.
    auto storage = makeStorage(tileStorageTypeAbilities(TileStorageKind::Ram).base);
    const auto huge =
        storage->tileRectInfo(cg::geo::RectI{0, 0, 2049, 1}, 13, MapVersionRequest{}, std::stop_token{});
    ASSERT_TRUE(huge.has_value());
    EXPECT_EQ(*huge, nullptr);
}

TEST(RamTileStorage, NoReadAccessStillAnswersNotExists) {
    // u_TileStorageInRAM.pas:173-184: no access test; a miss is the not-exists object, not nil (D-106).
    auto storage = makeStorage(TileStorageAbilities::none());
    auto info = storage->tileInfo(PointI{1, 2}, 3, nullptr, LoadMode::AsIs);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, TileInfoType::NotExists);
}

TEST(RamTileStorage, DestructorClearsMemCacheCallback) {
    // Regression for u_TileStorageInRAM.pas:158-162 (`FTileInfoMemCache.OnTileInfoUpdate := nil`
    // in Destroy): the mem cache is caller-owned and can outlive the storage, so the constructor's
    // `[this]` callback (ram_tile_storage.cpp) must be cleared on destruction or a later add()/evict
    // on the shared cache calls into the destroyed storage (ASan heap-use-after-free otherwise).
    auto memCache = makeMemCache();
    auto notifier = std::make_shared<TilePyramidNotifier>();

    int notifyCount = 0;
    auto subscription =
        notifier->subscribe([&notifyCount](const cg::storage::TileUpdate*) { ++notifyCount; });

    auto storage = std::make_unique<RamTileStorage>(
        tileStorageTypeAbilities(TileStorageKind::Ram), tileStorageTypeAbilities(TileStorageKind::Ram).base,
        std::make_shared<MapVersionFactory>(), makeProjectionSet(), notifier,
        ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap}, memCache);
    storage.reset(); // destroy the storage; memCache and notifier both outlive it

    // Trigger the mem cache's update path directly (what legacy's SaveTile/DeleteTile funnel
    // through FTileInfoMemCache.Add/Remove -> OnTileInfoUpdate). Must not touch the destroyed
    // storage: no crash (ASan-clean) and the destroyed storage's callback must not have fired.
    cg::storage::TileInfo info;
    info.type = TileInfoType::Exists;
    info.data = QByteArray("bytes");
    info.contentType = QByteArray("image/jpg");
    memCache->add(PointI{9, 9}, 1, nullptr, info);

    EXPECT_EQ(notifyCount, 0);
}

TEST(RamTileStorage, NullMemCacheConstructorThrows) {
    EXPECT_THROW((RamTileStorage(tileStorageTypeAbilities(TileStorageKind::Ram),
                                 tileStorageTypeAbilities(TileStorageKind::Ram).base,
                                 std::make_shared<MapVersionFactory>(), makeProjectionSet(),
                                 std::make_shared<TilePyramidNotifier>(),
                                 ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap}, nullptr)),
                 RamTileStorageMemCacheRequired);
}

TEST(RamTileStorage, SaveTileContentTypeCompatibility) {
    struct Case {
        const char* name;
        const char* mainType;
        const char* incomingType;
        bool compatible;
    };
    const std::vector<Case> cases = {
        {"JpegAliasIntoJpgStorageAccepted", "image/jpg", "image/jpeg", true},
        {"PngIntoJpgStorageRejected", "image/jpg", "image/png", false},
    };

    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        auto storage = makeStorage(tileStorageTypeAbilities(TileStorageKind::Ram).base,
                                   ContentTypeInfo{c.mainType, ".jpg", ContentKind::Bitmap});
        auto saved = storage->saveTile(PointI{0, 0}, 1, nullptr, fromPosixSeconds(1700000000),
                                       QByteArrayView(c.incomingType), QByteArray("bytes"), false);
        if (c.compatible) {
            ASSERT_TRUE(saved.has_value());
            EXPECT_TRUE(*saved);
        } else {
            ASSERT_FALSE(saved.has_value());
            EXPECT_EQ(saved.error().code(), ErrorCode::Unsupported);
        }
    }
}

TEST(RamTileStorage, AddAndDeleteAccessGating) {
    TileStorageAbilities abilities = tileStorageTypeAbilities(TileStorageKind::Ram).base;
    abilities.allowAdd = false;
    abilities.allowDelete = false;
    auto storage = makeStorage(abilities);

    auto saved = storage->saveTile(PointI{2, 2}, 1, nullptr, fromPosixSeconds(1700000000),
                                   QByteArrayView("image/jpg"), QByteArray("bytes"), false);
    ASSERT_TRUE(saved.has_value());
    EXPECT_FALSE(*saved);

    auto deleted = storage->deleteTile(PointI{2, 2}, 1, nullptr);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_FALSE(*deleted);
}

// T-0117: stateChangeable() is the RAM storage's own state holder.
TEST(RamTileStorage, StateChangeableIsOwnHolder) {
    auto storage = makeStorage(TileStorageAbilities::readWrite());
    cg::storage::test::expectStateChangeableIsOwn(*storage, true);
}
