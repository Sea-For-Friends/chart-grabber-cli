// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_TileDownloaderStateInternal.pas, u_TileRequest.pas; docs/specs/maptype.md §2.13.1-2.
#include "cg/maptype/tile_downloader_state.h"
#include "cg/maptype/tile_request.h"

#include <QString>

#include <gtest/gtest.h>

#include <memory>

using namespace cg::maptype;
using namespace Qt::StringLiterals;

namespace {

TEST(TileDownloaderState, StartsEnabledWithEmptyReason) {
    const TileDownloaderStateInternal s;
    EXPECT_TRUE(s.enabled());
    EXPECT_TRUE(s.disableReason().isEmpty());
    EXPECT_TRUE(s.getStatic()->enabled);
    EXPECT_TRUE(s.getStatic()->disableReason.isEmpty());
}

TEST(TileDownloaderState, SecondDisableKeepsFirstReasonAndDoesNotNotify) {
    TileDownloaderStateInternal s;
    int n = 0;
    const auto sub = s.changed().subscribe([&] { ++n; });
    s.disable(u"first"_s);
    s.disable(u"second"_s);
    EXPECT_EQ(n, 1);
    EXPECT_FALSE(s.enabled());
    EXPECT_EQ(s.disableReason(), u"first"_s);
    EXPECT_EQ(s.getStatic()->disableReason, u"first"_s);
    EXPECT_FALSE(s.getStatic()->enabled);
}

TEST(TileDownloaderState, EnableClearsReasonAndNotifiesOnlyWhenDisabled) {
    TileDownloaderStateInternal s;
    int n = 0;
    const auto sub = s.changed().subscribe([&] { ++n; });
    s.enable();
    EXPECT_EQ(n, 0);
    s.disable(u"r"_s);
    s.enable();
    EXPECT_EQ(n, 2);
    EXPECT_TRUE(s.enabled());
    EXPECT_TRUE(s.disableReason().isEmpty());
    EXPECT_TRUE(s.getStatic()->disableReason.isEmpty());
    s.disable(u"again"_s);
    EXPECT_EQ(s.disableReason(), u"again"_s);
}

TEST(TileRequest, HoldsItsValuesAndOnlyTheSizeCheckSubtypeIsMarked) {
    const TileRequest plain({5, -7}, 12, nullptr);
    EXPECT_EQ(plain.tile(), QPoint(5, -7));
    EXPECT_EQ(plain.zoom(), 12);
    EXPECT_EQ(plain.versionInfo(), nullptr);
    const std::shared_ptr<const ITileRequest> a =
        std::make_shared<const TileRequest>(QPoint(1, 2), 3, nullptr);
    const std::shared_ptr<const ITileRequest> b =
        std::make_shared<const TileRequestWithSizeCheck>(QPoint(1, 2), 3, nullptr);
    EXPECT_EQ(dynamic_cast<const ITileRequestWithSizeCheck*>(a.get()), nullptr);
    EXPECT_NE(dynamic_cast<const ITileRequestWithSizeCheck*>(b.get()), nullptr);
    EXPECT_EQ(b->tile(), QPoint(1, 2));
}

} // namespace
