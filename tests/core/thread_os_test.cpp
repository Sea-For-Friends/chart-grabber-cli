// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/core/platform/thread_os (new code, T-0335: the Windows branch has no other proof).
#include "cg/core/platform/thread_os.h"

#include <gtest/gtest.h>

#include <thread>

using cg::core::platform::currentThreadId;
using cg::core::platform::setCurrentThreadName;

TEST(ThreadOs, ThreadIdIsPositiveStableAndDiffersBetweenThreads) {
    const long mine = currentThreadId();
    EXPECT_GT(mine, 0);
    EXPECT_EQ(currentThreadId(), mine);
    long other = 0;
    std::thread([&other] { other = currentThreadId(); }).join();
    EXPECT_GT(other, 0);
    EXPECT_NE(other, mine);
}

TEST(ThreadOs, SettingANameLongerThanTheLimitAndANonAsciiNameDoesNotThrowOrCrash) {
    std::thread([] {
        setCurrentThreadName(QStringLiteral("a-name-longer-than-fifteen-characters"));
        setCurrentThreadName(QString::fromUtf8("\xD0\x9A\xD0\xB0\xD1\x80\xD1\x82\xD1\x8B"));
        setCurrentThreadName(QString());
    }).join();
    SUCCEED();
}
