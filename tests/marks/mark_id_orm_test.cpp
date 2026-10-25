// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkIdORM.pas (TMarkIdORM): category id derivation (:130-134), IsSameId (:246-256),
// IsSameMark (:258-270), IsEqual (:272-315), IsEqualInternal (:221-244, no caller in legacy Src), SetVisible
// (:317-320).
#include "cg/marks/mark_full_base.h"
#include "cg/marks/mormot/categories/mark_category_db_internal_orm.h"
#include "cg/marks/mormot/marks/mark_id_orm.h"
#include "cg/vector/category.h"
#include "cg/vector/vector_data_item_base.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include "mark_test_util.h"

namespace m = cg::marks;
namespace v = cg::vector;
namespace t = cg::marks::testutil;

namespace {

// Exposes the protected IsEqualInternal of the legacy class.
class ProbeMarkIdORM : public m::MarkIdORM {
public:
    using m::MarkIdORM::isEqualInternal;
    using m::MarkIdORM::MarkIdORM;
};

// An IMarkId that is not an IMarkInternalORM: the Supports failure of u_MarkIdORM.pas:252.
class PlainMarkId final : public m::IMarkId {
public:
    [[nodiscard]] v::CategoryPtr category() const override { return nullptr; }
    [[nodiscard]] QString name() const override { return {}; }
    [[nodiscard]] int multiGeometryCount() const override { return 0; }
    [[nodiscard]] m::MarkIdType markType() const override { return m::MarkIdType::Point; }
    [[nodiscard]] bool isSameId(const m::IMarkId*) const override { return false; }
    [[nodiscard]] bool isSameMark(const v::IVectorDataItem*) const override { return false; }
};

std::shared_ptr<const m::MarkIdORM> markId(qint64 id, quintptr dbId = 1, v::CategoryPtr category = nullptr,
                                           QString name = QStringLiteral("n"), bool visible = true,
                                           cg::core::HashValue hash = 5) {
    return std::make_shared<const m::MarkIdORM>(t::converter(), hash, m::MarkIdType::Point, std::move(name),
                                                QStringLiteral("d"), id, dbId, 1, std::move(category),
                                                visible);
}

TEST(MarkIdORM, CategoryIdComesFromAnOrmCategoryOnly) {
    struct Row {
        const char* what;
        v::CategoryPtr category;
        qint64 expected;
    };
    const std::vector<Row> rows{
        {"orm category", t::ormCategory(7), 7},
        {"plain category of another class", std::make_shared<const v::Category>(QStringLiteral("c")), 0},
        {"no category", nullptr, 0},
    };
    for (const auto& row : rows) {
        EXPECT_EQ(markId(9, 1, row.category)->categoryId(), row.expected) << row.what;
    }
}

TEST(MarkIdORM, IsSameIdComparesIdAndDbId) {
    struct Row {
        const char* what;
        std::shared_ptr<const m::IMarkId> other;
        bool expected;
    };
    const auto base = markId(10, 1);
    const std::vector<Row> rows{
        {"same id and db", markId(10, 1), true},
        {"other id", markId(11, 1), false},
        {"other db", markId(10, 2), false},
        {"not an ORM mark id", std::make_shared<const PlainMarkId>(), false},
        {"null", nullptr, false},
    };
    for (const auto& row : rows) {
        EXPECT_EQ(base->isSameId(row.other.get()), row.expected) << row.what;
    }
}

TEST(MarkIdORM, IsSameMarkReadsTheIdentityOfTheMainInfo) {
    struct Row {
        const char* what;
        v::VectorDataItemPtr mark;
        bool expected;
    };
    const auto base = markId(10, 1);
    const auto plainInfo = std::make_shared<const v::VectorDataItemMainInfo>(
        5, t::converter(), QStringLiteral("n"), QStringLiteral("d"));
    const std::vector<Row> rows{
        {"main info of the same id and db", t::markWithInfo(markId(10, 1)), true},
        {"main info of another id", t::markWithInfo(markId(11, 1)), false},
        {"main info of another db", t::markWithInfo(markId(10, 2)), false},
        {"plain main info", t::markWithInfo(plainInfo), false},
        {"null mark", nullptr, false},
    };
    for (const auto& row : rows) {
        EXPECT_EQ(base->isSameMark(row.mark.get()), row.expected) << row.what;
    }
}

TEST(MarkIdORM, IsEqualRules) {
    struct Row {
        const char* what;
        v::CategoryPtr thisCategory;
        v::VectorDataItemMainInfoPtr other;
        bool expected;
        cg::core::HashValue thisHash = 5;
    };
    const auto plain = [](cg::core::HashValue hash, QString name, QString desc) {
        return std::make_shared<const v::VectorDataItemMainInfo>(hash, t::converter(), std::move(name),
                                                                 std::move(desc));
    };
    const auto withCategory = [](v::CategoryPtr category) {
        return std::make_shared<const m::MarkMainInfo>(5, t::converter(), QStringLiteral("n"),
                                                       std::move(category), QStringLiteral("d"));
    };
    const std::vector<Row> rows{
        {"same values, no categories", nullptr, plain(5, "n", "d"), true},
        {"both hashes non-zero and different", nullptr, plain(6, "n", "d"), false},
        {"other hash zero is ignored", nullptr, plain(0, "n", "d"), true},
        {"this hash zero is ignored", nullptr, plain(6, "n", "d"), true, 0},
        {"name differs", nullptr, plain(5, "x", "d"), false},
        {"desc differs", nullptr, plain(5, "n", "x"), false},
        {"plain item, this has a category", t::ormCategory(7), plain(5, "n", "d"), false},
        {"mark info without category, this has none", nullptr, withCategory(nullptr), true},
        {"mark info with the same category id", t::ormCategory(7), withCategory(t::ormCategory(7)), true},
        {"mark info with another category id", t::ormCategory(7), withCategory(t::ormCategory(8)), false},
        {"mark info with category, this has none", nullptr, withCategory(t::ormCategory(7)), false},
        {"mark info without category, this has one", t::ormCategory(7), withCategory(nullptr), false},
    };
    for (const auto& row : rows) {
        const auto base = markId(10, 1, row.thisCategory, QStringLiteral("n"), true, row.thisHash);
        EXPECT_EQ(base->isEqual(row.other.get()), row.expected) << row.what;
    }
    EXPECT_FALSE(markId(10)->isEqual(nullptr));
}

TEST(MarkIdORM, IsEqualInternalComparesDbIdCategoryIdIdVisibleAndName) {
    struct Row {
        const char* what;
        quintptr dbId;
        qint64 id;
        v::CategoryPtr category;
        bool visible;
        QString name;
        bool expected;
    };
    const ProbeMarkIdORM base(t::converter(), 5, m::MarkIdType::Point, QStringLiteral("n"),
                              QStringLiteral("d"), 10, 1, 1, nullptr, true);
    const std::vector<Row> rows{
        {"all equal", 1, 10, nullptr, true, QStringLiteral("n"), true},
        {"db differs", 2, 10, nullptr, true, QStringLiteral("n"), false},
        {"id differs", 1, 11, nullptr, true, QStringLiteral("n"), false},
        {"category id differs", 1, 10, t::ormCategory(3), true, QStringLiteral("n"), false},
        {"visible differs", 1, 10, nullptr, false, QStringLiteral("n"), false},
        {"name differs", 1, 10, nullptr, true, QStringLiteral("x"), false},
    };
    for (const auto& row : rows) {
        const auto other = markId(row.id, row.dbId, row.category, row.name, row.visible);
        EXPECT_EQ(base.isEqualInternal(*other), row.expected) << row.what;
    }
}

TEST(MarkIdORM, SetVisibleChangesTheSharedObject) {
    const auto mark = markId(10);
    ASSERT_TRUE(mark->visible());
    mark->setVisible(false);
    EXPECT_FALSE(mark->visible());
}

} // namespace
