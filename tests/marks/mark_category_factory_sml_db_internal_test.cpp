// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkCategoryFactorySmlDbInternal.pas: CreateCategory (:60-76) stamps the factory's dbId on the new
// category and passes the other fields through in the order of TMarkCategorySmlDbInternal.Create (:70-90).
#include "cg/marks/sml/mark_category_factory_sml_db_internal.h"
#include "cg/marks/sml/mark_db_sml_internal.h"

#include <QString>

#include <gtest/gtest.h>

#include <vector>

namespace m = cg::marks;

namespace {

constexpr quintptr kDbA = 4242;
constexpr quintptr kDbB = 5151;

TEST(MarkCategoryFactorySmlDbInternal, CreatesCategoryWithTheFactoryDbIdAndTheGivenFields) {
    struct Row {
        const char* what;
        quintptr dbId;
        int id;
        QString name;
        bool visible;
        int afterScale;
        int beforeScale;
    };
    const std::vector<Row> rows{
        {"db A, visible", kDbA, 7, QStringLiteral("Река"), true, 5, 17},
        {"db B, hidden, zero zooms", kDbB, 0, QStringLiteral("город"), false, 0, 0},
    };
    for (const auto& row : rows) {
        const m::MarkCategoryFactorySmlDbInternal factory(row.dbId);
        const auto category =
            factory.createCategory(row.id, row.name, row.visible, row.afterScale, row.beforeScale);
        const auto* internal = dynamic_cast<const m::IMarkCategorySMLInternal*>(category.get());
        ASSERT_NE(internal, nullptr) << row.what;
        EXPECT_EQ(internal->id(), row.id) << row.what;
        EXPECT_EQ(internal->dbId(), row.dbId) << row.what;
        EXPECT_EQ(category->name(), row.name) << row.what;
        EXPECT_EQ(category->visible(), row.visible) << row.what;
        EXPECT_EQ(category->afterScale(), row.afterScale) << row.what;
        EXPECT_EQ(category->beforeScale(), row.beforeScale) << row.what;
    }
}

} // namespace
