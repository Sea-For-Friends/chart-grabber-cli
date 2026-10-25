// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT id (T-0146). Ports MapType/u_MapTypeSetChangeableBySourceSetWithFilter.pas; the ticket
// docs/backlog/T-0146-*.md is the specification (maptype.md 2.11.8 is an outline).
#include "cg/core/string_by_language.h"
#include "cg/maptype/map_type_set_changeable_by_source_set_with_filter.h"

#include <QString>
#include <QUuid>

#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <future>
#include <latch>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "fake_map_type.h"

using namespace cg::maptype;
using cg::core::StringByLanguageWithStaticList;

namespace {

QUuid id(int n) {
    return QUuid(static_cast<uint>(n), 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
}

class LicenseZmp final : public IZmpInfo {
public:
    explicit LicenseZmp(QStringList license)
        : license_(std::make_shared<StringByLanguageWithStaticList>(std::move(license))) {}
    QUuid guid() const override { return {}; }
    bool isLayer() const override { return false; }
    QString fileName() const override { return {}; }
    cg::core::IStringByLanguagePtr license() const override { return license_; }
    std::shared_ptr<const IZmpInfoGui> gui() const override { return nullptr; }
    int layerZOrder() const override { return 0; }
    QString version() const override { return {}; }
    TileDownloadRequestBuilderConfigStaticPtr tileDownloadRequestBuilderConfig() const override {
        return nullptr;
    }
    std::shared_ptr<const cg::net::TileDownloaderSettings> tileDownloaderConfig() const override {
        return nullptr;
    }
    TilePostDownloadCropConfigStaticPtr tilePostDownloadCropConfig() const override { return nullptr; }
    const cg::core::ContentTypeSubst& contentTypeSubst() const override {
        static const cg::core::ContentTypeSubst subst;
        return subst;
    }
    cg::geo::ProjectionSetPtr projectionSet() const override { return nullptr; }
    cg::geo::ProjectionSetPtr viewProjectionSet() const override { return nullptr; }
    MapAbilitiesConfigStaticPtr abilities() const override { return nullptr; }
    const std::vector<QByteArray>& emptyTileSamples() const override {
        static const std::vector<QByteArray> none;
        return none;
    }
    const std::vector<QByteArray>& banTileSamples() const override { return emptyTileSamples(); }
    cg::storage::SimpleTileStorageConfigStaticPtr storageConfig() const override { return nullptr; }
    cg::core::IConfigProviderPtr dataProvider() const override { return nullptr; }
    MapCoverageInfoPtr coverage() const override { return nullptr; }
    cg::vector::ImportPointParamsPtr pointParams() const override { return nullptr; }
    cg::vector::ImportLineParamsPtr lineParams() const override { return nullptr; }
    cg::vector::ImportPolyParamsPtr polyParams() const override { return nullptr; }

private:
    cg::core::IStringByLanguagePtr license_;
};

// FakeMapType is final and its isBitmapTiles()/isKmlTiles() are fixed false, so this fake carries the three
// things the filters read: the two flags and the licence of its ZMP. Everything else is empty.
class KindMap final : public IMapTypeProxy {
public:
    KindMap(QUuid guid, bool bitmap, bool kml, QStringList license)
        : guid_(std::move(guid)), bitmap_(bitmap), kml_(kml),
          zmp_(std::make_shared<LicenseZmp>(std::move(license))) {}

    void initialize(const cg::core::IConfigProviderPtr&) override {}
    void reset() override {}
    [[nodiscard]] bool isInitialized() const override { return true; }
    void saveConfig(const cg::core::IConfigWriterPtr&) const override {}
    QUuid guid() const override { return guid_; }
    void clearMemCache() override {}
    QString getTileShowName(cg::geo::PointI, quint8, const cg::storage::MapVersionInfoPtr&) const override {
        return {};
    }
    BitmapPtr loadTile(cg::geo::PointI, quint8, const MapVersionRequestPtr&, bool,
                       const std::shared_ptr<ITileObjCacheBitmap>&) const override {
        return nullptr;
    }
    VectorSubsetPtr loadTileVector(cg::geo::PointI, quint8, const MapVersionRequestPtr&, bool, bool,
                                   const std::shared_ptr<ITileObjCacheVector>&) const override {
        return nullptr;
    }
    BitmapPtr loadTileUni(cg::geo::PointI, const cg::geo::ProjectionPtr&, const MapVersionRequestPtr&, bool,
                          bool, bool, const std::shared_ptr<ITileObjCacheBitmap>&) const override {
        return nullptr;
    }
    BitmapPtr loadBitmap(cg::geo::RectI, quint8, const MapVersionRequestPtr&, bool, bool, bool,
                         const std::shared_ptr<ITileObjCacheBitmap>&) const override {
        return nullptr;
    }
    BitmapPtr loadBitmapUni(cg::geo::RectI, const cg::geo::ProjectionPtr&, const MapVersionRequestPtr&, bool,
                            bool, bool, const std::shared_ptr<ITileObjCacheBitmap>&) const override {
        return nullptr;
    }
    QString shortFolderName() const override { return {}; }
    void nextVersion(const cg::geo::LocalCoordConverterPtr&, int) override {}
    std::shared_ptr<const IZmpInfo> zmp() const override { return zmp_; }
    cg::geo::ProjectionSetPtr projectionSet() const override { return nullptr; }
    cg::geo::ProjectionSetPtr viewProjectionSet() const override { return nullptr; }
    std::shared_ptr<MapVersionFactoryChangeable> versionFactory() const override { return nullptr; }
    std::shared_ptr<MapVersionRequestConfig> versionRequestConfig() const override { return nullptr; }
    std::shared_ptr<MapVersionRequestChangeable> versionRequest() const override { return nullptr; }
    const cg::core::ContentTypeInfo* contentType() const override { return nullptr; }
    std::shared_ptr<MapAbilitiesConfig> abilities() const override { return nullptr; }
    std::shared_ptr<cg::storage::SimpleTileStorageConfig> storageConfig() const override { return nullptr; }
    bool isBitmapTiles() const override { return bitmap_; }
    bool isKmlTiles() const override { return kml_; }
    std::shared_ptr<ITileDownloadSubsystem> tileDownloadSubsystem() const override { return nullptr; }
    std::shared_ptr<cg::storage::ITileStorage> tileStorage() const override { return nullptr; }
    std::shared_ptr<MapTypeGuiConfig> guiConfig() const override { return nullptr; }
    std::shared_ptr<LayerDrawConfig> layerDrawConfig() const override { return nullptr; }
    std::shared_ptr<cg::net::TileDownloaderConfig> tileDownloaderConfig() const override { return nullptr; }
    std::shared_ptr<TileDownloadRequestBuilderConfig> tileDownloadRequestBuilderConfig() const override {
        return nullptr;
    }
    std::shared_ptr<ITileObjCacheBitmap> cacheBitmap() const override { return nullptr; }
    std::shared_ptr<ITileObjCacheVector> cacheVector() const override { return nullptr; }
    std::shared_ptr<cg::storage::TileInfoMemCache> cacheTileInfo() const override { return nullptr; }

private:
    QUuid guid_;
    bool bitmap_;
    bool kml_;
    std::shared_ptr<const IZmpInfo> zmp_;
};

// Map kinds. The default-language licence text is element 0 (StringByLanguageWithStaticList).
IMapTypePtr bitmapMap(int n) {
    return std::make_shared<KindMap>(id(n), true, false, QStringList{QString()});
}
IMapTypePtr kmlMap(int n) {
    return std::make_shared<KindMap>(id(n), false, true, QStringList{QString()});
}
IMapTypePtr plainMap(int n) {
    return std::make_shared<KindMap>(id(n), false, false, QStringList{QString()});
}
IMapTypePtr licensedMap(int n, bool bitmap = false) {
    return std::make_shared<KindMap>(id(n), bitmap, false, QStringList{QStringLiteral(u"Лицензия")});
}

IMapTypeSetPtr makeSet(const std::vector<IMapTypePtr>& maps, bool allowNil = false) {
    auto builder = MapTypeSetBuilderFactory().build(allowNil);
    for (const auto& m : maps) {
        builder->add(m);
    }
    return builder->makeAndClear();
}

std::vector<QUuid> guidsOf(const IMapTypeSetPtr& set) {
    std::vector<QUuid> out;
    for (int i = 0; set != nullptr && i < set->count(); ++i) {
        out.push_back(set->item(i)->guid());
    }
    return out;
}

// A source whose content and notification the test drives (MapTypeSetChangeableSimple never notifies).
class TestSource final : public IMapTypeSetChangeable {
public:
    explicit TestSource(IMapTypeSetPtr initial) : set_(std::move(initial)) {}
    [[nodiscard]] IMapTypeSetPtr getStatic() const override {
        const std::scoped_lock lock(mutex_);
        return set_;
    }
    void set(IMapTypeSetPtr value) {
        const std::scoped_lock lock(mutex_);
        set_ = std::move(value);
    }
    void change(IMapTypeSetPtr value) {
        set(std::move(value));
        fireChanged();
    }

private:
    mutable std::mutex mutex_;
    IMapTypeSetPtr set_;
};

template <class T> std::shared_ptr<T> makeFiltered(const std::shared_ptr<TestSource>& source) {
    auto filtered = std::make_shared<T>(std::make_shared<MapTypeSetBuilderFactory>(),
                                        std::static_pointer_cast<IMapTypeSetChangeable>(source));
    filtered->subscribe();
    return filtered;
}

// Proof 1. u_MapTypeSetChangeableBySourceSetWithFilter.pas:95 builds from the subclass filter (:118), in
// source order (:114-121). Source order is given mixed on purpose.
TEST(MapTypeSetChangeableBySourceSetWithFilter, InitialContentIsFilteredInSourceOrderForEachClass) {
    const auto source = std::make_shared<TestSource>(makeSet({
        kmlMap(2),            // kml
        licensedMap(1, true), // bitmap + licence
        plainMap(5),
        licensedMap(4), // licence
        bitmapMap(3),   // bitmap
    }));
    EXPECT_EQ(guidsOf(makeFiltered<MapTypeSetChangeableBySourceSetWithFilterBitmap>(source)->getStatic()),
              (std::vector<QUuid>{id(1), id(3)})); // :194 IsBitmapTiles
    EXPECT_EQ(guidsOf(makeFiltered<MapTypeSetChangeableBySourceSetWithFilterVector>(source)->getStatic()),
              (std::vector<QUuid>{id(2)})); // :203 IsKmlTiles
    EXPECT_EQ(
        guidsOf(makeFiltered<MapTypeSetChangeableBySourceSetWithFilterLicenseNotEmpty>(source)->getStatic()),
        (std::vector<QUuid>{id(1), id(4)})); // :211-214
}

// :108-124: a null source set gives an empty result (null, an empty builder result).
TEST(MapTypeSetChangeableBySourceSetWithFilter, NullSourceGivesEmptyStatic) {
    const auto source = std::make_shared<TestSource>(nullptr);
    EXPECT_EQ(makeFiltered<MapTypeSetChangeableBySourceSetWithFilterBitmap>(source)->getStatic(), nullptr);
}

// Proof 2 (:142-186), bitmap filter.
TEST(MapTypeSetChangeableBySourceSetWithFilter, ChangeTable) {
    struct Row {
        const char* name;
        std::vector<IMapTypePtr> before;
        bool beforeNull;
        std::vector<IMapTypePtr> after;
        bool afterNull;
        int notifications;
        std::vector<QUuid> content; // static after the change
    };
    const auto b1 = bitmapMap(1);
    const auto b2 = bitmapMap(2);
    const auto p1 = plainMap(3);
    const std::vector<Row> rows = {
        {"add a non-matching map", {b1}, false, {b1, p1}, false, 0, {id(1)}},
        {"add a matching map", {b1}, false, {b1, b2}, false, 1, {id(1), id(2)}},
        {"remove a matching map", {b1, b2}, false, {b1}, false, 1, {id(1)}},
        {"remove a non-matching map", {b1, p1}, false, {b1}, false, 0, {id(1)}},
        {"replace by an equal set", {b1, p1}, false, {b1, p1}, false, 0, {id(1)}},
        {"matching map replaced by non-matching", {b1}, false, {p1}, false, 1, {}},
        {"source set to null with matching maps before", {b1}, false, {}, true, 1, {}},
        {"null to null", {}, true, {}, true, 0, {}},
        {"null to a matching set", {}, true, {b2}, false, 1, {id(2)}},
        {"null to a non-matching set", {}, true, {p1}, false, 0, {}},
        {"a non-matching set to null", {p1}, false, {}, true, 0, {}},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        const auto source = std::make_shared<TestSource>(r.beforeNull ? nullptr : makeSet(r.before));
        const auto filtered = makeFiltered<MapTypeSetChangeableBySourceSetWithFilterBitmap>(source);
        int fired = 0;
        const auto sub = filtered->changed().subscribe([&] { ++fired; });
        const auto before = filtered->getStatic();
        source->change(r.afterNull ? nullptr : makeSet(r.after));
        EXPECT_EQ(fired, r.notifications);
        EXPECT_EQ(guidsOf(filtered->getStatic()), r.content);
        if (r.notifications == 0) {
            EXPECT_EQ(filtered->getStatic(), before); // the same static object
        } else {
            EXPECT_NE(filtered->getStatic(), before);
        }
    }
}

// Proof 3: the remembered source is replaced by every change (:176), so the next change is judged against
// the previous NEW set. Step 1 is a "0" row (a non-matching map appears), step 2 removes it again and adds a
// matching one.
TEST(MapTypeSetChangeableBySourceSetWithFilter, TwoStepRowJudgesAgainstTheLatestSource) {
    const auto b1 = bitmapMap(1);
    const auto b2 = bitmapMap(2);
    const auto p1 = plainMap(3);
    const auto source = std::make_shared<TestSource>(makeSet({b1}));
    const auto filtered = makeFiltered<MapTypeSetChangeableBySourceSetWithFilterBitmap>(source);
    int fired = 0;
    const auto sub = filtered->changed().subscribe([&] { ++fired; });

    source->change(makeSet({b1, p1}));
    EXPECT_EQ(fired, 0);
    source->change(makeSet({b1, p1, b2}));
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(guidsOf(filtered->getStatic()), (std::vector<QUuid>{id(1), id(2)}));
    source->change(makeSet({b1, b2})); // p1 removed: non-matching
    EXPECT_EQ(fired, 1);
    source->change(makeSet({b2})); // b1 removed: matching
    EXPECT_EQ(fired, 2);
    EXPECT_EQ(guidsOf(filtered->getStatic()), (std::vector<QUuid>{id(2)}));
}

// Maps are matched by GUID (:157-160, :167-170): a new object under the same GUID is not a change, and the
// static keeps the old object.
TEST(MapTypeSetChangeableBySourceSetWithFilter, MapsAreMatchedByGuidNotByObject) {
    const auto old = bitmapMap(1);
    const auto source = std::make_shared<TestSource>(makeSet({old}));
    const auto filtered = makeFiltered<MapTypeSetChangeableBySourceSetWithFilterBitmap>(source);
    int fired = 0;
    const auto sub = filtered->changed().subscribe([&] { ++fired; });
    source->change(makeSet({bitmapMap(1)}));
    EXPECT_EQ(fired, 0);
    EXPECT_EQ(filtered->getStatic()->item(0), old);
}

// The licence and the vector filter in the table too (each filter drives "changed").
TEST(MapTypeSetChangeableBySourceSetWithFilter, VectorAndLicenseFiltersDriveChanged) {
    {
        const auto source = std::make_shared<TestSource>(makeSet({bitmapMap(1)}));
        const auto filtered = makeFiltered<MapTypeSetChangeableBySourceSetWithFilterVector>(source);
        int fired = 0;
        const auto sub = filtered->changed().subscribe([&] { ++fired; });
        source->change(makeSet({bitmapMap(1), plainMap(2)}));
        EXPECT_EQ(fired, 0);
        source->change(makeSet({bitmapMap(1), plainMap(2), kmlMap(3)}));
        EXPECT_EQ(fired, 1);
        EXPECT_EQ(guidsOf(filtered->getStatic()), (std::vector<QUuid>{id(3)}));
    }
    {
        const auto source = std::make_shared<TestSource>(makeSet({bitmapMap(1)}));
        const auto filtered = makeFiltered<MapTypeSetChangeableBySourceSetWithFilterLicenseNotEmpty>(source);
        int fired = 0;
        const auto sub = filtered->changed().subscribe([&] { ++fired; });
        source->change(makeSet({bitmapMap(1), kmlMap(2)}));
        EXPECT_EQ(fired, 0);
        source->change(makeSet({bitmapMap(1), kmlMap(2), licensedMap(3)}));
        EXPECT_EQ(fired, 1);
        EXPECT_EQ(guidsOf(filtered->getStatic()), (std::vector<QUuid>{id(3)}));
    }
}

// Proof 4: the notification fires after the lock is released (:183-185). A listener calling getStatic() must
// return and see the NEW static. A hang is a FAIL through the watchdog, not a stuck test run.
TEST(MapTypeSetChangeableBySourceSetWithFilter, NotificationIsFiredOutsideTheLock) {
    struct State {
        std::shared_ptr<TestSource> source = std::make_shared<TestSource>(makeSet({bitmapMap(1)}));
        std::shared_ptr<MapTypeSetChangeableBySourceSetWithFilterBitmap> filtered =
            makeFiltered<MapTypeSetChangeableBySourceSetWithFilterBitmap>(source);
        std::vector<QUuid> seen;
        cg::core::Subscription sub;
        std::promise<void> done;
    };
    const auto state = std::make_shared<State>();
    state->sub = state->filtered->changed().subscribe(
        [s = state.get()] { s->seen = guidsOf(s->filtered->getStatic()); });
    auto finished = state->done.get_future();
    // The thread owns the state, so a hung thread does not dangle when the test returns.
    std::thread([state] {
        state->source->change(makeSet({bitmapMap(1), bitmapMap(2)}));
        state->done.set_value();
    }).detach();
    ASSERT_EQ(finished.wait_for(std::chrono::seconds(20)), std::future_status::ready)
        << "deadlock: the notification runs under the lock";
    EXPECT_EQ(state->seen, (std::vector<QUuid>{id(1), id(2)}));
}

// Proof 5 (:194, :203, :211-214): the filters. The licence is the DEFAULT-language text (element 0); a null
// map is out. A null item needs an allowNil source set.
TEST(MapTypeSetChangeableBySourceSetWithFilter, LicenseFilterReadsTheDefaultText) {
    struct Row {
        const char* name;
        QStringList license;
        bool in;
    };
    const Row rows[] = {
        {"empty default", {QString()}, false},
        {"non-empty default", {QStringLiteral(u"Лицензия")}, true},
        {"empty default, text in another language", {QString(), QStringLiteral(u"Лицензия")}, false},
        {"non-empty default, empty other language", {QStringLiteral(u"Лицензия"), QString()}, true},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        const auto source = std::make_shared<TestSource>(
            makeSet({std::make_shared<KindMap>(id(7), false, false, r.license)}));
        const auto filtered = makeFiltered<MapTypeSetChangeableBySourceSetWithFilterLicenseNotEmpty>(source);
        EXPECT_EQ(guidsOf(filtered->getStatic()), r.in ? std::vector<QUuid>{id(7)} : std::vector<QUuid>{});
    }
}

TEST(MapTypeSetChangeableBySourceSetWithFilter, NullMapIsOutForEveryFilter) {
    const auto source = std::make_shared<TestSource>(
        makeSet({nullptr, licensedMap(1, true), kmlMap(2)}, true)); // nil item stored under the zero GUID
    const auto bitmap = makeFiltered<MapTypeSetChangeableBySourceSetWithFilterBitmap>(source);
    const auto vec = makeFiltered<MapTypeSetChangeableBySourceSetWithFilterVector>(source);
    const auto lic = makeFiltered<MapTypeSetChangeableBySourceSetWithFilterLicenseNotEmpty>(source);
    EXPECT_EQ(guidsOf(bitmap->getStatic()), (std::vector<QUuid>{id(1)}));
    EXPECT_EQ(guidsOf(vec->getStatic()), (std::vector<QUuid>{id(2)}));
    EXPECT_EQ(guidsOf(lic->getStatic()), (std::vector<QUuid>{id(1)}));
}

// :98-106: the destructor removes the listener.
TEST(MapTypeSetChangeableBySourceSetWithFilter, DestructorRemovesTheListener) {
    const auto source = std::make_shared<TestSource>(makeSet({bitmapMap(1)}));
    auto filtered = makeFiltered<MapTypeSetChangeableBySourceSetWithFilterBitmap>(source);
    EXPECT_EQ(source->changed().subscriberCount(), 1U);
    filtered.reset();
    EXPECT_EQ(source->changed().subscriberCount(), 0U);
}

// Proof 6. TSan: getStatic() on one thread while the source changes on another. The two threads meet at a
// barrier in the middle of their loops, so both are alive for the second halves (overlap asserted by the
// barrier); every observed static is one of the two consistent contents.
TEST(MapTypeSetChangeableBySourceSetWithFilter, GetStaticWhileTheSourceChanges) {
    const auto b1 = bitmapMap(1);
    const auto b2 = bitmapMap(2);
    const auto one = makeSet({b1});
    const auto two = makeSet({b1, b2});
    const auto source = std::make_shared<TestSource>(one);
    const auto filtered = makeFiltered<MapTypeSetChangeableBySourceSetWithFilterBitmap>(source);
    constexpr int kWrites = 200;
    constexpr int kReads = 2000;
    std::latch start(2);
    std::barrier middle(2);
    std::atomic<int> bad{0};
    std::atomic<int> reads{0};
    std::thread writer([&] {
        start.arrive_and_wait();
        for (int i = 0; i < kWrites; ++i) {
            if (i == kWrites / 2) {
                middle.arrive_and_wait();
            }
            source->change(i % 2 == 0 ? two : one);
        }
    });
    std::thread reader([&] {
        start.arrive_and_wait();
        for (int i = 0; i < kReads; ++i) {
            if (i == kReads / 2) {
                middle.arrive_and_wait();
            }
            const auto n = filtered->getStatic()->count();
            if (n != 1 && n != 2) {
                ++bad;
            }
            ++reads;
        }
    });
    writer.join();
    reader.join();
    EXPECT_EQ(bad.load(), 0);
    EXPECT_EQ(reads.load(), kReads);
    // The last write (i = 199) put `one`.
    EXPECT_EQ(guidsOf(filtered->getStatic()), (std::vector<QUuid>{id(1)}));
}

// subscribe() racing a source change: whatever the interleaving, the final static is the filter of the final
// source (no change lost, :91-95 order kept). This test proves the end state and TSan quietness; it does not
// force the one interleaving "change between listener and read".
TEST(MapTypeSetChangeableBySourceSetWithFilter, SubscribeRacingASourceChangeLosesNothing) {
    for (int round = 0; round < 200; ++round) {
        const auto source = std::make_shared<TestSource>(makeSet({bitmapMap(1), plainMap(3)}));
        const auto filtered = std::make_shared<MapTypeSetChangeableBySourceSetWithFilterBitmap>(
            std::make_shared<MapTypeSetBuilderFactory>(), source);
        const auto next = makeSet({bitmapMap(1), plainMap(3), bitmapMap(2)});
        std::latch start(3);
        std::thread a([&] {
            start.arrive_and_wait();
            filtered->subscribe();
        });
        std::thread b([&] {
            start.arrive_and_wait();
            source->change(next);
        });
        start.arrive_and_wait();
        a.join();
        b.join();
        ASSERT_EQ(guidsOf(filtered->getStatic()), (std::vector<QUuid>{id(1), id(2)})) << "round " << round;
    }
}

// Destruction while the source is notifying, 50 rounds. TSan quietness and no crash only; afterwards the
// listener is gone.
TEST(MapTypeSetChangeableBySourceSetWithFilter, DestructionWhileTheSourceIsNotifying) {
    for (int round = 0; round < 50; ++round) {
        const auto source = std::make_shared<TestSource>(makeSet({bitmapMap(1)}));
        auto filtered = makeFiltered<MapTypeSetChangeableBySourceSetWithFilterBitmap>(source);
        const auto a = makeSet({bitmapMap(1), bitmapMap(2)});
        const auto b = makeSet({bitmapMap(1)});
        std::latch start(2);
        std::thread notifier([&] {
            start.arrive_and_wait();
            for (int i = 0; i < 20; ++i) {
                source->change(i % 2 == 0 ? a : b);
            }
        });
        start.arrive_and_wait();
        filtered.reset();
        notifier.join();
        ASSERT_EQ(source->changed().subscriberCount(), 0U) << "round " << round;
    }
}

} // namespace
