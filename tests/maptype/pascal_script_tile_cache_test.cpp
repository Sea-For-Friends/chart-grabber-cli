// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// PascalScriptTileCache (u_PascalScriptTileCache.pas)
#include "cg/core/content_type.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/maptype/pascal_script_tile_cache.h"
#include "cg/storage/codecs/sas_tile_name_codec.h"
#include "cg/storage/file_system_tile_storage.h"
#include "cg/storage/tile_pyramid_notifier.h"

#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <deque>
#include <vector>

using namespace cg;
using cg::maptype::PascalScriptTileCache;

namespace {

geo::ProjectionSetPtr makeProjectionSet() {
    static const auto datums = std::make_shared<const geo::DatumFactory>(geo::HashSeed{});
    static const geo::ProjectionSetFactory factory{geo::HashSeed{}, datums};
    return factory.byCode(geo::kProjEpsgGoogle, geo::kTileSplitQuadrate256x256);
}

// Forwards to a real folder storage and records the load mode of every tileInfo call.
class SpyStorage final : public storage::ITileStorage {
public:
    SpyStorage(const QString& path, storage::MapVersionFactoryPtr versions)
        : real_(storage::tileStorageTypeAbilities(storage::TileStorageKind::FolderSimple),
                storage::TileStorageAbilities::readWrite(), std::move(versions), makeProjectionSet(),
                std::make_shared<storage::TilePyramidNotifier>(), path,
                core::ContentTypeInfo{"image/jpg", ".jpg", core::ContentKind::Bitmap},
                std::make_shared<storage::SasTileNameCodec>(), nullptr) {}

    [[nodiscard]] storage::TileStorageTypeAbilities typeAbilities() const override {
        return real_.typeAbilities();
    }
    [[nodiscard]] storage::StorageStatePtr state() const override { return real_.state(); }
    [[nodiscard]] core::Changeable& stateChangeable() override { return real_.stateChangeable(); }
    [[nodiscard]] const geo::IProjectionSet& projectionSet() const override { return real_.projectionSet(); }
    [[nodiscard]] storage::TilePyramidNotifier& tileNotifier() override { return real_.tileNotifier(); }
    [[nodiscard]] QString tileFileName(geo::PointI xy, quint8 zoom,
                                       const storage::MapVersionInfoPtr& v) const override {
        return real_.tileFileName(xy, zoom, v);
    }
    [[nodiscard]] core::Result<storage::TileInfo> tileInfo(geo::PointI xy, quint8 zoom,
                                                           const storage::MapVersionInfoPtr& version,
                                                           storage::LoadMode mode) const override {
        modes.push_back(mode);
        if (!answers.empty()) {
            auto answer = std::move(answers.front());
            answers.pop_front();
            return answer;
        }
        return real_.tileInfo(xy, zoom, version, mode);
    }
    [[nodiscard]] core::Result<storage::TileInfo> tileInfoEx(geo::PointI xy, quint8 zoom,
                                                             const storage::MapVersionRequest& r,
                                                             storage::LoadMode mode) const override {
        return real_.tileInfoEx(xy, zoom, r, mode);
    }
    [[nodiscard]] core::Result<bool> deleteTile(geo::PointI xy, quint8 zoom,
                                                const storage::MapVersionInfoPtr& v) override {
        if (failWrites) {
            return core::fail(core::ErrorCode::IoError, QStringLiteral("disk full"));
        }
        return real_.deleteTile(xy, zoom, v);
    }
    [[nodiscard]] core::Result<bool> saveTile(geo::PointI xy, quint8 zoom,
                                              const storage::MapVersionInfoPtr& v, storage::TileDate date,
                                              QByteArrayView type, const QByteArray& data,
                                              bool overwrite) override {
        savedVersions.push_back(v ? v->storeString() : QString());
        if (failWrites) {
            return core::fail(core::ErrorCode::IoError, QStringLiteral("disk full"));
        }
        return real_.saveTile(xy, zoom, v, std::move(date), type, data, overwrite);
    }
    [[nodiscard]] core::Result<storage::MapVersionList>
    listVersions(geo::PointI xy, quint8 zoom, const storage::MapVersionRequest& r) const override {
        return real_.listVersions(xy, zoom, r);
    }
    [[nodiscard]] core::Result<storage::TileRectInfoPtr> tileRectInfo(geo::RectI rect, quint8 zoom,
                                                                      const storage::MapVersionRequest& r,
                                                                      std::stop_token stop) const override {
        return real_.tileRectInfo(rect, zoom, r, stop);
    }
    [[nodiscard]] core::Result<storage::EnumTileInfoPtr> scanTiles(bool a, bool b) override {
        return real_.scanTiles(a, b);
    }

    storage::FileSystemTileStorage& real() { return real_; }

    mutable std::vector<storage::LoadMode> modes;
    mutable std::deque<core::Result<storage::TileInfo>> answers; // scripted tileInfo answers, first
    bool failWrites = false;
    std::vector<QString> savedVersions; // store strings handed to saveTile

private:
    storage::FileSystemTileStorage real_;
};

struct Fixture {
    QTemporaryDir dir;
    std::shared_ptr<SpyStorage> storage;
    std::unique_ptr<PascalScriptTileCache> cache;
    Fixture() {
        auto versions = std::make_shared<storage::MapVersionFactory>();
        storage =
            std::make_shared<SpyStorage>(QDir(dir.path()).filePath(QStringLiteral("cache")) + u'/', versions);
        cache = std::make_unique<PascalScriptTileCache>(
            storage, versions,
            std::shared_ptr<const core::ContentTypeRegistry>(std::shared_ptr<void>(),
                                                             &core::ContentTypeRegistry::standard()));
    }
};

} // namespace

TEST(PascalScriptTileCache, WriteReadWithAndWithoutDataTneAndDelete) {
    Fixture f;
    ASSERT_TRUE(f.dir.isValid());
    const QByteArray data("jpegbytes");

    EXPECT_TRUE(f.cache->write(3, 7, 5, "v1", "image/jpg", data, false));
    {
        // checked in the storage itself, not through the class under test
        auto stored = f.storage->real().tileInfo({3, 7}, 5, nullptr, storage::LoadMode::WithData);
        ASSERT_TRUE(stored.has_value());
        EXPECT_TRUE(stored->isExists());
        EXPECT_EQ(stored->data, data);
    }
    EXPECT_FALSE(f.cache->write(3, 7, 5, "v1", "image/jpg", "other", false)); // exists, no overwrite

    auto withData = f.cache->read(3, 7, 5, "v1", true);
    EXPECT_TRUE(withData.isExists);
    EXPECT_FALSE(withData.isExistsTne);
    EXPECT_TRUE(withData.loadDate.isValid());
    EXPECT_EQ(withData.size, data.size());
    EXPECT_EQ(withData.contentType, QByteArray("image/jpg"));
    EXPECT_EQ(withData.data, data);

    auto noData = f.cache->read(3, 7, 5, "v1", false);
    EXPECT_TRUE(noData.isExists);
    EXPECT_EQ(noData.size, data.size());
    EXPECT_TRUE(noData.data.isEmpty());

    EXPECT_TRUE(f.cache->remove(3, 7, 5, "v1"));
    auto gone = f.cache->read(3, 7, 5, "v1", true);
    EXPECT_FALSE(gone.isExists);
    EXPECT_FALSE(gone.isExistsTne);
    EXPECT_FALSE(gone.loadDate.isValid());

    EXPECT_TRUE(f.cache->writeTne(3, 7, 5, "v1"));
    auto tne = f.cache->read(3, 7, 5, "v1", true);
    EXPECT_FALSE(tne.isExists);
    EXPECT_TRUE(tne.isExistsTne);
    EXPECT_TRUE(tne.loadDate.isValid());
    EXPECT_EQ(tne.size, 0);
    EXPECT_TRUE(tne.contentType.isEmpty());
    EXPECT_TRUE(tne.data.isEmpty());

    EXPECT_TRUE(f.cache->remove(3, 7, 5, "v1"));
    EXPECT_FALSE(f.cache->read(3, 7, 5, "v1", false).isExistsTne);
}

TEST(PascalScriptTileCache, MissWithDataAsksAgainWithoutData) {
    Fixture f;
    (void)f.cache->read(1, 1, 3, "", true);
    ASSERT_EQ(f.storage->modes.size(), 2u);
    EXPECT_EQ(f.storage->modes[0], storage::LoadMode::WithData);
    EXPECT_EQ(f.storage->modes[1], storage::LoadMode::WithoutData);

    f.storage->modes.clear();
    (void)f.cache->read(1, 1, 3, "", false);
    ASSERT_EQ(f.storage->modes.size(), 1u);
    EXPECT_EQ(f.storage->modes[0], storage::LoadMode::WithoutData);

    ASSERT_TRUE(f.cache->write(1, 1, 3, "", "image/jpg", "x", true));
    f.storage->modes.clear();
    (void)f.cache->read(1, 1, 3, "", true); // a hit is one lookup
    EXPECT_EQ(f.storage->modes.size(), 1u);
}

TEST(PascalScriptTileCache, WriteOfAnUnknownContentTypeThrowsLegacyText) {
    Fixture f;
    try {
        f.cache->write(1, 1, 3, "", "image/nope", "x", true);
        FAIL() << "no exception";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "Unknown Content-Type: image/nope");
    }
}

TEST(PascalScriptTileCache, EmptyDataWritesAZeroByteTileNotATne) {
    Fixture f;
    EXPECT_TRUE(f.cache->write(2, 2, 4, "", "image/jpg", QByteArray(), true)); // a null array
    auto stored = f.storage->real().tileInfo({2, 2}, 4, nullptr, storage::LoadMode::WithData);
    ASSERT_TRUE(stored.has_value());
    EXPECT_TRUE(stored->isExists());
    EXPECT_FALSE(stored->isExistsTne());
    EXPECT_EQ(stored->size, 0u);
}

TEST(PascalScriptTileCache, StorageFailureIsThrownWithTheStorageMessage) {
    Fixture f;
    f.storage->failWrites = true;
    for (int op = 0; op < 3; ++op) {
        try {
            if (op == 0) {
                (void)f.cache->write(1, 1, 3, "", "image/jpg", "x", true);
            } else if (op == 1) {
                (void)f.cache->writeTne(1, 1, 3, "");
            } else {
                (void)f.cache->remove(1, 1, 3, "");
            }
            FAIL() << "no exception, op " << op;
        } catch (const std::runtime_error& e) {
            EXPECT_STREQ(e.what(), "disk full");
        }
    }
}

namespace {

storage::TileInfo infoOf(storage::TileInfoType type, quint32 size = 0, const QByteArray& mime = {},
                         storage::MapVersionInfoPtr version = nullptr) {
    storage::TileInfo info;
    info.type = type;
    info.size = size;
    info.contentType = mime;
    info.version = std::move(version);
    if (type != storage::TileInfoType::NotExists) {
        info.loadDate = storage::fromPosixSeconds(1700000000);
    }
    return info;
}

} // namespace

// Reproduced legacy defect (u_PascalScriptTileCache.pas:150-179): the second answer replaces the
// first for everything but IsExists, so a tile that appears between the two lookups is reported
// as "not exists" with its size and content type filled.
TEST(PascalScriptTileCache, SecondAnswerFeedsEveryFieldButIsExists) {
    Fixture f;
    f.storage->answers.push_back(infoOf(storage::TileInfoType::NotExists));
    f.storage->answers.push_back(infoOf(storage::TileInfoType::Exists, 5, "image/png"));
    auto r = f.cache->read(1, 1, 3, "", true);
    EXPECT_FALSE(r.isExists);
    EXPECT_FALSE(r.isExistsTne);
    EXPECT_EQ(r.size, 5);
    EXPECT_EQ(r.contentType, QByteArray("image/png"));
    EXPECT_TRUE(r.loadDate.isValid()); // also from the second answer
}

TEST(PascalScriptTileCache, SecondAnswerSuppliesTneAndItsLoadDate) {
    Fixture f;
    f.storage->answers.push_back(infoOf(storage::TileInfoType::NotExists));
    f.storage->answers.push_back(infoOf(storage::TileInfoType::TneExists));
    auto r = f.cache->read(1, 1, 3, "", true);
    EXPECT_FALSE(r.isExists);
    EXPECT_TRUE(r.isExistsTne);
    EXPECT_TRUE(r.loadDate.isValid());

    f.storage->answers.push_back(infoOf(storage::TileInfoType::NotExists));
    f.storage->answers.push_back(core::fail(core::ErrorCode::IoError, QStringLiteral("x")));
    EXPECT_FALSE(
        f.cache->read(1, 1, 3, "", true).isExistsTne); // second answer missing: what is filled so far
}

TEST(PascalScriptTileCache, ReturnedVersionIsTheStoreStringAsUtf8) {
    Fixture f;
    auto versions = std::make_shared<storage::MapVersionFactory>();
    const QString ver = QStringLiteral("v\u00e9\u0416");
    f.storage->answers.push_back(
        infoOf(storage::TileInfoType::Exists, 1, "image/jpg", versions->byStoreString(ver)));
    EXPECT_EQ(f.cache->read(1, 1, 3, ver.toUtf8(), false).version, ver.toUtf8());
    EXPECT_TRUE(f.cache->write(1, 1, 3, ver.toUtf8(), "image/jpg", "x", true));
    ASSERT_EQ(f.storage->savedVersions.size(), 1u);
    EXPECT_EQ(f.storage->savedVersions[0], ver);
}

// u_PascalScriptTileCache.pas:143-145: a nil answer (Unknown) is the empty record after ONE storage call.
// The Unknown value carries a version, so a port that read on would show it in the result.
TEST(PascalScriptTileCache, NilAnswerIsTheEmptyRecordAfterOneCall) {
    Fixture f;
    auto versions = std::make_shared<storage::MapVersionFactory>();
    f.storage->answers.push_back(
        infoOf(storage::TileInfoType::Unknown, 0, {}, versions->byStoreString(QStringLiteral("v9"))));
    const auto r = f.cache->read(1, 1, 3, "", true);
    EXPECT_FALSE(r.isExists);
    EXPECT_FALSE(r.isExistsTne);
    EXPECT_TRUE(r.version.isEmpty());
    EXPECT_EQ(f.storage->modes.size(), 1u); // no second WithoutData request
}

// :152-154: a nil second answer ends the read with what is filled so far (the first answer filled nothing).
TEST(PascalScriptTileCache, NilSecondAnswerEndsTheRead) {
    Fixture f;
    auto versions = std::make_shared<storage::MapVersionFactory>();
    f.storage->answers.push_back(infoOf(storage::TileInfoType::NotExists));
    f.storage->answers.push_back(
        infoOf(storage::TileInfoType::Unknown, 0, {}, versions->byStoreString(QStringLiteral("v9"))));
    const auto r = f.cache->read(1, 1, 3, "", true);
    EXPECT_FALSE(r.isExists);
    EXPECT_TRUE(r.version.isEmpty());
    EXPECT_EQ(f.storage->modes.size(), 2u);
}
