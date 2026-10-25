// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports i_StorageStateProxy.pas, u_StorageStateProxy.pas; docs/specs/storage.md §2.22.2 (ticket T-0136, P-4c)
#include "cg/core/content_type.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/storage/storage_state_proxy.h"
#include "cg/storage/tile_pyramid_notifier.h"

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>

using cg::core::ErrorCode;
using cg::storage::ITileStorage;
using cg::storage::ITileStoragePtr;
using cg::storage::StorageState;
using cg::storage::StorageStateHolder;
using cg::storage::StorageStateProxy;
using cg::storage::TileStorageAbilities;

namespace {

// An ITileStorage whose state() / stateChangeable() are a real StorageStateHolder; nothing else is used.
class StateOnlyStorage final : public ITileStorage {
public:
    static cg::core::Unexpected unsupported() {
        return cg::core::fail(ErrorCode::Unsupported, QStringLiteral("state-only double"));
    }

    explicit StateOnlyStorage(TileStorageAbilities abilities) : holder_(abilities) {}

    StorageStateHolder& holder() { return holder_; }

    cg::storage::TileStorageTypeAbilities typeAbilities() const override { return {}; }
    cg::storage::StorageStatePtr state() const override { return holder_.get(); }
    cg::core::Changeable& stateChangeable() override { return holder_; }
    const cg::geo::IProjectionSet& projectionSet() const override { return *projectionSet_; }
    cg::storage::TilePyramidNotifier& tileNotifier() override { return notifier_; }
    QString tileFileName(cg::geo::PointI, quint8, const cg::storage::MapVersionInfoPtr&) const override {
        return {};
    }
    cg::core::Result<cg::storage::TileInfo> tileInfo(cg::geo::PointI, quint8,
                                                     const cg::storage::MapVersionInfoPtr&,
                                                     cg::storage::LoadMode) const override {
        return unsupported();
    }
    cg::core::Result<cg::storage::TileInfo> tileInfoEx(cg::geo::PointI, quint8,
                                                       const cg::storage::MapVersionRequest&,
                                                       cg::storage::LoadMode) const override {
        return unsupported();
    }
    cg::core::Result<bool> deleteTile(cg::geo::PointI, quint8,
                                      const cg::storage::MapVersionInfoPtr&) override {
        return unsupported();
    }
    cg::core::Result<bool> saveTile(cg::geo::PointI, quint8, const cg::storage::MapVersionInfoPtr&,
                                    cg::storage::TileDate, QByteArrayView, const QByteArray&, bool) override {
        return unsupported();
    }
    cg::core::Result<cg::storage::MapVersionList>
    listVersions(cg::geo::PointI, quint8, const cg::storage::MapVersionRequest&) const override {
        return unsupported();
    }
    cg::core::Result<cg::storage::TileRectInfoPtr> tileRectInfo(cg::geo::RectI, quint8,
                                                                const cg::storage::MapVersionRequest&,
                                                                std::stop_token) const override {
        return unsupported();
    }
    cg::core::Result<cg::storage::EnumTileInfoPtr> scanTiles(bool, bool) override { return unsupported(); }

private:
    static cg::geo::ProjectionSetPtr makeProjectionSet() {
        static const auto datums = std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{});
        static const cg::geo::ProjectionSetFactory factory{cg::geo::HashSeed{}, datums};
        return factory.byCode(cg::geo::kProjEpsgGoogle, cg::geo::kTileSplitQuadrate256x256);
    }

    StorageStateHolder holder_;
    cg::geo::ProjectionSetPtr projectionSet_ = makeProjectionSet();
    cg::storage::TilePyramidNotifier notifier_;
};

std::shared_ptr<StateOnlyStorage> makeStorage(bool read, bool add) {
    return std::make_shared<StateOnlyStorage>(TileStorageAbilities{read, false, add, false, false});
}

struct Counter {
    explicit Counter(StorageStateProxy& proxy) : sub(proxy.changed().subscribe([this] { ++fired; })) {}
    int fired = 0;
    cg::core::Subscription sub;
};

void expectState(const cg::storage::StorageStatePtr& s, bool read, bool scan, bool add, bool del, bool repl) {
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(*s, (StorageState{read, scan, add, del, repl}));
}

} // namespace

TEST(StorageStateProxy, NewProxyHasNoTargetAndNoAccess) {
    const StorageStateProxy proxy;
    EXPECT_EQ(proxy.target(), nullptr);
    expectState(proxy.getStatic(), false, false, false, false, false);
}

TEST(StorageStateProxy, SetTargetPublishesItsStateOnceAndSameTargetIsSilent) {
    StorageStateProxy proxy;
    Counter c(proxy);
    const auto a = makeStorage(true, true);
    proxy.setTarget(a);
    EXPECT_EQ(c.fired, 1);
    EXPECT_EQ(proxy.target(), a);
    expectState(proxy.getStatic(), true, false, true, false, false);
    EXPECT_EQ(proxy.getStatic(), a->state()); // the store keeps the new pointer
    proxy.setTarget(a);
    EXPECT_EQ(c.fired, 1);
}

TEST(StorageStateProxy, TargetChangeIsPublishedOnlyWhenTheStateDiffers) {
    StorageStateProxy proxy;
    const auto a = makeStorage(true, true);
    proxy.setTarget(a);
    Counter c(proxy);
    a->holder().setScanAccess(true); // clamped by abilities (scan not allowed): no transition
    EXPECT_EQ(c.fired, 0);
    a->holder().setReadAccess(false);
    EXPECT_EQ(c.fired, 1);
    expectState(proxy.getStatic(), false, false, true, false, false);
    a->holder().setReadAccess(false); // no transition in the holder
    EXPECT_EQ(c.fired, 1);
}

TEST(StorageStateProxy, TargetWithEqualStateIsSilentAndOnlyTheNewTargetIsFollowed) {
    StorageStateProxy proxy;
    const auto a = makeStorage(true, true);
    const auto b = makeStorage(true, true);
    proxy.setTarget(a);
    Counter c(proxy);
    proxy.setTarget(b);
    EXPECT_EQ(c.fired, 0);
    EXPECT_EQ(proxy.target(), b);
    a->holder().setReadAccess(false); // old target: ignored
    EXPECT_EQ(c.fired, 0);
    expectState(proxy.getStatic(), true, false, true, false, false);
    b->holder().setAddAccess(false);
    EXPECT_EQ(c.fired, 1);
    expectState(proxy.getStatic(), true, false, false, false, false);
}

TEST(StorageStateProxy, NullTargetPublishesNoAccessOnce) {
    StorageStateProxy proxy;
    const auto a = makeStorage(true, true);
    proxy.setTarget(a);
    Counter c(proxy);
    proxy.setTarget(nullptr);
    EXPECT_EQ(c.fired, 1);
    EXPECT_EQ(proxy.target(), nullptr);
    expectState(proxy.getStatic(), false, false, false, false, false);
    proxy.setTarget(nullptr);
    EXPECT_EQ(c.fired, 1);
    EXPECT_EQ(a->holder().changed().subscriberCount(), 0U); // unsubscribed from the old target
    a->holder().setReadAccess(false);
    EXPECT_EQ(c.fired, 1);
}

TEST(StorageStateProxy, DestroyedTargetKeepsLastSnapshotAndCanBeReplaced) {
    StorageStateProxy proxy;
    auto a = makeStorage(true, true);
    proxy.setTarget(a);
    Counter c(proxy);
    a.reset(); // the proxy holds it weakly: it dies here
    EXPECT_EQ(proxy.target(), nullptr);
    expectState(proxy.getStatic(), true, false, true, false, false);
    EXPECT_EQ(c.fired, 0);
    proxy.setTarget(nullptr); // a destroyed target is not "no target"
    EXPECT_EQ(c.fired, 1);
    expectState(proxy.getStatic(), false, false, false, false, false);

    auto d = makeStorage(true, true);
    proxy.setTarget(d);
    d.reset();
    const auto e = makeStorage(false, true);
    proxy.setTarget(e); // differs from the dead d even though d's address may be reused
    EXPECT_EQ(c.fired, 3);
    e->holder().setAddAccess(false);
    EXPECT_EQ(c.fired, 4);
    expectState(proxy.getStatic(), false, false, false, false, false);
}

TEST(StorageStateProxy, DestroyedProxyIsUnsubscribedFromItsTarget) {
    const auto a = makeStorage(true, true);
    {
        StorageStateProxy proxy;
        proxy.setTarget(a);
        EXPECT_EQ(a->holder().changed().subscriberCount(), 1U);
    }
    EXPECT_EQ(a->holder().changed().subscriberCount(), 0U);
    a->holder().setReadAccess(false); // must not reach the dead proxy (ASan)
}

TEST(StorageStateProxy, SubscriberReadsTheNewSnapshotInsideTheNotification) {
    StorageStateProxy proxy;
    const auto a = makeStorage(true, true);
    cg::storage::StorageStatePtr seen;
    const auto sub = proxy.changed().subscribe([&] { seen = proxy.getStatic(); });
    proxy.setTarget(a); // from setTarget
    expectState(seen, true, false, true, false, false);
    a->holder().setReadAccess(false); // from onTargetChange
    expectState(seen, false, false, true, false, false);
}

TEST(StorageStateProxy, TargetNotificationWhoseStateIsAlreadyPublishedIsSilent) {
    // A holder subscriber registered before the proxy reverts the flag inside its own slot, so by the time
    // the proxy's slot runs the target's state equals the published one (u_StorageStateProxy.pas:123).
    StorageStateProxy proxy;
    const auto a = makeStorage(true, true);
    bool reverted = false;
    const auto revert = a->holder().changed().subscribe([&] {
        if (!reverted) {
            reverted = true;
            a->holder().setReadAccess(true);
        }
    });
    proxy.setTarget(a);
    const Counter c(proxy);
    a->holder().setReadAccess(false);
    EXPECT_TRUE(reverted);
    EXPECT_EQ(c.fired, 0);
    expectState(proxy.getStatic(), true, false, true, false, false);
}
