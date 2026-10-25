// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkCategoryDbInternalORM.pas: isSame / isEqual (docs/specs/marks.md §11.4, proof row T-XXXX-O4).
// The isSame row-id-only rule is the reproduced legacy behaviour (L-193 in docs/parity/legacy-defects.md).
#include "cg/marks/mark_category.h"
#include "cg/marks/mormot/categories/mark_category_db_internal_orm.h"
#include "cg/vector/category.h"

#include <QString>

#include <gtest/gtest.h>

#include <memory>

using cg::marks::MarkCategory;
using cg::marks::MarkCategoryDbInternalORM;
using cg::vector::Category;

namespace {

constexpr quintptr kDbA = 1001;
constexpr quintptr kDbB = 2002;

std::shared_ptr<const MarkCategoryDbInternalORM> orm(qint64 id, quintptr dbId, const QString& name,
                                                     bool visible = true, int after = 3, int before = 23) {
    return std::make_shared<const MarkCategoryDbInternalORM>(id, dbId, name, visible, after, before);
}

TEST(MarkCategoryDbInternalORM, IsSameWithNullIsFalse) {
    const auto a = orm(5, kDbA, QStringLiteral("A"));
    EXPECT_FALSE(a->isSame(nullptr));
}

TEST(MarkCategoryDbInternalORM, IsSameComparesTheRowIdAcrossObjects) {
    const auto a = orm(5, kDbA, QStringLiteral("A"));
    EXPECT_TRUE(a->isSame(orm(5, kDbA, QStringLiteral("other name"), false, 7, 9).get()))
        << "same id: the other fields are not compared";
    EXPECT_FALSE(a->isSame(orm(6, kDbA, QStringLiteral("A")).get())) << "different id";
}

TEST(MarkCategoryDbInternalORM, IsSameDoesNotCompareDbId) {
    const auto a = orm(5, kDbA, QStringLiteral("A"));
    EXPECT_TRUE(a->isSame(orm(5, kDbB, QStringLiteral("A")).get()))
        << "legacy IsSame (:158-168) compares FId only";
}

TEST(MarkCategoryDbInternalORM, IsSameWithAPlainCategoryOfAnotherClassIsFalse) {
    const auto a = orm(5, kDbA, QStringLiteral("A"));
    const Category plain(QStringLiteral("A"));
    const MarkCategory marks(QStringLiteral("A"), true, 3, 23);
    EXPECT_FALSE(a->isSame(&plain)) << "not an IMarkCategoryInternalORM";
    EXPECT_FALSE(a->isSame(&marks)) << "not an IMarkCategoryInternalORM";
}

TEST(MarkCategoryDbInternalORM, IsEqualWithNullIsFalse) {
    const auto a = orm(5, kDbA, QStringLiteral("A"));
    EXPECT_FALSE(a->isEqual(nullptr));
}

TEST(MarkCategoryDbInternalORM, IsEqualIgnoresRowIdAndDbId) {
    const auto a = orm(5, kDbA, QStringLiteral("A"));
    EXPECT_TRUE(a->isEqual(orm(99, kDbB, QStringLiteral("A")).get()));
}

TEST(MarkCategoryDbInternalORM, IsEqualComparesTheNameExactly) {
    const auto a = orm(5, kDbA, QStringLiteral("Река"));
    EXPECT_FALSE(a->isEqual(orm(5, kDbA, QStringLiteral("река")).get())) << "case-sensitive";
    EXPECT_FALSE(a->isEqual(orm(5, kDbA, QStringLiteral("Река ")).get())) << "name differs";
}

TEST(MarkCategoryDbInternalORM, IsEqualComparesVisibleAndBothZooms) {
    const auto a = orm(5, kDbA, QStringLiteral("A"));
    EXPECT_FALSE(a->isEqual(orm(5, kDbA, QStringLiteral("A"), false).get())) << "visible";
    EXPECT_FALSE(a->isEqual(orm(5, kDbA, QStringLiteral("A"), true, 4).get())) << "afterScale";
    EXPECT_FALSE(a->isEqual(orm(5, kDbA, QStringLiteral("A"), true, 3, 22).get())) << "beforeScale";
}

TEST(MarkCategoryDbInternalORM, IsEqualToPlainMarkCategoryWithSameFieldsIsTrue) {
    const auto a = orm(5, kDbA, QStringLiteral("A"), false, 4, 22);
    const MarkCategory plain(QStringLiteral("A"), false, 4, 22);
    EXPECT_TRUE(a->isEqual(&plain)) << "Supports(ACategory, IMarkCategory) succeeds";
}

TEST(MarkCategoryDbInternalORM, IsEqualToPlainCategoryOfTheSameNameIsFalse) {
    const auto a = orm(5, kDbA, QStringLiteral("A"));
    const Category plain(QStringLiteral("A"));
    EXPECT_FALSE(a->isEqual(&plain)) << "Supports(ACategory, IMarkCategory) fails";
}

} // namespace
