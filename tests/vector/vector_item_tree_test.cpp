// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-037. Ports u_VectorItemTree.pas; docs/specs/vector.md §2.7, §7.2.
#include "cg/vector/vector_item_tree.h"

#include <gtest/gtest.h>

using namespace cg::vector;

TEST(VectorItemTree, NodeAccessorsAndBounds) {
    const auto leaf =
        std::make_shared<VectorItemTree>(QStringLiteral("leaf"), nullptr, std::vector<VectorItemTreePtr>{});
    const auto items = std::make_shared<VectorItemSubset>(0, std::vector<VectorDataItemPtr>{});
    const VectorItemTree root(QStringLiteral("root"), items, {leaf, nullptr});

    EXPECT_EQ(root.name(), QStringLiteral("root"));
    EXPECT_EQ(root.items(), items);
    EXPECT_EQ(leaf->items(), nullptr); // a tree's Items may be nil
    ASSERT_EQ(root.subTreeItemCount(), 2);
    EXPECT_EQ(root.subTreeItem(0), leaf);
    EXPECT_EQ(root.subTreeItem(1), nullptr); // a nil child is returned as is
    // Out of range answers null, never throws (u_VectorItemTree.pas:77-86).
    EXPECT_EQ(root.subTreeItem(-1), nullptr);
    EXPECT_EQ(root.subTreeItem(2), nullptr);
    EXPECT_EQ(leaf->subTreeItemCount(), 0); // legacy nil list == empty list
    EXPECT_EQ(leaf->subTreeItem(0), nullptr);
}
