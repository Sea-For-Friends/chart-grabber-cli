// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkCategorySmlDbInternal.pas: isSame (:148-158) and isEqual (:122-146). Each table row pins one
// branch of the Pascal; the expected values are literals from the Pascal, not from the class under test.
// The isSame rule compares the id only: the SML twin of the L-193 row of docs/parity/legacy-defects.md.
#include "cg/marks/mark_category.h"
#include "cg/marks/mormot/categories/mark_category_db_internal_orm.h"
#include "cg/marks/sml/mark_category_sml_db_internal.h"
#include "cg/vector/category.h"

#include <QString>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "mark_test_util.h"

namespace m = cg::marks;
namespace v = cg::vector;
namespace t = cg::marks::testutil;

namespace {

constexpr quintptr kDbA = 1001;
constexpr quintptr kDbB = 2002;

std::shared_ptr<const m::MarkCategorySmlDbInternal> sml(int id, quintptr dbId, const QString& name,
                                                        bool visible = true, int after = 3, int before = 23) {
    return std::make_shared<const m::MarkCategorySmlDbInternal>(id, dbId, name, visible, after, before);
}

TEST(MarkCategorySmlDbInternal, IsSameTable) {
    struct Row {
        const char* what;
        v::CategoryPtr other;
        bool expected;
    };
    const auto base = sml(5, kDbA, QStringLiteral("A"));
    const std::vector<Row> rows{
        {"same id and db", sml(5, kDbA, QStringLiteral("A")), true},
        {"same id, other db (L-193: id only)", sml(5, kDbB, QStringLiteral("A")), true},
        {"same id, other name, visible, zooms", sml(5, kDbA, QStringLiteral("other"), false, 7, 9), true},
        {"other id", sml(6, kDbA, QStringLiteral("A")), false},
        {"other id, same db", sml(4, kDbA, QStringLiteral("A")), false},
        {"ORM category with the same id (not an SML internal)", t::ormCategory(5, kDbA, QStringLiteral("A")),
         false},
        {"plain MarkCategory with the same name (not an SML internal)",
         std::make_shared<const m::MarkCategory>(QStringLiteral("A"), true, 3, 23), false},
        {"plain Category with the same name (not an SML internal)",
         std::make_shared<const v::Category>(QStringLiteral("A")), false},
        {"null", nullptr, false},
    };
    for (const auto& row : rows) {
        EXPECT_EQ(base->isSame(row.other.get()), row.expected) << row.what;
    }
}

TEST(MarkCategorySmlDbInternal, IsEqualTable) {
    struct Row {
        const char* what;
        v::CategoryPtr other;
        bool expected;
    };
    const auto base = sml(5, kDbA, QStringLiteral("Река"), false, 4, 22);
    const std::vector<Row> rows{
        {"same name and fields, other id and db", sml(99, kDbB, QStringLiteral("Река"), false, 4, 22), true},
        {"same name and fields, plain MarkCategory (IMarkCategory of another class)",
         std::make_shared<const m::MarkCategory>(QStringLiteral("Река"), false, 4, 22), true},
        {"same name and fields, ORM category (IMarkCategory of another class)",
         std::make_shared<const m::MarkCategoryDbInternalORM>(5, kDbA, QStringLiteral("Река"), false, 4, 22),
         true},
        {"name differs in case", sml(5, kDbA, QStringLiteral("река"), false, 4, 22), false},
        {"name differs by a trailing space", sml(5, kDbA, QStringLiteral("Река "), false, 4, 22), false},
        {"visible differs", sml(5, kDbA, QStringLiteral("Река"), true, 4, 22), false},
        {"afterScale differs", sml(5, kDbA, QStringLiteral("Река"), false, 5, 22), false},
        {"beforeScale differs", sml(5, kDbA, QStringLiteral("Река"), false, 4, 23), false},
        {"plain Category with the same name is not an IMarkCategory",
         std::make_shared<const v::Category>(QStringLiteral("Река")), false},
        {"null", nullptr, false},
    };
    for (const auto& row : rows) {
        EXPECT_EQ(base->isEqual(row.other.get()), row.expected) << row.what;
    }
}

} // namespace
