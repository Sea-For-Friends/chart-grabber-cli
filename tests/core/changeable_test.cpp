// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT (core idiom); docs/specs/core.md 2.5, test plan 7.4
#include "cg/core/changeable.h"

#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

using cg::core::Changeable;
using cg::core::Subscription;

namespace {

class Probe : public Changeable {
public:
    QStringList trace;

    void fire() { fireChanged(); }

protected:
    void doBeforeChangeNotify() override { trace.append(QStringLiteral("hook-before")); }
    void doChangeNotify() override { trace.append(QStringLiteral("hook-changed")); }
    void doAfterChangeNotify() override { trace.append(QStringLiteral("hook-after")); }
};

} // namespace

TEST(Changeable, FiringOrderIsBeforeChangedAfter) {
    Probe probe;
    QStringList order;
    const Subscription before =
        probe.beforeChanged().subscribe([&order] { order.append(QStringLiteral("before")); });
    const Subscription changed =
        probe.changed().subscribe([&order] { order.append(QStringLiteral("changed")); });
    const Subscription after =
        probe.afterChanged().subscribe([&order] { order.append(QStringLiteral("after")); });

    probe.fire();

    EXPECT_EQ(order,
              (QStringList{QStringLiteral("before"), QStringLiteral("changed"), QStringLiteral("after")}));
}

TEST(Changeable, VirtualHooksRunBeforeTheirNotifier) {
    Probe probe;
    const Subscription before =
        probe.beforeChanged().subscribe([&probe] { probe.trace.append(QStringLiteral("notify-before")); });
    const Subscription changed =
        probe.changed().subscribe([&probe] { probe.trace.append(QStringLiteral("notify-changed")); });
    const Subscription after =
        probe.afterChanged().subscribe([&probe] { probe.trace.append(QStringLiteral("notify-after")); });

    probe.fire();

    EXPECT_EQ(probe.trace, (QStringList{QStringLiteral("hook-before"), QStringLiteral("notify-before"),
                                        QStringLiteral("hook-changed"), QStringLiteral("notify-changed"),
                                        QStringLiteral("hook-after"), QStringLiteral("notify-after")}));
}

TEST(Changeable, NoSubscribersStillRunsHooks) {
    Probe probe;
    probe.fire();
    EXPECT_EQ(probe.trace, (QStringList{QStringLiteral("hook-before"), QStringLiteral("hook-changed"),
                                        QStringLiteral("hook-after")}));
}

TEST(Changeable, NotifiersAreIndependent) {
    Probe probe;
    int changedCalls = 0;
    const Subscription changed = probe.changed().subscribe([&changedCalls] { ++changedCalls; });

    EXPECT_EQ(probe.beforeChanged().subscriberCount(), 0U);
    EXPECT_EQ(probe.changed().subscriberCount(), 1U);
    EXPECT_EQ(probe.afterChanged().subscriberCount(), 0U);

    probe.fire();
    EXPECT_EQ(changedCalls, 1);
}
