// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_TileStorageOfMapType.pas; docs/specs/storage.md §2.22.3-§2.22.6 (ticket T-0138, P-4d).
// Covers: FEAT-STO-001. Real folder / RAM backends in a QTemporaryDir; one scripted storage type for the
// failed-build and race cases. No legacy DUnit exists.
#include "cg/core/content_type.h"
#include "cg/core/path_config.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/storage/file_system_tile_storage_type.h"
#include "cg/storage/ram_tile_storage_type.h"
#include "cg/storage/tile_storage_of_map_type.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <latch>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

namespace {

using namespace cg::storage;
using cg::core::ErrorCode;
using cg::geo::PointI;

cg::geo::ProjectionSetPtr projectionSet() {
    static const auto datums = std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{});
    static const cg::geo::ProjectionSetFactory factory{cg::geo::HashSeed{}, datums};
    return factory.byCode(cg::geo::kProjEpsgGoogle, cg::geo::kTileSplitQuadrate256x256);
}

const cg::core::ContentTypeInfo* jpeg() {
    return cg::core::ContentTypeRegistry::standard().infoByType("image/jpeg");
}

// One-shot gate on a mutex and a condition variable. ThreadSanitizer models these exactly; it did not see the
// happens-before edge std::latch gives (libstdc++ futex wait), so the three hand-over tests use this instead.
class Gate {
public:
    explicit Gate(int) {}
    void count_down() {
        {
            const std::lock_guard lock(mutex_);
            open_ = true;
        }
        cv_.notify_all();
    }
    void wait() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return open_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool open_ = false;
};

// Forwards everything to `inner`; the first state() call runs `hook` (a hand-over point for the race test).
class HookedStorage final : public ITileStorage {
public:
    HookedStorage(ITileStoragePtr inner, std::function<void()> hook)
        : inner_(std::move(inner)), hook_(std::move(hook)) {}
    StorageStateHolder& holder() const {
        return dynamic_cast<StorageStateHolder&>(inner_->stateChangeable());
    }

    TileStorageTypeAbilities typeAbilities() const override { return inner_->typeAbilities(); }
    StorageStatePtr state() const override {
        if (hook_ && !fired_.exchange(true)) {
            hook_();
        }
        return inner_->state();
    }
    cg::core::Changeable& stateChangeable() override { return inner_->stateChangeable(); }
    const cg::geo::IProjectionSet& projectionSet() const override { return inner_->projectionSet(); }
    TilePyramidNotifier& tileNotifier() override { return inner_->tileNotifier(); }
    QString tileFileName(PointI xy, quint8 z, const MapVersionInfoPtr& v) const override {
        return inner_->tileFileName(xy, z, v);
    }
    cg::core::Result<TileInfo> tileInfo(PointI xy, quint8 z, const MapVersionInfoPtr& v,
                                        LoadMode m) const override {
        return inner_->tileInfo(xy, z, v, m);
    }
    cg::core::Result<TileInfo> tileInfoEx(PointI xy, quint8 z, const MapVersionRequest& r,
                                          LoadMode m) const override {
        return inner_->tileInfoEx(xy, z, r, m);
    }
    cg::core::Result<bool> deleteTile(PointI xy, quint8 z, const MapVersionInfoPtr& v) override {
        return inner_->deleteTile(xy, z, v);
    }
    cg::core::Result<bool> saveTile(PointI xy, quint8 z, const MapVersionInfoPtr& v, TileDate d,
                                    QByteArrayView c, const QByteArray& data, bool o) override {
        return inner_->saveTile(xy, z, v, d, c, data, o);
    }
    cg::core::Result<MapVersionList> listVersions(PointI xy, quint8 z,
                                                  const MapVersionRequest& r) const override {
        return inner_->listVersions(xy, z, r);
    }
    cg::core::Result<TileRectInfoPtr> tileRectInfo(cg::geo::RectI rc, quint8 z, const MapVersionRequest& r,
                                                   std::stop_token s) const override {
        return inner_->tileRectInfo(rc, z, r, std::move(s));
    }
    cg::core::Result<EnumTileInfoPtr> scanTiles(bool a, bool b) override { return inner_->scanTiles(a, b); }

private:
    ITileStoragePtr inner_;
    std::function<void()> hook_;
    mutable std::atomic<bool> fired_{false};

public:
    // a slot on the backend's own state holder, registered before the proxy's subscription
    cg::core::Subscription flipSub;
};

// A storage type whose build() answers what the test scripts.
class ScriptedType final : public ITileStorageType {
public:
    enum class Mode { Error, Throw, Null, Ram };

    explicit ScriptedType(MapVersionFactoryPtr factory)
        : factory_(std::move(factory)), ram_(std::make_shared<RamTileStorageType>(factory_, nullptr)) {}

    TileStorageTypeAbilities abilities() const override { return ram_->abilities(); }
    cg::core::PathConfigPtr basePath() const override {
        if (auto hook =
                std::exchange(onBasePath, nullptr)) { // one shot: parks the caller in updateActualPath
            hook();
        }
        return nullptr;
    }
    MapVersionFactoryPtr mapVersionFactory() const override { return factory_; }
    cg::core::Result<ITileStoragePtr> build(const TileStorageAbilities& force, cg::geo::ProjectionSetPtr ps,
                                            const cg::core::ContentTypeInfo& ct, TilePyramidNotifierPtr n,
                                            const QString& path,
                                            std::shared_ptr<TileInfoMemCache> cache) override {
        ++builds;
        oldGoneAtBuild = last.expired(); // B1: before the new backend is made
        switch (mode) {
        case Mode::Error:
            return cg::core::fail(ErrorCode::IoError, QStringLiteral("scripted"));
        case Mode::Throw:
            throw std::runtime_error("scripted");
        case Mode::Null:
            return ITileStoragePtr{};
        case Mode::Ram:
            break;
        }
        auto inner = ram_->build(force, std::move(ps), ct, std::move(n), path, std::move(cache));
        if (!inner) {
            return inner;
        }
        auto hooked = std::make_shared<HookedStorage>(*inner, std::exchange(nextHook, nullptr));
        last = hooked;
        if (auto slot = std::exchange(nextFlipSlot, nullptr)) {
            hooked->flipSub = hooked->holder().changed().subscribe(std::move(slot));
        }
        return ITileStoragePtr{hooked};
    }

    Mode mode = Mode::Ram;
    int builds = 0;
    bool oldGoneAtBuild = false;
    std::function<void()> nextHook;
    std::function<void()> nextFlipSlot;
    mutable std::function<void()> onBasePath;
    std::weak_ptr<HookedStorage> last;

private:
    MapVersionFactoryPtr factory_;
    std::shared_ptr<RamTileStorageType> ram_;
};

struct Recorder final : IMapVersionFactoryChangeableInternal {
    void setFactory(const MapVersionFactoryPtr& value) override { calls.push_back(value); }
    std::vector<MapVersionFactoryPtr> calls;
};

struct Counts {
    std::atomic<int> full{0}; // bumped by the notifier on whichever thread saves / unlocks
    std::atomic<int> tile{0};
};

ITileStorageTypeListItemPtr item(CacheTypeId id, ITileStorageTypePtr type) {
    return std::make_shared<TileStorageTypeListItem>(QUuid(), static_cast<int>(id), QStringLiteral("t"),
                                                     std::move(type), true, true);
}

SimpleTileStorageConfigStaticPtr defaults(CacheTypeId type) {
    return std::make_shared<const SimpleTileStorageConfigStatic>(
        SimpleTileStorageConfigStatic{type, QStringLiteral("Город/sat"), QByteArray("image/jpg"),
                                      QByteArray(".jpg"), TileStorageAbilities::readWrite()});
}

const TileDate kDate = fromPosixSeconds(1700000000);
const QByteArray kData("jpg-bytes");

struct Rig {
    QTemporaryDir dir;
    std::shared_ptr<GlobalCacheConfig> global =
        std::make_shared<GlobalCacheConfig>(cg::core::PathConfig::create(QString(), dir.path(), nullptr));
    MapVersionFactoryPtr factory = std::make_shared<MapVersionFactory>();
    std::shared_ptr<ScriptedType> scripted = std::make_shared<ScriptedType>(factory);
    ITileStorageTypePtr sasType = std::make_shared<FileSystemTileStorageType>(
        codecForCacheType(CacheTypeId::Sas), factory, global->pathFor(CacheTypeId::Sas));
    ITileStorageTypeListPtr list =
        std::make_shared<TileStorageTypeList>(std::vector<ITileStorageTypeListItemPtr>{
            item(CacheTypeId::Sas, sasType),
            item(CacheTypeId::Gmv,
                 std::make_shared<FileSystemTileStorageType>(codecForCacheType(CacheTypeId::Gmv), factory,
                                                             global->pathFor(CacheTypeId::Gmv))),
            item(CacheTypeId::Ram, std::make_shared<RamTileStorageType>(factory, nullptr)),
            item(CacheTypeId::Bdb, scripted)});
    std::shared_ptr<TileInfoMemCache> memCache =
        std::make_shared<TileInfoMemCache>(100, std::chrono::milliseconds(60'000), ClearStrategy::ByYoungest);
    std::shared_ptr<Recorder> recorder = std::make_shared<Recorder>();
    std::shared_ptr<SimpleTileStorageConfig> config;
    std::unique_ptr<TileStorageOfMapType> wrapper;
    Counts counts;
    cg::core::Subscription tileSub;

    void start(CacheTypeId type, const cg::core::ContentTypeInfo* content = jpeg(),
               bool withMemCache = true) {
        global->setDefCache(CacheTypeId::Sas);
        config = std::make_shared<SimpleTileStorageConfig>(defaults(type));
        wrapper = std::make_unique<TileStorageOfMapType>(global, projectionSet(), list, recorder, config,
                                                         withMemCache ? memCache : nullptr, content);
        tileSub = wrapper->tileNotifier().subscribe([this](const TileUpdate* u) {
            if (u != nullptr) {
                ++counts.tile;
            } else {
                ++counts.full;
            }
        });
    }
    QString root(const char* sub) const {
        return dir.path() + u'/' + QLatin1String(sub) + QStringLiteral("/Город/sat/");
    }
    bool save(PointI xy = {0, 0}, quint8 zoom = 0) {
        auto r = wrapper->saveTile(xy, zoom, nullptr, kDate, QByteArrayView("image/jpg"), kData, false);
        return r.has_value() && *r;
    }
    TileInfoType typeOf(PointI xy = {0, 0}, quint8 zoom = 0) const {
        auto r = wrapper->tileInfo(xy, zoom, nullptr, LoadMode::WithoutData);
        EXPECT_TRUE(r.has_value());
        return r ? r->type : TileInfoType::Unknown;
    }
    void putInMemCache() {
        TileInfo ti;
        ti.type = TileInfoType::Exists;
        memCache->add({5, 5}, 3, nullptr, ti);
    }
    bool inMemCache() const { return memCache->get({5, 5}, 3, nullptr, LoadMode::AsIs, false).has_value(); }
};

void expectNoAccess(const StorageStatePtr& s) {
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(*s, (StorageState{false, false, false, false, false}));
}

int countJpg(const QString& root) {
    int n = 0;
    QDirIterator it(root, {QStringLiteral("*.jpg")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++n;
    }
    return n;
}

} // namespace

// T-0352 (option C): the facade records nothing by default, whatever the version (a static map with
// Version=RUS or 2.33.0 refuses identical saves on every --overwrite run); only after the run switched the
// record on (a snapshot source) a refused save with data is recorded, with the size and CRC-32 of the data.
TEST(TileStorageOfMapType, RefusedSavesAreRecordedOnlyWhenTheRunSwitchedItOn) {
    Rig r;
    r.start(CacheTypeId::Ram);
    const MapVersionInfoPtr versioned = r.factory->byStoreString(QStringLiteral("/v1"));
    const auto saveUnder = [&](const MapVersionInfoPtr& v, PointI xy) {
        auto res = r.wrapper->saveTile(xy, 3, v, kDate, QByteArrayView("image/jpg"), kData, false);
        EXPECT_TRUE(res.has_value());
        return res && *res;
    };
    EXPECT_TRUE(saveUnder(versioned, PointI{1, 2}));
    EXPECT_FALSE(saveUnder(versioned, PointI{1, 2})); // refused, record off: nothing kept
    EXPECT_TRUE(r.wrapper->takeSkippedSaves(versioned).empty());

    r.wrapper->setRecordSkippedSaves(true);
    EXPECT_FALSE(saveUnder(versioned, PointI{1, 2}));
    const auto taken = r.wrapper->takeSkippedSaves(versioned);
    EXPECT_EQ(taken, (std::map<std::tuple<quint8, int, int>, ITileStorage::SkippedSave>{
                         {{3, 1, 2}, {9, 0x018c5e54U}}}));
    EXPECT_TRUE(r.wrapper->takeSkippedSaves(versioned).empty()); // taken once

    r.wrapper->setRecordSkippedSaves(false);
    EXPECT_FALSE(saveUnder(versioned, PointI{1, 2}));
    EXPECT_TRUE(r.wrapper->takeSkippedSaves(versioned).empty());
}

TEST(TileStorageOfMapType, BuildsTheBackendAtConstruction) {
    Rig r;
    r.start(CacheTypeId::Sas);
    EXPECT_TRUE(r.save());
    EXPECT_TRUE(QFile::exists(r.root("cache") + QStringLiteral("z1/0/x0/0/y0.jpg")));
    EXPECT_EQ(r.typeOf(), TileInfoType::Exists);
    EXPECT_EQ(r.wrapper->typeAbilities().storageClass, StorageClass::InSeparateFiles);
    // the state is the one a folder backend built directly by the same type with the same arguments has
    auto direct = r.sasType->build(TileStorageAbilities::readWrite(), projectionSet(), *jpeg(),
                                   std::make_shared<TilePyramidNotifier>(), r.root("cache"), nullptr);
    ASSERT_TRUE(direct.has_value());
    EXPECT_EQ(*r.wrapper->state(), *(*direct)->state());
    EXPECT_TRUE(r.wrapper->state()->addAccess);
    ASSERT_EQ(r.recorder->calls.size(), 1U);
    EXPECT_EQ(r.recorder->calls[0], r.factory);
    EXPECT_EQ(r.counts.full, 0); // the construction-time full update had no subscriber
}

TEST(TileStorageOfMapType, NoBackendAnswersNothing) {
    // rows: a type with no list item (Ge), and a null content type
    for (const bool nullContent : {false, true}) {
        Rig r;
        r.putInMemCache();
        r.start(nullContent ? CacheTypeId::Sas : CacheTypeId::Ge, nullContent ? nullptr : jpeg());
        const TileStorageTypeAbilities abilities = r.wrapper->typeAbilities();
        EXPECT_EQ(abilities.base, TileStorageAbilities{});
        EXPECT_EQ(abilities.version, VersionSupport::Ignored);
        EXPECT_FALSE(abilities.supportsMixedContentTypes);
        EXPECT_EQ(abilities.storageClass, StorageClass::Other);
        expectNoAccess(r.wrapper->state());
        EXPECT_TRUE(r.wrapper->tileFileName({0, 0}, 0, nullptr).isEmpty());
        auto info = r.wrapper->tileInfo({0, 0}, 0, nullptr, LoadMode::WithData);
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->type, TileInfoType::Unknown);
        auto infoEx = r.wrapper->tileInfoEx({0, 0}, 0, MapVersionRequest{}, LoadMode::AsIs);
        ASSERT_TRUE(infoEx.has_value());
        EXPECT_EQ(infoEx->type, TileInfoType::Unknown);
        auto del = r.wrapper->deleteTile({0, 0}, 0, nullptr);
        ASSERT_TRUE(del.has_value());
        EXPECT_FALSE(*del);
        auto sav = r.wrapper->saveTile({0, 0}, 0, nullptr, kDate, QByteArrayView("image/jpg"), kData, true);
        ASSERT_TRUE(sav.has_value());
        EXPECT_FALSE(*sav);
        auto versions = r.wrapper->listVersions({0, 0}, 0, MapVersionRequest{});
        ASSERT_TRUE(versions.has_value());
        EXPECT_EQ(*versions, nullptr);
        auto rect = r.wrapper->tileRectInfo(cg::geo::RectI{}, 0, MapVersionRequest{}, std::stop_token{});
        ASSERT_TRUE(rect.has_value());
        EXPECT_EQ(*rect, nullptr);
        auto scan = r.wrapper->scanTiles(false, false);
        ASSERT_TRUE(scan.has_value());
        EXPECT_EQ(*scan, nullptr);
        EXPECT_EQ(&r.wrapper->projectionSet(), projectionSet().get()); // the wrapper's own, the cached set
        EXPECT_NE(&r.wrapper->tileNotifier(), nullptr);
        EXPECT_TRUE(r.recorder->calls.empty());
        EXPECT_TRUE(r.inMemCache()); // not cleared: nothing was built
    }
}

TEST(TileStorageOfMapType, DefaultFollowsDefCacheAndSwapKeepsSubscribers) {
    Rig r;
    r.start(CacheTypeId::Default);
    int stateFired = 0;
    const auto stateSub = r.wrapper->stateChangeable().changed().subscribe([&] { ++stateFired; });
    ASSERT_TRUE(r.save());
    EXPECT_TRUE(QFile::exists(r.root("cache") + QStringLiteral("z1/0/x0/0/y0.jpg")));
    EXPECT_EQ(r.counts.tile, 1);
    r.global->setDefCache(CacheTypeId::Gmv);
    EXPECT_EQ(r.counts.full, 1);
    EXPECT_EQ(r.counts.tile, 1);
    EXPECT_EQ(r.typeOf(), TileInfoType::NotExists);
    EXPECT_TRUE(QFile::exists(r.root("cache") + QStringLiteral("z1/0/x0/0/y0.jpg"))); // still on disk
    ASSERT_TRUE(r.save());
    EXPECT_TRUE(QFile::exists(r.root("cache_old") + QStringLiteral("01/t.jpg"))); // the GMV layout
    EXPECT_EQ(r.counts.tile, 2);                                                  // the same subscriber
    EXPECT_EQ(stateFired, 0); // both folder backends have equal state
}

TEST(TileStorageOfMapType, PermissionsDoNotRebuildButANameDoes) {
    Rig r;
    r.start(CacheTypeId::Ram);
    ASSERT_TRUE(r.save());
    ASSERT_TRUE(r.wrapper->state()->addAccess);
    r.config->setAllowAdd(false); // L-c1: reaches the listener, never the live backend
    EXPECT_EQ(r.counts.full, 0);
    EXPECT_EQ(r.typeOf(), TileInfoType::Exists);
    EXPECT_TRUE(r.wrapper->state()->addAccess);
    r.config->setNameInCache(QStringLiteral("other"));
    EXPECT_EQ(r.counts.full, 1); // the nested path notification adds no second update
    EXPECT_EQ(r.typeOf(), TileInfoType::NotExists);
    EXPECT_FALSE(r.wrapper->state()->addAccess);
}

TEST(TileStorageOfMapType, RootPathChangeRebuilds) {
    Rig r;
    QTemporaryDir other;
    r.start(CacheTypeId::Sas);
    ASSERT_TRUE(r.save());
    r.global->pathFor(CacheTypeId::Sas)->setPath(other.path());
    EXPECT_EQ(r.counts.full, 1);
    ASSERT_TRUE(r.save());
    EXPECT_TRUE(QFile::exists(other.path() + QStringLiteral("/Город/sat/z1/0/x0/0/y0.jpg")));
}

TEST(TileStorageOfMapType, StateAcrossSwapToNothingAndBack) {
    Rig r;
    r.start(CacheTypeId::Sas);
    const StorageStatePtr before = r.wrapper->state();
    const cg::core::Changeable* addr = &r.wrapper->stateChangeable();
    std::vector<StorageStatePtr> seen;
    const auto sub =
        r.wrapper->stateChangeable().changed().subscribe([&] { seen.push_back(r.wrapper->state()); });
    r.config->setCacheType(CacheTypeId::Dbms); // accepted by the config, no list item in the port
    ASSERT_EQ(seen.size(), 1U);
    expectNoAccess(seen[0]);
    r.config->setCacheType(CacheTypeId::Sas);
    ASSERT_EQ(seen.size(), 2U);
    EXPECT_EQ(*seen[1], *before);
    EXPECT_TRUE(before->readAccess);
    EXPECT_EQ(&r.wrapper->stateChangeable(), addr);
}

TEST(TileStorageOfMapType, MemCacheIsClearedAndHandedOver) {
    Rig r;
    r.start(CacheTypeId::Sas);
    r.putInMemCache();
    ASSERT_TRUE(r.inMemCache());
    r.config->setCacheType(CacheTypeId::Gmv);
    EXPECT_FALSE(r.inMemCache());
    ASSERT_TRUE(r.save({1, 1}, 2));
    EXPECT_TRUE(r.memCache->get({1, 1}, 2, nullptr, LoadMode::AsIs, false).has_value()); // the new backend's
}

TEST(TileStorageOfMapType, OneBatchOneUpdate) {
    Rig r;
    r.start(CacheTypeId::Sas);
    ASSERT_EQ(r.recorder->calls.size(), 1U);
    {
        const cg::core::ConfigElement::BatchToken batch(*r.config);
        r.config->setCacheType(CacheTypeId::Gmv);
        r.config->setNameInCache(QStringLiteral("x"));
    }
    EXPECT_EQ(r.counts.full, 1);
    EXPECT_EQ(r.recorder->calls.size(), 2U);
    EXPECT_TRUE(r.save());
    EXPECT_TRUE(QFile::exists(r.dir.path() + QStringLiteral("/cache_old/x/01/t.jpg")));
}

TEST(TileStorageOfMapType, FailedBuildIsRememberedUntilCodeOrPathChanges) {
    struct Row {
        ScriptedType::Mode mode;
        size_t factoryCalls; // a successful null still swaps the factory
    };
    for (const Row row : {Row{ScriptedType::Mode::Error, 0}, Row{ScriptedType::Mode::Throw, 0},
                          Row{ScriptedType::Mode::Null, 1}}) {
        Rig r;
        r.scripted->mode = row.mode;
        r.putInMemCache();
        r.start(CacheTypeId::Bdb); // nothing escapes the constructor
        EXPECT_EQ(r.scripted->builds, 1);
        EXPECT_EQ(r.recorder->calls.size(), row.factoryCalls);
        EXPECT_FALSE(r.inMemCache());
        expectNoAccess(r.wrapper->state());
        EXPECT_EQ(r.typeOf(), TileInfoType::Unknown);
        r.config->setAllowAdd(false); // L-c2: no retry
        EXPECT_EQ(r.scripted->builds, 1);
        EXPECT_EQ(r.counts.full, 0);
        r.config->setNameInCache(QStringLiteral("again"));
        EXPECT_EQ(r.scripted->builds, 2);
        EXPECT_EQ(r.counts.full, 1);
    }
}

// T-0275 (D row, L-44): the cause of a failed build is kept until the next build.
TEST(TileStorageOfMapType, BuildErrorHoldsPathAndCauseUntilAGoodBuild) {
    for (const auto mode : {ScriptedType::Mode::Error, ScriptedType::Mode::Throw}) {
        Rig r;
        r.scripted->mode = mode;
        r.start(CacheTypeId::Bdb);
        const std::optional<QString> failed = r.wrapper->buildError();
        ASSERT_TRUE(failed.has_value());
        EXPECT_TRUE(failed->contains(QStringLiteral("Город/sat/ (cache type 6): ")));
        EXPECT_TRUE(r.wrapper->path().endsWith(QStringLiteral("Город/sat/")));
        EXPECT_TRUE(failed->contains(QStringLiteral("scripted")));
        EXPECT_TRUE(failed->contains(QStringLiteral("cache type")));
        r.config->setAllowAdd(false); // L-c2: no rebuild, the cause stays
        EXPECT_EQ(r.wrapper->buildError(), failed);
        r.scripted->mode = ScriptedType::Mode::Ram;
        r.config->setNameInCache(QStringLiteral("again"));
        EXPECT_FALSE(r.wrapper->buildError().has_value());
    }
}

TEST(TileStorageOfMapType, BuildErrorIsEmptyWhenNothingFailed) {
    Rig noBackendAsked; // no content type: no build is attempted
    noBackendAsked.start(CacheTypeId::Bdb, nullptr);
    EXPECT_FALSE(noBackendAsked.wrapper->buildError().has_value());
    Rig nullBackend; // a build that returns a null backend is not an error (legacy)
    nullBackend.scripted->mode = ScriptedType::Mode::Null;
    nullBackend.start(CacheTypeId::Bdb);
    EXPECT_FALSE(nullBackend.wrapper->buildError().has_value());
}

// T-0333 (owner 2026-10-21): the cache types 6 and 61 have no backend any more. A map that asks for them gets
// no storage and a build error naming BerkeleyDB; the other unregistered types (GoogleEarth 5) stay silent.
TEST(TileStorageOfMapType, RemovedBerkeleyDbTypesGiveAClearBuildErrorOthersStaySilent) {
    struct Row {
        CacheTypeId type;
        bool expectError;
    };
    for (const Row row :
         {Row{CacheTypeId::Bdb, true}, Row{CacheTypeId::BdbVersioned, true}, Row{CacheTypeId::Ge, false}}) {
        SCOPED_TRACE(static_cast<int>(row.type));
        Rig r;
        const ITileStorageTypeListPtr onlySas = std::make_shared<TileStorageTypeList>(
            std::vector<ITileStorageTypeListItemPtr>{item(CacheTypeId::Sas, r.sasType)});
        r.global->setDefCache(CacheTypeId::Sas);
        r.config = std::make_shared<SimpleTileStorageConfig>(defaults(row.type));
        r.wrapper = std::make_unique<TileStorageOfMapType>(r.global, projectionSet(), onlySas, r.recorder,
                                                           r.config, r.memCache, jpeg());
        expectNoAccess(r.wrapper->state()); // no storage
        const std::optional<QString> error = r.wrapper->buildError();
        ASSERT_EQ(error.has_value(), row.expectError);
        if (row.expectError) {
            EXPECT_TRUE(error->contains(QStringLiteral("BerkeleyDB")));
            EXPECT_TRUE(error->contains(QStringLiteral("(cache type %1)").arg(static_cast<int>(row.type))));
        }
    }
}

TEST(TileStorageOfMapType, FailureAfterAWorkingBackendFiresOneUpdateAndDropsTheBackend) {
    Rig r;
    r.start(CacheTypeId::Sas);
    r.putInMemCache();
    r.scripted->mode = ScriptedType::Mode::Error;
    r.config->setCacheType(CacheTypeId::Bdb);
    EXPECT_EQ(r.counts.full, 1);
    EXPECT_FALSE(r.inMemCache());
    expectNoAccess(r.wrapper->state());
    EXPECT_EQ(r.recorder->calls.size(), 1U); // only the Sas build set the factory
}

TEST(TileStorageOfMapType, FullUpdateRunsAfterTheStorageLockIsReleased) {
    // L-c3: the state notification is inside the write lock, the full update outside it, so a full-update
    // slot may call the wrapper (a state slot must not).
    Rig r;
    r.start(CacheTypeId::Sas);
    std::vector<char> order;
    const auto stateSub = r.wrapper->stateChangeable().changed().subscribe([&] { order.push_back('S'); });
    TileInfoType seen = TileInfoType::Exists;
    const auto sub = r.wrapper->tileNotifier().subscribe([&](const TileUpdate* u) {
        if (u == nullptr) {
            order.push_back('F');
            seen = r.typeOf();
        }
    });
    r.config->setCacheType(CacheTypeId::Dbms);
    EXPECT_EQ(order, (std::vector<char>{'S', 'F'}));
    EXPECT_EQ(seen, TileInfoType::Unknown);
}

TEST(TileStorageOfMapType, OldBackendIsGoneBeforeTheNextIsBuilt) {
    // B1 (legacy :268): the old backend is destroyed first; the proxy keeps its target weakly for that
    // reason.
    Rig r;
    r.start(CacheTypeId::Bdb);
    ASSERT_EQ(r.scripted->builds, 1);
    ASSERT_NE(r.scripted->last.lock(), nullptr);
    r.config->setNameInCache(QStringLiteral("b"));
    EXPECT_EQ(r.scripted->builds, 2);
    EXPECT_TRUE(r.scripted->oldGoneAtBuild);
}

TEST(TileStorageOfMapType, NestedLockCounterHoldsTheUpdateBackAcrossThreads) {
    // StorageLock / StorageUnlock (legacy :420-426): thread A is inside its listener (counter 1); a change on
    // the main thread leaves and finds the counter at 2 -> 1: no update. Only A's unlock (1 -> 0) runs one.
    Rig r;
    r.start(CacheTypeId::Default);
    r.global->setDefCache(CacheTypeId::Bdb);
    ASSERT_EQ(r.scripted->builds, 1);
    ASSERT_EQ(r.counts.full, 1);
    Gate parked(1);
    Gate release(1);
    r.scripted->onBasePath = [&] {
        parked.count_down();
        release.wait();
    };
    std::thread a([&] { r.config->setAllowAdd(false); });
    parked.wait();
    r.global->setDefCache(CacheTypeId::Ram); // main thread: counter 2 -> 1
    EXPECT_EQ(r.counts.full, 1);
    EXPECT_FALSE(r.scripted->last.expired()); // the scripted backend is still the live one
    release.count_down();
    a.join();
    EXPECT_EQ(r.counts.full, 2);
    EXPECT_TRUE(r.scripted->last.expired()); // swapped to the RAM type's backend
    EXPECT_EQ(r.scripted->builds, 1);
}

TEST(TileStorageOfMapType, SetTargetReadsTheFlippedStateDeterministically) {
    // The new backend's own state slot (ahead of the proxy's) counts down `flipped`; the hook, inside
    // setTarget under the proxy lock, waits for it. So setTarget necessarily reads the flipped state while
    // the flipper's onTargetChange waits for the proxy lock.
    Rig r;
    r.start(CacheTypeId::Bdb);
    ASSERT_TRUE(r.wrapper->state()->readAccess);
    int fired = 0;
    const auto sub = r.wrapper->stateChangeable().changed().subscribe([&] { ++fired; });
    Gate inSetTarget(1);
    Gate flipped(1);
    r.scripted->nextFlipSlot = [&] { flipped.count_down(); };
    r.scripted->nextHook = [&] {
        inSetTarget.count_down();
        flipped.wait();
    };
    std::thread flipper([&] {
        inSetTarget.wait();
        const auto target = r.scripted->last.lock();
        ASSERT_NE(target, nullptr);
        target->holder().setReadAccess(false);
    });
    r.config->setNameInCache(QStringLiteral("b"));
    if (r.scripted->builds != 2) {
        inSetTarget.count_down(); // no rebuild, so the hook never ran: release the flipper, fail below
        flipped.count_down();
    }
    flipper.join();
    EXPECT_FALSE(r.wrapper->state()->readAccess);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(r.scripted->builds, 2);
}

TEST(TileStorageOfMapType, SetTargetRacingATargetStateChange) {
    // --tsan (the latches leave the flip and setTarget's body unordered; the deterministic twin is above):
    // the new backend's state flips while setTarget (inside the proxy lock) reads it. The hook runs in the
    // new backend's first state() call -- inside setTarget -- and hands over to the flipper, which counts
    // down before it flips, so the two windows overlap by construction.
    Rig r;
    r.start(CacheTypeId::Bdb);
    ASSERT_TRUE(r.wrapper->state()->readAccess);
    int fired = 0;
    const auto sub = r.wrapper->stateChangeable().changed().subscribe([&] { ++fired; });
    Gate inSetTarget(1);
    Gate flipperStarted(1);
    r.scripted->nextHook = [&] {
        inSetTarget.count_down();
        flipperStarted.wait();
    };
    std::thread flipper([&] {
        inSetTarget.wait();
        const auto target = r.scripted->last.lock();
        ASSERT_NE(target, nullptr);
        flipperStarted.count_down();
        target->holder().setReadAccess(false);
    });
    r.config->setNameInCache(QStringLiteral("b"));
    if (r.scripted->builds != 2) {
        inSetTarget.count_down(); // no rebuild, so the hook never ran: release the flipper, fail below
    }
    flipper.join();
    EXPECT_FALSE(r.wrapper->state()->readAccess);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(r.scripted->builds, 2);
}

TEST(TileStorageOfMapType, TileCallsRaceWithBackendSwaps) {
    // --tsan. Whether both roots receive a file depends on scheduling, so only what always holds is asserted:
    // every call returned a value and every successful save is on disk in one of the two roots.
    Rig r;
    r.start(CacheTypeId::Default);
    constexpr int kThreads = 4;
    constexpr int kTiles = 40;
    std::latch go(kThreads + 1);
    std::atomic<int> saved{0};
    std::atomic<int> failed{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            go.arrive_and_wait();
            for (int i = 0; i < kTiles; ++i) {
                const PointI xy{t, i};
                auto s =
                    r.wrapper->saveTile(xy, 6, nullptr, kDate, QByteArrayView("image/jpg"), kData, false);
                auto q = r.wrapper->tileInfo(xy, 6, nullptr, LoadMode::WithoutData);
                if (!s.has_value() || !q.has_value()) {
                    ++failed;
                } else if (*s) {
                    ++saved;
                }
            }
        });
    }
    go.arrive_and_wait();
    for (int i = 0; i < 20; ++i) {
        r.global->setDefCache(i % 2 == 0 ? CacheTypeId::Gmv : CacheTypeId::Sas);
    }
    for (auto& th : threads) {
        th.join();
    }
    EXPECT_EQ(failed.load(), 0);
    EXPECT_EQ(saved.load(), kThreads * kTiles);
    EXPECT_EQ(countJpg(r.dir.path() + QStringLiteral("/cache")) +
                  countJpg(r.dir.path() + QStringLiteral("/cache_old")),
              saved.load());
}
