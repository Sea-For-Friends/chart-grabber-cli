// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-UI-206; docs/specs/core.md 2.17, test plan 7.14
#include "cg/core/config_element_complex.h"

#include <QStringList>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using cg::core::ConfigElement;
using cg::core::ConfigElementChildFlags;
using cg::core::ConfigElementComplex;
using cg::core::IConfigProviderPtr;
using cg::core::IConfigWriter;
using cg::core::IConfigWriterPtr;
using cg::core::SaveLoadSlot;

namespace {

class LeafElement : public ConfigElement {
public:
    explicit LeafElement(QString id, std::vector<QString>* order = nullptr)
        : id_(std::move(id)), order_(order) {}
    void publicSetChanged() { setChanged(); }

    QString id_;
    std::vector<QString>* order_;
    std::vector<IConfigProviderPtr> readSources;
    mutable std::vector<IConfigWriterPtr> writeDests;
    int beforeChangeNotifyCalls = 0; // rework: proves finishWrite() runs the hook on every path

protected:
    void doReadConfig(const IConfigProviderPtr& src) override {
        if (order_ != nullptr) {
            order_->push_back(id_);
        }
        readSources.push_back(src);
    }
    void doWriteConfig(const IConfigWriterPtr& dst) const override { writeDests.push_back(dst); }
    void doBeforeChangeNotify() override { ++beforeChangeNotifyCalls; }
};

class ComplexUnderTest : public ConfigElementComplex {
public:
    using ConfigElementComplex::addChangeable;
    using ConfigElementComplex::addChild;

    std::vector<int>* order = nullptr;
    int subItemChangeCalls = 0;

protected:
    void doSubItemChange() override {
        ++subItemChangeCalls;
        if (order != nullptr) {
            order->push_back(0);
        }
    }
};

// A minimal IConfigWriter test double that records which sub-items were created.
class RecordingWriter : public IConfigWriter {
public:
    QStringList subItemsCreated;

    [[nodiscard]] IConfigProviderPtr subItem(QStringView) const override { return nullptr; }
    [[nodiscard]] QVariant value(QStringView) const override { return {}; }
    [[nodiscard]] QByteArray binary(QStringView) const override { return {}; }
    [[nodiscard]] QStringList subItemNames() const override { return {}; }
    [[nodiscard]] QStringList valueNames() const override { return {}; }
    [[nodiscard]] IConfigWriterPtr orCreateSubItem(QStringView id) override {
        subItemsCreated.append(id.toString());
        return std::make_shared<RecordingWriter>();
    }
    void deleteSubItem(QStringView) override {}
    void deleteValue(QStringView) override {}
    void deleteValues() override {}
    void writeValue(QStringView, const QVariant&) override {}
    void writeBinary(QStringView, const QByteArray&) override {}
};

} // namespace

TEST(ConfigElementComplex, ReadConfigForwardsToChildrenInAddOrder) {
    std::vector<QString> order;
    ComplexUnderTest parent;
    auto a = std::make_shared<LeafElement>(QStringLiteral("A"), &order);
    auto b = std::make_shared<LeafElement>(QStringLiteral("B"), &order);
    parent.addChild(a, SaveLoadSlot{});
    parent.addChild(b, SaveLoadSlot{});
    parent.readConfig(nullptr);
    EXPECT_EQ(order, (std::vector<QString>{QStringLiteral("A"), QStringLiteral("B")}));
}

TEST(ConfigElementComplex, WriteConfigUsesSubItemSlot) {
    ComplexUnderTest parent;
    auto child = std::make_shared<LeafElement>(QStringLiteral("Child"));
    parent.addChild(child, SaveLoadSlot{QStringLiteral("ChildSlot")});
    auto writer = std::make_shared<RecordingWriter>();
    parent.writeConfig(writer);
    ASSERT_EQ(writer->subItemsCreated.size(), 1);
    EXPECT_EQ(writer->subItemsCreated.at(0), QStringLiteral("ChildSlot"));
    EXPECT_EQ(child->writeDests.size(), 1u);
}

TEST(ConfigElementComplex, EmptySlotUsesParentProvider) {
    ComplexUnderTest parent;
    auto child = std::make_shared<LeafElement>(QStringLiteral("Child"));
    parent.addChild(child, SaveLoadSlot{});
    auto writer = std::make_shared<RecordingWriter>();
    parent.writeConfig(writer);
    EXPECT_TRUE(writer->subItemsCreated.isEmpty());
    ASSERT_EQ(child->writeDests.size(), 1u);
    EXPECT_EQ(child->writeDests.at(0), writer);
}

TEST(ConfigElementComplex, ChildChangeNotifiesParentOnce) {
    ComplexUnderTest parent;
    auto child = std::make_shared<LeafElement>(QStringLiteral("Child"));
    ConfigElementChildFlags flags; // listenToChanges is what this test needs (finding 3: defaults
    flags.listenToChanges = true;  // are all-false now, so opt in explicitly)
    parent.addChild(child, SaveLoadSlot{}, flags);
    int notifications = 0;
    const auto sub = parent.changed().subscribe([&] { ++notifications; });
    {
        const auto guard = child->writeLock();
        child->publicSetChanged();
    }
    EXPECT_EQ(notifications, 1);
}

TEST(ConfigElementComplex, DoSubItemChangeRunsBeforeParentNotification) {
    std::vector<int> order;
    ComplexUnderTest parent;
    parent.order = &order;
    auto child = std::make_shared<LeafElement>(QStringLiteral("Child"));
    ConfigElementChildFlags flags;
    flags.listenToChanges = true; // finding 3: defaults are all-false now
    parent.addChild(child, SaveLoadSlot{}, flags);
    const auto sub = parent.changed().subscribe([&] { order.push_back(1); });
    {
        const auto guard = child->writeLock();
        child->publicSetChanged();
    }
    EXPECT_EQ(order, (std::vector<int>{0, 1}));
}

TEST(ConfigElementComplex, LockPropagationFollowsFlags) {
    ComplexUnderTest parent;
    auto flagged = std::make_shared<LeafElement>(QStringLiteral("Flagged"));
    auto unflagged = std::make_shared<LeafElement>(QStringLiteral("Unflagged"));
    ConfigElementChildFlags flagsOn;
    flagsOn.propagateReadLock = true;
    flagsOn.propagateWriteLock = true;
    flagsOn.propagateBatch = true;
    flagsOn.listenToChanges = true;
    parent.addChild(flagged, SaveLoadSlot{}, flagsOn);
    parent.addChild(unflagged, SaveLoadSlot{}); // defaults: all flags false (finding 3)

    std::atomic<bool> parentShouldRelease{false};
    std::atomic<bool> flaggedAcquired{false};
    std::atomic<bool> unflaggedAcquired{false};
    std::thread writer([&] {
        const auto guard = parent.writeLock(); // ConfigElementComplex::writeLock(): propagates
        while (!parentShouldRelease.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    std::thread r1([&] {
        const auto guard = flagged->writeLock();
        flaggedAcquired.store(true);
    });
    std::thread r2([&] {
        const auto guard = unflagged->writeLock();
        unflaggedAcquired.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(flaggedAcquired.load());  // propagated: still locked by the parent's guard
    EXPECT_TRUE(unflaggedAcquired.load()); // not propagated: free
    parentShouldRelease.store(true);
    writer.join();
    r1.join();
    r2.join();
}

TEST(ConfigElementComplex, BatchPropagationFollowsFlags) {
    ComplexUnderTest parent;
    auto child = std::make_shared<LeafElement>(QStringLiteral("Batched"));
    ConfigElementChildFlags flags;
    flags.propagateReadLock = false;
    flags.propagateWriteLock = false;
    flags.propagateBatch = true;
    flags.listenToChanges = false; // isolate from the listener mechanism below
    parent.addChild(child, SaveLoadSlot{}, flags);

    int childNotifications = 0;
    const auto sub = child->changed().subscribe([&] { ++childNotifications; });
    {
        const auto parentGuard = parent.writeLock(); // bumps the child's batch counter, no child lock
        std::atomic<bool> acquired{false};
        std::thread t([&] {
            const auto guard = child->writeLock(); // the child's own mutex is free
            child->publicSetChanged();
            acquired.store(true);
        });
        t.join();
        EXPECT_TRUE(acquired.load());
        EXPECT_EQ(childNotifications, 0); // deferred: still inside the parent's open write scope
        EXPECT_EQ(child->beforeChangeNotifyCalls, 0);
    }
    EXPECT_EQ(childNotifications, 1); // fires once the parent's write scope ends
    // The BatchToken path (config_element_complex.cpp's propagateBatch) is the only place this
    // hook runs for this sequence -- WriteGuard's own under-lock pre-pass does not fire here
    // because the child's batch depth is 2 (parent's BatchToken + the child's own writeLock()) at
    // that point, not 1. Without u_ChangeableBase.pas's DoBeforeChangeNotify running in
    // finishWrite(), this stays 0 and `changed()` publishes a stale snapshot.
    EXPECT_EQ(child->beforeChangeNotifyCalls, 1);
}

TEST(ConfigElementComplex, DestructorUnsubscribesFromChildren) {
    auto child = std::make_shared<LeafElement>(QStringLiteral("Child"));
    {
        ComplexUnderTest parent;
        ConfigElementChildFlags flags;
        flags.listenToChanges = true; // must actually subscribe for this test to prove anything
        parent.addChild(child, SaveLoadSlot{}, flags);
    } // parent destructs; its subscription to child->changed() must not outlive it
    int notifications = 0;
    const auto sub = child->changed().subscribe([&] { ++notifications; });
    {
        const auto guard = child->writeLock();
        child->publicSetChanged();
    }
    EXPECT_EQ(notifications, 1); // only our subscription fired; no crash from a dangling one
}

TEST(ConfigElementComplex, DefaultFlagsAllowChildAccessInsideParentWriteScope) {
    // finding 3: with the pre-fix all-true ChildFlags defaults, this self-deadlocks -- the
    // parent's writeLock() would already hold the child's (non-recursive) mutex via propagation,
    // so a second writeLock() on the same child from inside the parent's scope never returns.
    ComplexUnderTest parent;
    auto child = std::make_shared<LeafElement>(QStringLiteral("Child"));
    parent.addChild(child, SaveLoadSlot{}); // defaults: no propagation
    std::atomic<bool> childAcquired{false};
    {
        const auto parentGuard = parent.writeLock();
        std::thread t([&] {
            const auto childGuard = child->writeLock();
            child->publicSetChanged();
            childAcquired.store(true);
        });
        t.join();
    }
    EXPECT_TRUE(childAcquired.load());
}
