// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Test rig of T-0190 (tile_estimate_test.cpp, fill_report_test.cpp): a scripted tile storage that counts and
// records every lookup, a map type around it (Google/3857, 256 px tiles), and one real AppContext for the
// geometry and tile-iterator factories.
// Why not the RAM cache of a ZMP folder: the ticket needs the storage calls counted and an error / stop
// injected at a chosen call; the scripted storage gives that and exact TileInfo types (Exists, TneExists,
// NotExists, Unknown) without a ZMP folder.

#include "cg/app/app_context.h"
#include "cg/geo/datum.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/geo/tile_count.h"
#include "cg/maptype/map_version_factory_changeable.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/map_version_request_config.h"
#include "cg/storage/tile_storage.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "../maptype/maptype_test_util.h"
#include "../region/region_fake_map_type.h"

namespace cg::app::test {

struct StorageCall {
    int zoom = 0;
    int x = 0;
    int y = 0;
    storage::LoadMode mode = storage::LoadMode::AsIs;
    storage::MapVersionInfoPtr baseVersion;
};

inline storage::TileInfo presentTile(quint32 size) {
    storage::TileInfo info;
    info.type = storage::TileInfoType::Exists;
    info.size = size;
    return info;
}

inline storage::TileInfo markerTile() {
    storage::TileInfo info;
    info.type = storage::TileInfoType::TneExists;
    return info;
}

inline storage::TileInfo notExistsTile() {
    storage::TileInfo info;
    info.type = storage::TileInfoType::NotExists;
    return info;
}

class ScriptedStorage final : public storage::ITileStorage {
public:
    std::map<std::tuple<int, int, int>, storage::TileInfo> infos; // (zoom, x, y); absent = Unknown
    int failAtCall = 0;                                           // 1-based ordinal of the call that fails
    std::function<void(int)> onCall; // called with the ordinal, before the answer
    mutable std::vector<StorageCall> calls;

    void put(int zoom, int x, int y, const storage::TileInfo& info) { infos[{zoom, x, y}] = info; }

    [[nodiscard]] core::Result<storage::TileInfo> tileInfoEx(geo::PointI xy, quint8 zoom,
                                                             const storage::MapVersionRequest& request,
                                                             storage::LoadMode mode) const override {
        calls.push_back(StorageCall{zoom, xy.x(), xy.y(), mode, request.baseVersion});
        const int ordinal = static_cast<int>(calls.size());
        if (onCall) {
            onCall(ordinal);
        }
        if (ordinal == failAtCall) {
            return core::fail(core::ErrorCode::IoError,
                              QStringLiteral("scripted failure at call %1").arg(ordinal));
        }
        const auto it = infos.find({zoom, xy.x(), xy.y()});
        return it == infos.end() ? storage::TileInfo{} : it->second;
    }

    [[nodiscard]] storage::TileStorageTypeAbilities typeAbilities() const override { unused(); }
    [[nodiscard]] storage::StorageStatePtr state() const override { unused(); }
    [[nodiscard]] core::Changeable& stateChangeable() override { unused(); }
    [[nodiscard]] const geo::IProjectionSet& projectionSet() const override { unused(); }
    [[nodiscard]] storage::TilePyramidNotifier& tileNotifier() override { unused(); }
    [[nodiscard]] QString tileFileName(geo::PointI, quint8,
                                       const storage::MapVersionInfoPtr&) const override {
        unused();
    }
    [[nodiscard]] core::Result<storage::TileInfo>
    tileInfo(geo::PointI, quint8, const storage::MapVersionInfoPtr&, storage::LoadMode) const override {
        unused();
    }
    // T-0246 (snapshot_prune_test.cpp): scripted versions per tile and a record of the deletions
    std::map<std::tuple<int, int, int>, std::vector<storage::MapVersionInfoPtr>>
        versions;                               // absent = null list
    mutable std::vector<StorageCall> listCalls; // baseVersion = request's
    mutable std::vector<bool> listShowOther;
    std::vector<std::tuple<int, int, int, storage::MapVersionInfoPtr>> deletions; // (zoom, x, y, version)
    int failListAtCall = 0;                                                       // 1-based
    int failDeleteAtCall = 0;                                                     // 1-based
    bool deleteAnswer = true;
    std::function<void(int)> onDelete; // called with the ordinal of the deletion, before the answer

    [[nodiscard]] core::Result<bool> deleteTile(geo::PointI xy, quint8 zoom,
                                                const storage::MapVersionInfoPtr& version) override {
        deletions.emplace_back(zoom, xy.x(), xy.y(), version);
        const int ordinal = static_cast<int>(deletions.size());
        if (onDelete) {
            onDelete(ordinal);
        }
        if (ordinal == failDeleteAtCall) {
            return core::fail(core::ErrorCode::IoError, QStringLiteral("scripted delete failure"));
        }
        return deleteAnswer;
    }
    // T-0351: the saves of the prune (zoom, x, y, version, data); the answers are scripted below
    std::vector<std::tuple<int, int, int, storage::MapVersionInfoPtr, QByteArray>> saves;
    // T-0351: the record of the skipped saves the prune takes (per call: the version asked for)
    std::map<std::tuple<quint8, int, int>, storage::ITileStorage::SkippedSave> skippedSaves;
    std::vector<storage::MapVersionInfoPtr> takenFor;
    [[nodiscard]] std::map<std::tuple<quint8, int, int>, storage::ITileStorage::SkippedSave>
    takeSkippedSaves(const storage::MapVersionInfoPtr& version) override {
        takenFor.push_back(version);
        return skippedSaves;
    }
    int failSaveAtCall = 0;   // 1-based: this save answers an error
    int refuseSaveAtCall = 0; // 1-based: this save answers false
    [[nodiscard]] core::Result<bool> saveTile(geo::PointI xy, quint8 zoom,
                                              const storage::MapVersionInfoPtr& v, storage::TileDate,
                                              QByteArrayView, const QByteArray& data, bool) override {
        saves.emplace_back(zoom, xy.x(), xy.y(), v, data);
        const int ordinal = static_cast<int>(saves.size());
        if (ordinal == failSaveAtCall) {
            return core::fail(core::ErrorCode::IoError, QStringLiteral("scripted save failure"));
        }
        return ordinal != refuseSaveAtCall;
    }
    [[nodiscard]] core::Result<storage::MapVersionList>
    listVersions(geo::PointI xy, quint8 zoom, const storage::MapVersionRequest& request) const override {
        listCalls.push_back(StorageCall{zoom, xy.x(), xy.y(), storage::LoadMode::AsIs, request.baseVersion});
        listShowOther.push_back(request.showOtherVersions);
        if (static_cast<int>(listCalls.size()) == failListAtCall) {
            return core::fail(core::ErrorCode::IoError, QStringLiteral("scripted list failure"));
        }
        const auto it = versions.find({zoom, xy.x(), xy.y()});
        if (it == versions.end()) {
            return storage::MapVersionList{};
        }
        return std::make_shared<const std::vector<storage::MapVersionInfoPtr>>(it->second);
    }
    [[nodiscard]] core::Result<storage::TileRectInfoPtr>
    tileRectInfo(geo::RectI, quint8, const storage::MapVersionRequest&, std::stop_token) const override {
        unused();
    }
    [[nodiscard]] core::Result<storage::EnumTileInfoPtr> scanTiles(bool, bool) override { unused(); }

private:
    [[noreturn]] static void unused() { throw std::logic_error("ScriptedStorage: not used by the estimate"); }
};

class EstimateRigTest : public ::testing::Test {
public:
    static void SetUpTestSuite() {
        maptype::test::ensureGuiApp();
        dataDir_ = std::make_unique<QTemporaryDir>();
        AppOptions options;
        options.arguments = {QStringLiteral("--datadir=") + dataDir_->path()};
        options.warn = [](const QString&) {};
        options.mapsDirs = {dataDir_->path() + QStringLiteral("/Maps")};
        auto created = AppContext::create(options);
        if (!created) {
            throw std::runtime_error("AppContext::create failed");
        }
        app_ = std::move(created.value());
    }
    static void TearDownTestSuite() {
        app_.reset();
        dataDir_.reset();
    }

protected:
    void SetUp() override {
        geo::ProjectionSetFactory setFactory{geo::HashSeed{},
                                             std::make_shared<const geo::DatumFactory>(geo::HashSeed{})};
        const auto versionFactory = std::make_shared<maptype::MapVersionFactoryChangeable>(
            std::make_shared<storage::MapVersionFactory>());
        map_ = std::make_shared<region::test::FakeMapType>();
        map_->projectionSetValue = setFactory.byCode(geo::kProjEpsgGoogle, geo::kTileSplitQuadrate256x256);
        map_->versionFactoryValue = versionFactory;
        map_->versionRequestValue = std::make_shared<maptype::MapVersionRequestChangeable>(
            std::make_shared<maptype::MapVersionRequestConfig>(QStringLiteral("base-ver")), versionFactory);
        storage_ = std::make_shared<ScriptedStorage>();
        map_->tileStorageValue = storage_;
    }

    [[nodiscard]] static const AppContext& app() { return *app_; }

    // Lon/lat rectangle {left, top, right, bottom}.
    [[nodiscard]] static std::shared_ptr<const geo::IGeometryLonLatPolygon>
    rect(double left, double top, double right, double bottom) {
        return app_->lonLatFactory()->createPolygonByRect(geo::LonLatRect{left, top, right, bottom});
    }
    [[nodiscard]] static std::shared_ptr<const geo::IGeometryLonLatPolygon>
    polygon(std::vector<geo::LonLat> points) {
        return std::dynamic_pointer_cast<const geo::IGeometryLonLatPolygon>(
            app_->lonLatFactory()->createPolygon(points));
    }

    // The tiles the real iterator yields for `area` at `zoom`, in iteration order.
    [[nodiscard]] std::vector<geo::PointI>
    iterate(quint8 zoom, const std::shared_ptr<const geo::IGeometryLonLatPolygon>& area) const {
        const auto iterator =
            app_->tileIteratorFactory()->makeTileIterator(map_->projectionSet()->zoom(zoom), area);
        std::vector<geo::PointI> tiles;
        geo::PointI tile;
        while (iterator->next(tile)) {
            tiles.push_back(tile);
        }
        return tiles;
    }

    // Every lookup went out as a metadata-only read for the map's static base version (read-only towards
    // the cache: never WithData).
    void expectMetadataOnlyCalls() const {
        const storage::MapVersionInfoPtr base = map_->versionRequestValue->getStatic()->baseVersion;
        ASSERT_NE(base, nullptr);
        for (const StorageCall& call : storage_->calls) {
            EXPECT_EQ(call.mode, storage::LoadMode::WithoutData);
            EXPECT_EQ(call.baseVersion, base);
        }
    }

    std::shared_ptr<region::test::FakeMapType> map_;
    std::shared_ptr<ScriptedStorage> storage_;

private:
    inline static std::unique_ptr<QTemporaryDir> dataDir_;
    inline static std::unique_ptr<AppContext> app_;
};

// The z6 rectangle lon -135..-45, lat 30..10 (Google/3857): pixel rect {2048,6759,6144,7735}, tiles
// {8,26,24,31} = x 8..23, y 26..30 = 16 x 5 = 80 tiles (hand-derived in
// tests/region/tiles_download_test.cpp). At z3 the same rectangle covers x 1..2, y 3 = 2 tiles (pixels
// 256..768 x 844.95..966.82).
inline constexpr double kLonLeft = -135.0;
inline constexpr double kLonRight = -45.0;
inline constexpr double kLatTop = 30.0;
inline constexpr double kLatBottom = 10.0;

// The z6 contents used by the estimate and fill tests: 10 present tiles (sizes sum 15000, mean 1500), 6 TNE
// markers, 3 NotExists entries (count as missing), the rest unknown; decoys outside the rectangle.
inline void fillZ6(ScriptedStorage& s) {
    s.put(6, 23, 30, presentTile(700));
    s.put(6, 8, 26, presentTile(500));
    s.put(6, 15, 28, presentTile(3300));
    s.put(6, 9, 27, presentTile(1100));
    s.put(6, 20, 29, presentTile(900));
    s.put(6, 12, 30, presentTile(1900));
    s.put(6, 16, 26, presentTile(1300));
    s.put(6, 10, 29, presentTile(2100));
    s.put(6, 22, 27, presentTile(1500));
    s.put(6, 13, 28, presentTile(1700));
    s.put(6, 8, 30, markerTile());
    s.put(6, 9, 26, markerTile());
    s.put(6, 14, 29, markerTile());
    s.put(6, 18, 27, markerTile());
    s.put(6, 21, 28, markerTile());
    s.put(6, 23, 26, markerTile());
    s.put(6, 11, 26, notExistsTile());
    s.put(6, 17, 29, notExistsTile());
    s.put(6, 19, 30, notExistsTile());
    s.put(6, 24, 26, presentTile(99999)); // just right of the rectangle
    s.put(6, 7, 26, presentTile(99999));  // just left of it
    s.put(6, 8, 31, presentTile(99999));  // just below it
    s.put(6, 8, 25, markerTile());        // just above it
    s.put(5, 8, 26, presentTile(99999));  // another zoom
    s.put(7, 8, 26, presentTile(99999));
}

} // namespace cg::app::test
