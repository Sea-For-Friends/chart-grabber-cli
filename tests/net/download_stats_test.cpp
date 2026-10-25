// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// T-0156: DownloadInfo start values (u_DownloadInfoSimple.pas:62-73, Add :75-87, Reset :109-118).
#include "cg/net/download_stats.h"

#include <gtest/gtest.h>

#include <memory>

using cg::net::DownloadInfo;

TEST(DownloadInfo, DefaultsAreZero) {
    const DownloadInfo info;
    EXPECT_EQ(info.tileCount(), 0U);
    EXPECT_EQ(info.size(), 0U);
}

TEST(DownloadInfo, StartValuesAreTheArgumentsAndAddForwardsOnlyTheDelta) {
    const auto parent = std::make_shared<DownloadInfo>();
    DownloadInfo info(parent, 7, 1000);
    EXPECT_EQ(info.tileCount(), 7U);
    EXPECT_EQ(info.size(), 1000U);
    EXPECT_EQ(parent->tileCount(), 0U); // start values are not forwarded
    EXPECT_EQ(parent->size(), 0U);

    info.add(3, 40);
    EXPECT_EQ(info.tileCount(), 10U);
    EXPECT_EQ(info.size(), 1040U);
    EXPECT_EQ(parent->tileCount(), 3U);
    EXPECT_EQ(parent->size(), 40U);
}

TEST(DownloadInfo, ResetZeroesSelfOnly) {
    const auto parent = std::make_shared<DownloadInfo>(nullptr, 5, 50);
    DownloadInfo info(parent, 7, 1000);
    info.reset();
    EXPECT_EQ(info.tileCount(), 0U);
    EXPECT_EQ(info.size(), 0U);
    EXPECT_EQ(parent->tileCount(), 5U);
    EXPECT_EQ(parent->size(), 50U);
}
