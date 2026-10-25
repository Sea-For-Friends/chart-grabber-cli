// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-033. Ports u_ReadWriteStateInternal.pas, u_ReadWriteStateInternalByOther.pas,
// u_ReadWriteStateStatic.pas (no DUnit suite exists; cases derived from the Pascal).
#include "cg/core/read_write_state_internal.h"
#include "cg/core/read_write_state_internal_by_other.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using cg::core::ReadWriteStateChangeablePtr;
using cg::core::ReadWriteStateInternal;
using cg::core::ReadWriteStateInternalByOther;
using cg::core::Subscription;

namespace {

enum class Op { setRead, setWrite };

struct Row {
    bool read0;
    bool write0;
    Op op;
    bool value;
    bool read1;
    bool write1;
    bool notified;
};

class ReadWriteStateTransition : public testing::TestWithParam<Row> {};

} // namespace

// u_ReadWriteStateInternal.pas:110-154. The last row is the legacy quirk: the constructor stores
// (read = false, write = true) unchecked, and setReadAccess(false) is then "no change".
TEST_P(ReadWriteStateTransition, SettersAndNotification) {
    const Row row = GetParam();
    ReadWriteStateInternal state(row.read0, row.write0);
    const auto before = state.getStatic();
    int fired = 0;
    const Subscription sub = state.changed().subscribe([&fired] { ++fired; });

    if (row.op == Op::setRead) {
        state.setReadAccess(row.value);
    } else {
        state.setWriteAccess(row.value);
    }

    EXPECT_EQ(state.readAccess(), row.read1);
    EXPECT_EQ(state.writeAccess(), row.write1);
    EXPECT_EQ(state.getStatic()->readAccess, row.read1);
    EXPECT_EQ(state.getStatic()->writeAccess, row.write1);
    EXPECT_EQ(fired, row.notified ? 1 : 0);
    EXPECT_EQ(before->readAccess, row.read0); // a published snapshot is never mutated
    EXPECT_EQ(before->writeAccess, row.write0);
    EXPECT_EQ(state.getStatic() != before, row.notified);
}

INSTANTIATE_TEST_SUITE_P(
    Legacy, ReadWriteStateTransition,
    testing::Values(Row{true, true, Op::setRead, false, false, false, true}, // lowering read lowers write
                    Row{true, false, Op::setRead, false, false, false, true},
                    Row{false, false, Op::setRead, false, false, false, false},
                    Row{true, true, Op::setRead, true, true, true, false},
                    Row{false, false, Op::setRead, true, true, false, true},
                    Row{true, false, Op::setWrite, true, true, true, true},
                    Row{false, false, Op::setWrite, true, true, true, true}, // raising write raises read
                    Row{true, true, Op::setWrite, false, true, false, true}, // lowering write keeps read
                    Row{false, false, Op::setWrite, false, false, false, false},
                    Row{false, true, Op::setRead, false, false, true, false}, // quirk: unchecked constructor
                    Row{false, true, Op::setWrite, true, false, true, false}));

TEST(ReadWriteStateInternal, ChangeNotificationFiresBeforeChangedAfter) {
    ReadWriteStateInternal state(false, false);
    std::vector<int> order;
    const Subscription a = state.beforeChanged().subscribe([&order] { order.push_back(1); });
    const Subscription b = state.changed().subscribe([&order] { order.push_back(2); });
    const Subscription c = state.afterChanged().subscribe([&order] { order.push_back(3); });
    state.setWriteAccess(true);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(ReadWriteStateByOther, WithoutSourceReportsNeitherReadNorWrite) {
    ReadWriteStateInternalByOther proxy;
    EXPECT_FALSE(proxy.getStatic()->readAccess);
    EXPECT_FALSE(proxy.getStatic()->writeAccess);
}

TEST(ReadWriteStateByOther, FollowsSourceAndForwardsItsChanges) {
    const auto source = std::make_shared<ReadWriteStateInternal>(true, false);
    ReadWriteStateInternalByOther proxy;
    int fired = 0;
    const Subscription sub = proxy.changed().subscribe([&fired] { ++fired; });

    proxy.setOther(source);
    EXPECT_EQ(fired, 1); // swapping the source notifies
    EXPECT_TRUE(proxy.getStatic()->readAccess);
    EXPECT_FALSE(proxy.getStatic()->writeAccess);

    source->setWriteAccess(true);
    EXPECT_EQ(fired, 2);
    EXPECT_TRUE(proxy.getStatic()->writeAccess);

    proxy.setOther(source); // same source: nothing happens
    EXPECT_EQ(fired, 2);
}

TEST(ReadWriteStateByOther, SwitchingOrClearingSourceUnsubscribesFromTheOldOne) {
    const auto first = std::make_shared<ReadWriteStateInternal>(true, true);
    const auto second = std::make_shared<ReadWriteStateInternal>(true, false);
    ReadWriteStateInternalByOther proxy;
    int fired = 0;
    const Subscription sub = proxy.changed().subscribe([&fired] { ++fired; });

    proxy.setOther(first);
    proxy.setOther(second);
    EXPECT_EQ(fired, 2);
    EXPECT_EQ(first->changed().subscriberCount(), 0U);
    first->setReadAccess(false);
    EXPECT_EQ(fired, 2);
    EXPECT_FALSE(proxy.getStatic()->writeAccess);

    proxy.setOther(ReadWriteStateChangeablePtr());
    EXPECT_EQ(fired, 3);
    EXPECT_EQ(second->changed().subscriberCount(), 0U);
    EXPECT_FALSE(proxy.getStatic()->readAccess); // back to the default
}

TEST(ReadWriteStateByOther, KeepsSourceAliveAndUnsubscribesWhenDestroyed) {
    auto source = std::make_shared<ReadWriteStateInternal>(true, true);
    const std::weak_ptr<ReadWriteStateInternal> weak = source;
    {
        ReadWriteStateInternalByOther proxy;
        proxy.setOther(source);
        source.reset();
        ASSERT_FALSE(weak.expired()); // the proxy holds the source
        EXPECT_TRUE(proxy.getStatic()->writeAccess);
        EXPECT_EQ(weak.lock()->changed().subscriberCount(), 1U);
    }
    EXPECT_TRUE(weak.expired());
}
