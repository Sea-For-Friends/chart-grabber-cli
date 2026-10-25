// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkCategoryFactoryDbInternalORM.pas: both CreateCategory overloads stamp the factory's dbId
// (docs/specs/marks.md §2.9.6). Only the object identity needed by the isSame table is checked here.
#include "cg/marks/mormot/categories/mark_category_db_internal_orm.h"
#include "cg/marks/mormot/categories/mark_category_factory_db_internal_orm.h"
#include "cg/marks/mormot/categories/mark_category_internal_orm.h"
#include "cg/marks/mormot/mark_system_orm_types.h"

#include <QString>

#include <gtest/gtest.h>

#include <memory>

using cg::marks::IMarkCategoryInternalORM;
using cg::marks::MarkCategoryFactoryDbInternalORM;
using cg::marks::OrmCategoryRec;

namespace {

constexpr quintptr kDb = 4242;

TEST(MarkCategoryFactoryDbInternalORM, BothOverloadsStoreIdDbIdAndFields) {
    const MarkCategoryFactoryDbInternalORM factory(kDb);

    OrmCategoryRec rec;
    rec.categoryId = 7;
    rec.name = QStringLiteral("Река");
    rec.viewId = 2;
    rec.visible = false;
    rec.minZoom = 5;
    rec.maxZoom = 17;

    const auto fromRec = factory.createCategory(rec);
    const auto fromFields = factory.createCategory(7, QStringLiteral("Река"), false, 5, 17);

    for (const auto& category : {fromRec, fromFields}) {
        const auto* internal = dynamic_cast<const IMarkCategoryInternalORM*>(category.get());
        ASSERT_NE(internal, nullptr);
        EXPECT_EQ(internal->id(), 7);
        EXPECT_EQ(internal->dbId(), kDb);
        EXPECT_EQ(category->name(), QStringLiteral("Река"));
        EXPECT_FALSE(category->visible());
        EXPECT_EQ(category->afterScale(), 5);
        EXPECT_EQ(category->beforeScale(), 17);
    }
}

} // namespace
