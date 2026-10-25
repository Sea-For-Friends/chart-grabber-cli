// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-033, FEAT-GEO-034 (held open). Ports Test/u_MarkCategoryListToTree_Test.pas
// (TestSimpleList, TestTree, verbatim: same names, same expected shape and order) and
// u_MarkSystemHelpers.pas:303-441. The cases after the two DUnit ports pin the path rules the legacy suite
// does not reach.
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_list.h"
#include "cg/marks/mark_category_tree.h"
#include "cg/marks/mark_system_helpers.h"

#include <QString>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace cg::marks;

namespace {

MarkCategoryPtr cat(const QString& name) {
    return std::make_shared<const MarkCategory>(name, true, 0, 24);
}

MarkCategoryTreePtr treeOf(const std::vector<MarkCategoryPtr>& list) {
    return categoryListToCategoryTree(MarkCategoryList::build(list));
}

TEST(CategoryListToTree, TestSimpleList) {
    const std::vector<MarkCategoryPtr> list{cat(QStringLiteral("T1")), cat(QStringLiteral("T2")),
                                            cat(QStringLiteral("T3"))};
    const MarkCategoryTreePtr tree = treeOf(list);
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->name(), QString());
    EXPECT_EQ(tree->subItemCount(), 3);
    EXPECT_EQ(tree->markCategory(), nullptr);

    EXPECT_EQ(tree->subItem(0)->markCategory(), list[0]);
    EXPECT_EQ(tree->subItem(1)->markCategory(), list[1]);
    EXPECT_EQ(tree->subItem(2)->markCategory(), list[2]);

    EXPECT_EQ(tree->subItem(0)->name(), list[0]->name());
    EXPECT_EQ(tree->subItem(1)->name(), list[1]->name());
    EXPECT_EQ(tree->subItem(2)->name(), list[2]->name());

    EXPECT_EQ(tree->subItem(0)->subItemCount(), 0);
    EXPECT_EQ(tree->subItem(1)->subItemCount(), 0);
    EXPECT_EQ(tree->subItem(2)->subItemCount(), 0);
}

TEST(CategoryListToTree, TestTree) {
    const std::vector<MarkCategoryPtr> list{
        cat(QStringLiteral("T1\\TT1\\TTT1\\TTTT1\\TTTTT1")),
        cat(QStringLiteral("T2")),
        cat(QStringLiteral("T2\\TT1")),
        cat(QStringLiteral("T2\\TT1\\TTT1")),
        cat(QStringLiteral("T2\\TT1\\TTT2")),
        cat(QStringLiteral("T2\\TT2\\TTT1")),
        cat(QStringLiteral("T3\\TT1")),
        cat(QStringLiteral("T3\\TT1\\TTT1")),
    };
    const MarkCategoryTreePtr tree = treeOf(list);
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->name(), QString());
    EXPECT_EQ(tree->markCategory(), nullptr);
    ASSERT_EQ(tree->subItemCount(), 3);

    EXPECT_EQ(tree->subItem(0)->markCategory(), nullptr);
    EXPECT_EQ(tree->subItem(0)->name(), QStringLiteral("T1"));
    ASSERT_EQ(tree->subItem(0)->subItemCount(), 1);

    EXPECT_EQ(tree->subItem(0)->subItem(0)->markCategory(), nullptr);
    EXPECT_EQ(tree->subItem(0)->subItem(0)->name(), QStringLiteral("TT1"));
    ASSERT_EQ(tree->subItem(0)->subItem(0)->subItemCount(), 1);

    EXPECT_EQ(tree->subItem(0)->subItem(0)->subItem(0)->markCategory(), nullptr);
    EXPECT_EQ(tree->subItem(0)->subItem(0)->subItem(0)->name(), QStringLiteral("TTT1"));
    ASSERT_EQ(tree->subItem(0)->subItem(0)->subItem(0)->subItemCount(), 1);

    EXPECT_EQ(tree->subItem(0)->subItem(0)->subItem(0)->subItem(0)->markCategory(), nullptr);
    EXPECT_EQ(tree->subItem(0)->subItem(0)->subItem(0)->subItem(0)->name(), QStringLiteral("TTTT1"));
    ASSERT_EQ(tree->subItem(0)->subItem(0)->subItem(0)->subItem(0)->subItemCount(), 1);

    EXPECT_EQ(tree->subItem(0)->subItem(0)->subItem(0)->subItem(0)->subItem(0)->name(),
              QStringLiteral("TTTTT1"));
    EXPECT_EQ(tree->subItem(0)->subItem(0)->subItem(0)->subItem(0)->subItem(0)->markCategory(), list[0]);
    EXPECT_EQ(tree->subItem(0)->subItem(0)->subItem(0)->subItem(0)->subItem(0)->subItemCount(), 0);

    EXPECT_EQ(tree->subItem(1)->markCategory(), list[1]);
    EXPECT_EQ(tree->subItem(1)->name(), QStringLiteral("T2"));
    ASSERT_EQ(tree->subItem(1)->subItemCount(), 2);

    EXPECT_EQ(tree->subItem(1)->subItem(0)->markCategory(), list[2]);
    EXPECT_EQ(tree->subItem(1)->subItem(0)->name(), QStringLiteral("TT1"));
    ASSERT_EQ(tree->subItem(1)->subItem(0)->subItemCount(), 2);

    EXPECT_EQ(tree->subItem(1)->subItem(0)->subItem(0)->markCategory(), list[3]);
    EXPECT_EQ(tree->subItem(1)->subItem(0)->subItem(0)->name(), QStringLiteral("TTT1"));
    EXPECT_EQ(tree->subItem(1)->subItem(0)->subItem(0)->subItemCount(), 0);

    EXPECT_EQ(tree->subItem(1)->subItem(0)->subItem(1)->markCategory(), list[4]);
    EXPECT_EQ(tree->subItem(1)->subItem(0)->subItem(1)->name(), QStringLiteral("TTT2"));
    EXPECT_EQ(tree->subItem(1)->subItem(0)->subItem(1)->subItemCount(), 0);

    EXPECT_EQ(tree->subItem(1)->subItem(1)->markCategory(), nullptr);
    EXPECT_EQ(tree->subItem(1)->subItem(1)->name(), QStringLiteral("TT2"));
    ASSERT_EQ(tree->subItem(1)->subItem(1)->subItemCount(), 1);

    EXPECT_EQ(tree->subItem(1)->subItem(1)->subItem(0)->markCategory(), list[5]);
    EXPECT_EQ(tree->subItem(1)->subItem(1)->subItem(0)->name(), QStringLiteral("TTT1"));
    EXPECT_EQ(tree->subItem(1)->subItem(1)->subItem(0)->subItemCount(), 0);

    EXPECT_EQ(tree->subItem(2)->markCategory(), nullptr);
    EXPECT_EQ(tree->subItem(2)->name(), QStringLiteral("T3"));
    ASSERT_EQ(tree->subItem(2)->subItemCount(), 1);

    EXPECT_EQ(tree->subItem(2)->subItem(0)->markCategory(), list[6]);
    EXPECT_EQ(tree->subItem(2)->subItem(0)->name(), QStringLiteral("TT1"));
    ASSERT_EQ(tree->subItem(2)->subItem(0)->subItemCount(), 1);

    EXPECT_EQ(tree->subItem(2)->subItem(0)->subItem(0)->markCategory(), list[7]);
    EXPECT_EQ(tree->subItem(2)->subItem(0)->subItem(0)->name(), QStringLiteral("TTT1"));
    EXPECT_EQ(tree->subItem(2)->subItem(0)->subItem(0)->subItemCount(), 0);
}

// ---- rules the DUnit suite does not reach (u_MarkSystemHelpers.pas:303-441) ----

TEST(CategoryListToTree, EmptyOrNullListGivesNull) { // L-159: legacy raises EStringListError on Strings[0]
    EXPECT_EQ(categoryListToCategoryTree(nullptr), nullptr);
    EXPECT_EQ(
        categoryListToCategoryTree(std::make_shared<const MarkCategoryList>(std::vector<MarkCategoryPtr>{})),
        nullptr);
}

TEST(CategoryListToTree, NilElementRaisesAsLegacyAccessViolation) { // u_MarkSystemHelpers.pas:422-423
    const auto named = std::make_shared<const MarkCategory>(QStringLiteral("A"), true, 3, 23);
    EXPECT_THROW(static_cast<void>(categoryListToCategoryTree(MarkCategoryList::build({named, nullptr}))),
                 std::runtime_error);
}

TEST(CategoryListToTree, SingleEntryWithSeparatorIsAChainOfFolders) {
    const auto c = cat(QStringLiteral("A\\B"));
    const auto tree = treeOf({c});
    ASSERT_EQ(tree->subItemCount(), 1);
    EXPECT_EQ(tree->subItem(0)->name(), QStringLiteral("A"));
    EXPECT_EQ(tree->subItem(0)->markCategory(), nullptr);
    ASSERT_EQ(tree->subItem(0)->subItemCount(), 1);
    EXPECT_EQ(tree->subItem(0)->subItem(0)->name(), QStringLiteral("B"));
    EXPECT_EQ(tree->subItem(0)->subItem(0)->markCategory(), c);
}

TEST(CategoryListToTree, TrailingSeparatorGivesALeafWithEmptyName) {
    const auto c = cat(QStringLiteral("A\\"));
    const auto tree = treeOf({c});
    ASSERT_EQ(tree->subItemCount(), 1);
    EXPECT_EQ(tree->subItem(0)->name(), QStringLiteral("A"));
    EXPECT_EQ(tree->subItem(0)->markCategory(), nullptr);
    ASSERT_EQ(tree->subItem(0)->subItemCount(), 1);
    EXPECT_EQ(tree->subItem(0)->subItem(0)->name(), QString());
    EXPECT_EQ(tree->subItem(0)->subItem(0)->markCategory(), c);
}

TEST(CategoryListToTree, LeadingSeparatorGivesAFolderWithEmptyName) {
    const auto c = cat(QStringLiteral("\\A"));
    const auto tree = treeOf({c});
    ASSERT_EQ(tree->subItemCount(), 1);
    EXPECT_EQ(tree->subItem(0)->name(), QString());
    EXPECT_EQ(tree->subItem(0)->markCategory(), nullptr);
    ASSERT_EQ(tree->subItem(0)->subItemCount(), 1);
    EXPECT_EQ(tree->subItem(0)->subItem(0)->name(), QStringLiteral("A"));
    EXPECT_EQ(tree->subItem(0)->subItem(0)->markCategory(), c);
}

TEST(CategoryListToTree, MissingIntermediateNodeIsAFolderWithoutCategory) {
    const auto a = cat(QStringLiteral("A"));
    const auto deep = cat(QStringLiteral("A\\B\\C"));
    const auto tree = treeOf({deep, a}); // input order is irrelevant: the names are sorted
    ASSERT_EQ(tree->subItemCount(), 1);
    const auto nodeA = tree->subItem(0);
    EXPECT_EQ(nodeA->markCategory(), a);
    ASSERT_EQ(nodeA->subItemCount(), 1);
    EXPECT_EQ(nodeA->subItem(0)->name(), QStringLiteral("B"));
    EXPECT_EQ(nodeA->subItem(0)->markCategory(), nullptr);
    ASSERT_EQ(nodeA->subItem(0)->subItemCount(), 1);
    EXPECT_EQ(nodeA->subItem(0)->subItem(0)->markCategory(), deep);
}

TEST(CategoryListToTree, SamePathTwiceGivesTwoSiblingNodesInInputOrder) {
    const auto first = cat(QStringLiteral("A"));
    const auto second = std::make_shared<const MarkCategory>(QStringLiteral("A"), false, 1, 2);
    const auto tree = treeOf({first, second});
    ASSERT_EQ(tree->subItemCount(), 2);
    EXPECT_EQ(tree->subItem(0)->name(), QStringLiteral("A"));
    EXPECT_EQ(tree->subItem(0)->markCategory(), first);
    EXPECT_EQ(tree->subItem(1)->name(), QStringLiteral("A"));
    EXPECT_EQ(tree->subItem(1)->markCategory(), second);
}

TEST(CategoryListToTree, RussianNamesWithSpacesKeepTheirSpaces) {
    const auto river = cat(QStringLiteral("Берег"));
    const auto roads = cat(QStringLiteral("Мои метки"));
    const auto house = cat(QStringLiteral("Мои метки\\Дом"));
    const auto ctrl = cat(QStringLiteral("Ящик") + QChar(1) + QStringLiteral("2")); // L-160
    const auto tree = treeOf({house, roads, river, ctrl});
    ASSERT_EQ(tree->subItemCount(), 3);
    EXPECT_EQ(tree->subItem(2)->name(), QStringLiteral("Ящик 2")) << "U+0001 comes back as a space";
    EXPECT_EQ(tree->subItem(0)->name(), QStringLiteral("Берег"));
    EXPECT_EQ(tree->subItem(0)->markCategory(), river);
    EXPECT_EQ(tree->subItem(1)->name(), QStringLiteral("Мои метки"));
    EXPECT_EQ(tree->subItem(1)->markCategory(), roads);
    ASSERT_EQ(tree->subItem(1)->subItemCount(), 1);
    EXPECT_EQ(tree->subItem(1)->subItem(0)->name(), QStringLiteral("Дом"));
    EXPECT_EQ(tree->subItem(1)->subItem(0)->markCategory(), house);
}

// The pin for the sort: scrambled input; the same result under any process locale (LC_ALL=C included).
TEST(CategoryListToTree, SortOrder) {
    const auto trips2024 = cat(QStringLiteral("Trips 2024"));
    const auto lower = cat(QStringLiteral("b"));
    const auto alps = cat(QStringLiteral("Trips\\Alps"));
    const auto ya = cat(QStringLiteral("Я"));
    const auto upper = cat(QStringLiteral("B"));
    const auto trips = cat(QStringLiteral("Trips"));
    const auto a = cat(QStringLiteral("a"));
    const auto bang = cat(QStringLiteral("Trips!"));
    const auto tree = treeOf({trips2024, lower, alps, ya, upper, trips, a, bang});
    ASSERT_EQ(tree->subItemCount(), 7);
    const std::vector<std::pair<QString, MarkCategoryPtr>> expected{
        {QStringLiteral("a"), a},         {QStringLiteral("b"), lower},
        {QStringLiteral("B"), upper},     {QStringLiteral("Trips"), trips},
        {QStringLiteral("Trips!"), bang}, {QStringLiteral("Trips 2024"), trips2024},
        {QStringLiteral("Я"), ya}};
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(tree->subItem(i)->name(), expected[static_cast<std::size_t>(i)].first) << i;
        EXPECT_EQ(tree->subItem(i)->markCategory(), expected[static_cast<std::size_t>(i)].second) << i;
    }
    ASSERT_EQ(tree->subItem(3)->subItemCount(), 1); // one Trips node, carrying its category and Alps
    EXPECT_EQ(tree->subItem(3)->subItem(0)->name(), QStringLiteral("Alps"));
    EXPECT_EQ(tree->subItem(3)->subItem(0)->markCategory(), alps);
}

} // namespace
