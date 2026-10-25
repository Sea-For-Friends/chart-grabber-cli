// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-037. Ports u_Category.pas; docs/specs/vector.md §2.1, §7.2.
#include "cg/vector/category.h"

#include <gtest/gtest.h>

using cg::vector::Category;

namespace {

TEST(Category, NameIsTheConstructorArgument) {
    EXPECT_EQ(Category(QStringLiteral("Roads")).name(), QStringLiteral("Roads"));
}

TEST(Category, IsSameIsIdentity) {
    const Category a(QStringLiteral("A"));
    const Category b(QStringLiteral("A"));
    EXPECT_TRUE(a.isSame(&a));
    EXPECT_FALSE(a.isSame(&b));
    EXPECT_FALSE(a.isSame(nullptr));
}

TEST(Category, IsEqualLadder) {
    const Category a(QStringLiteral("A"));
    const Category sameName(QStringLiteral("A"));
    const Category otherCase(QStringLiteral("a"));
    const Category other(QStringLiteral("B"));
    EXPECT_FALSE(a.isEqual(nullptr));    // null
    EXPECT_TRUE(a.isEqual(&a));          // identity
    EXPECT_TRUE(a.isEqual(&sameName));   // same name
    EXPECT_FALSE(a.isEqual(&otherCase)); // case-sensitive
    EXPECT_FALSE(a.isEqual(&other));
}

} // namespace
