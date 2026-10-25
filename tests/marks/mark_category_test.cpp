// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-033, FEAT-GEO-034 (held open). Ports u_MarkCategory.pas, u_MarkCategoryList.pas,
// u_MarkCategoryTree.pas; docs/specs/marks.md section 2.1. The legacy suite does not reach these rules.
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_list.h"
#include "cg/marks/mark_category_tree.h"
#include "cg/vector/category.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

using cg::marks::MarkCategory;
using cg::marks::MarkCategoryList;
using cg::marks::MarkCategoryPtr;
using cg::marks::MarkCategoryTree;
using cg::vector::Category;

namespace {

MarkCategoryPtr make(const QString& name, bool visible = true, int after = 3, int before = 23) {
    return std::make_shared<const MarkCategory>(name, visible, after, before);
}

TEST(MarkCategory, FieldsAreTheConstructorArguments) {
    const MarkCategory c(QStringLiteral("Дороги"), false, 5, 17);
    EXPECT_EQ(c.name(), QStringLiteral("Дороги"));
    EXPECT_FALSE(c.visible());
    EXPECT_EQ(c.afterScale(), 5);
    EXPECT_EQ(c.beforeScale(), 17);
}

TEST(MarkCategory, IsSameIsIdentity) {
    const MarkCategoryPtr a = make(QStringLiteral("A"));
    const MarkCategoryPtr twin = make(QStringLiteral("A"));
    EXPECT_TRUE(a->isSame(a.get()));
    EXPECT_FALSE(a->isSame(twin.get()));
    EXPECT_FALSE(a->isSame(nullptr));
}

TEST(MarkCategory, IsEqualLadder) {
    const MarkCategoryPtr a = make(QStringLiteral("Река"));
    EXPECT_FALSE(a->isEqual(nullptr));
    EXPECT_TRUE(a->isEqual(a.get()));
    EXPECT_TRUE(a->isEqual(make(QStringLiteral("Река")).get()));
    EXPECT_FALSE(a->isEqual(make(QStringLiteral("река")).get())) << "name compare is case-sensitive";
    EXPECT_FALSE(a->isEqual(make(QStringLiteral("Река"), false).get())) << "visible";
    EXPECT_FALSE(a->isEqual(make(QStringLiteral("Река"), true, 4).get())) << "afterScale";
    EXPECT_FALSE(a->isEqual(make(QStringLiteral("Река"), true, 3, 22).get())) << "beforeScale";
}

TEST(MarkCategory, IsEqualToPlainCategoryOfTheSameNameIsFalseButNotViceVersa) {
    const MarkCategoryPtr a = make(QStringLiteral("A"));
    const Category plain(QStringLiteral("A"));
    EXPECT_FALSE(a->isEqual(&plain)) << "Supports(ACategory, IMarkCategory) fails";
    EXPECT_TRUE(plain.isEqual(a.get())) << "TCategory.IsEqual compares the name only";
}

TEST(MarkCategoryList, BuildOfEmptyListIsNull) {
    EXPECT_EQ(MarkCategoryList::build({}), nullptr);
}

TEST(MarkCategoryList, KeepsOrderAndRejectsOutOfRange) {
    const MarkCategoryPtr a = make(QStringLiteral("B"));
    const MarkCategoryPtr b = make(QStringLiteral("A"));
    const auto list = MarkCategoryList::build({a, b});
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->count(), 2);
    EXPECT_EQ(list->item(0), a);
    EXPECT_EQ(list->item(1), b);
    EXPECT_THROW((void)list->item(2), std::out_of_range);
    EXPECT_THROW((void)list->item(-1), std::out_of_range);
}

TEST(MarkCategoryTree, SubItemOutOfRangeIsNull) {
    const auto leaf = std::make_shared<const MarkCategoryTree>(nullptr, QStringLiteral("x"),
                                                               std::vector<cg::marks::MarkCategoryTreePtr>{});
    const MarkCategoryTree node(nullptr, QString(), {leaf});
    EXPECT_EQ(node.subItemCount(), 1);
    EXPECT_EQ(node.subItem(0), leaf);
    EXPECT_EQ(node.subItem(1), nullptr);
    EXPECT_EQ(node.subItem(-1), nullptr);
    EXPECT_EQ(leaf->subItemCount(), 0);
    EXPECT_EQ(leaf->subItem(0), nullptr);
}

} // namespace
