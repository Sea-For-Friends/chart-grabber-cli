// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-002. Ports u_LayerDrawConfig.pas; docs/specs/maptype.md §2.7.2, §7.2 item 6.
#include "cg/maptype/layer_draw_config.h"

#include <gtest/gtest.h>

#include "maptype_test_util.h"

using cg::maptype::LayerDrawConfig;
using cg::maptype::test::iniFrom;

namespace {

TEST(LayerDrawConfig, StartsAtTheZmpValueAndWritesNothingWhileUnchanged) {
    const auto root = iniFrom("[S]\r\nLayerZOrder=9\r\nKeep=1\r\n");
    LayerDrawConfig c(5);
    EXPECT_EQ(c.layerZOrder(), 5);
    c.writeConfig(root->orCreateSubItem(u"S"));
    EXPECT_EQ(root->subItem(u"S")->valueNames(), QStringList{QStringLiteral("Keep")});
}

TEST(LayerDrawConfig, ChangedValueRoundTripsThroughTheSection) {
    const auto root = iniFrom({});
    LayerDrawConfig c(5);
    c.setLayerZOrder(-3);
    c.writeConfig(root->orCreateSubItem(u"S"));
    EXPECT_EQ(root->subItem(u"S")->readInt(u"LayerZOrder", 0), -3);
    LayerDrawConfig back(5);
    back.readConfig(root->subItem(u"S"));
    EXPECT_EQ(back.layerZOrder(), -3);
}

TEST(LayerDrawConfig, SetterNotifiesOnChangeOnlyButReadAlways) {
    LayerDrawConfig c(5);
    int n = 0;
    const auto sub = c.changed().subscribe([&] { ++n; });
    c.setLayerZOrder(5);
    EXPECT_EQ(n, 0);
    c.setLayerZOrder(6);
    EXPECT_EQ(n, 1);
    c.readConfig(iniFrom("[S]\r\n")->subItem(u"S")); // key absent: value stays, still notifies
    EXPECT_EQ(n, 2);
    EXPECT_EQ(c.layerZOrder(), 6);
}

} // namespace
