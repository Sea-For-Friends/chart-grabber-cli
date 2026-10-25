// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkId.pas (TMarkId): the category id derivation (:122-127), IsSameId (:239-249), IsSameMark
// (:251-263), IsEqual (:265-308), IsEqualInternal (:214-237, no caller in legacy Src), GetInfoHTML
// (:171-179), GetInfoUrl
// (:181-187), SetVisible (:310-313). Expected values are literals from the Pascal; a row names its branch.
#include "cg/core/hash.h"
#include "cg/marks/c_mark_system.h"
#include "cg/marks/mark_full_base.h"
#include "cg/marks/mormot/marks/mark_id_orm.h"
#include "cg/marks/sml/mark_id.h"
#include "cg/vector/category.h"
#include "cg/vector/vector_data_item_base.h"

#include <QString>

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
class ProbeMarkId : public m::MarkId {
public:
    using m::MarkId::isEqualInternal;
    using m::MarkId::MarkId;
};

// A category that is never the same as another object (its isSame is False), to observe the identity shortcut
// of IsEqual (:273-276): without it the category test at :289-294 would answer False for the mark itself.
class NeverSameCategory final : public v::ICategory {
public:
    [[nodiscard]] QString name() const override { return QStringLiteral("n"); }
    [[nodiscard]] bool isSame(const v::ICategory*) const override { return false; }
    [[nodiscard]] bool isEqual(const v::ICategory*) const override { return false; }
};

std::shared_ptr<const m::MarkId> markId(int id, quintptr dbId = 1, v::CategoryPtr category = nullptr,
                                        QString name = QStringLiteral("n"), bool visible = true,
                                        cg::core::HashValue hash = 5) {
    return std::make_shared<const m::MarkId>(t::converter(), hash, m::MarkIdType::Point, std::move(name),
                                             QStringLiteral("d"), id, dbId, 1, std::move(category), visible);
}

// A mark of the SQLite backend (another class that implements IVectorDataItemMainInfo and IMarkId).
std::shared_ptr<const m::MarkIdORM> ormMarkId(qint64 id, quintptr dbId, v::CategoryPtr category = nullptr) {
    return std::make_shared<const m::MarkIdORM>(t::converter(), 5, m::MarkIdType::Point, QStringLiteral("n"),
                                                QStringLiteral("d"), id, dbId, 1, std::move(category), true);
}

v::VectorDataItemMainInfoPtr plainInfo(cg::core::HashValue hash, QString name, QString desc) {
    return std::make_shared<const v::VectorDataItemMainInfo>(hash, t::converter(), std::move(name),
                                                             std::move(desc));
}

TEST(MarkIdSml, CategoryIdIsTheSmlCategoryIdOrMinusOne) {
    struct Row {
        const char* what;
        v::CategoryPtr category;
        int expected;
    };
    const std::vector<Row> rows{
        {"sml category id 7", t::smlCategory(7), 7},
        {"sml category id 0 (not a special value)", t::smlCategory(0), 0},
        {"sqlite category of the same id (another class)", t::ormCategory(7), -1},
        {"plain category of another class", std::make_shared<const v::Category>(QStringLiteral("c")), -1},
        {"no category", nullptr, -1},
    };
    for (const auto& row : rows) {
        EXPECT_EQ(markId(9, 1, row.category)->categoryId(), row.expected) << row.what;
    }
}

TEST(MarkIdSml, IsSameIdComparesIdAndDbId) {
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
        {"sqlite mark id with the same id and db (another class)", ormMarkId(10, 1), false},
        {"null", nullptr, false},
    };
    for (const auto& row : rows) {
        EXPECT_EQ(base->isSameId(row.other.get()), row.expected) << row.what;
    }
}

TEST(MarkIdSml, IsSameMarkReadsTheIdentityOfTheMainInfo) {
    struct Row {
        const char* what;
        v::VectorDataItemPtr mark;
        bool expected;
    };
    const auto base = markId(10, 1);
    const std::vector<Row> rows{
        {"main info of the same id and db", t::markWithInfo(markId(10, 1)), true},
        {"main info of another id", t::markWithInfo(markId(11, 1)), false},
        {"main info of another db", t::markWithInfo(markId(10, 2)), false},
        {"plain main info", t::markWithInfo(plainInfo(5, QStringLiteral("n"), QStringLiteral("d"))), false},
        {"sqlite main info with the same id and db (another class)", t::markWithInfo(ormMarkId(10, 1)),
         false},
        {"null mark", nullptr, false},
    };
    for (const auto& row : rows) {
        EXPECT_EQ(base->isSameMark(row.mark.get()), row.expected) << row.what;
    }
}

TEST(MarkIdSml, IsEqualTable) {
    struct Row {
        const char* what;
        v::CategoryPtr thisCategory;
        v::VectorDataItemMainInfoPtr other;
        bool expected;
        cg::core::HashValue thisHash = 5;
    };
    const auto withCategory = [](v::CategoryPtr category, QString name = QStringLiteral("n")) {
        return std::make_shared<const m::MarkMainInfo>(5, t::converter(), std::move(name),
                                                       std::move(category), QStringLiteral("d"));
    };
    const std::vector<Row> rows{
        {"same values, no categories", nullptr, plainInfo(5, "n", "d"), true},
        {"both hashes non-zero and different", nullptr, plainInfo(6, "n", "d"), false},
        {"null argument", nullptr, nullptr, false},
        {"both hashes non-zero and equal", nullptr, plainInfo(5, "n", "d"), true},
        {"other hash zero is ignored", nullptr, plainInfo(0, "n", "d"), true},
        {"this hash zero is ignored", nullptr, plainInfo(6, "n", "d"), true, 0},
        {"name differs", nullptr, plainInfo(5, "x", "d"), false},
        {"desc differs", nullptr, plainInfo(5, "n", "x"), false},
        {"plain item, this has a category", t::smlCategory(7), plainInfo(5, "n", "d"), false},
        {"mark info without category, this has none", nullptr, withCategory(nullptr), true},
        {"mark info with the same category id", t::smlCategory(7), withCategory(t::smlCategory(7)), true},
        {"mark info with the same category id, other db (id only)", t::smlCategory(7, 1),
         withCategory(t::smlCategory(7, 2)), true},
        {"mark info with another category id", t::smlCategory(7), withCategory(t::smlCategory(8)), false},
        {"mark info with category, this has none", nullptr, withCategory(t::smlCategory(7)), false},
        {"mark info without category, this has one", t::smlCategory(7), withCategory(nullptr), false},
        {"mark info with a sqlite category of id 7 (another class)", t::smlCategory(7),
         withCategory(t::ormCategory(7)), false},
        {"sqlite mark id without category, this has none (another class)", nullptr, ormMarkId(10, 1), true},
        {"sqlite mark id without category, this has one (another class)", t::smlCategory(7), ormMarkId(10, 1),
         false},
    };
    for (const auto& row : rows) {
        const auto base = markId(10, 1, row.thisCategory, QStringLiteral("n"), true, row.thisHash);
        EXPECT_EQ(base->isEqual(row.other.get()), row.expected) << row.what;
    }
}

TEST(MarkIdSml, IsEqualIsTrueForTheMarkItselfEvenWhenItsCategoryIsNeverSame) {
    // Without the identity shortcut (:273-276) the category test (:289-294) calls NeverSameCategory::isSame,
    // which is False, so this test fails when the shortcut is removed.
    const auto mark = markId(10, 1, std::make_shared<const NeverSameCategory>());
    EXPECT_TRUE(mark->isEqual(mark.get()));
}

TEST(MarkIdSml, IsEqualInternalComparesDbIdCategoryIdIdVisibleAndName) {
    struct Row {
        const char* what;
        quintptr dbId;
        int id;
        v::CategoryPtr category;
        bool visible;
        QString name;
        bool expected;
    };
    const ProbeMarkId base(t::converter(), 5, m::MarkIdType::Point, QStringLiteral("n"), QStringLiteral("d"),
                           10, 1, 1, nullptr, true);
    const std::vector<Row> rows{
        {"all equal", 1, 10, nullptr, true, QStringLiteral("n"), true},
        {"db differs", 2, 10, nullptr, true, QStringLiteral("n"), false},
        {"id differs", 1, 11, nullptr, true, QStringLiteral("n"), false},
        {"category id differs", 1, 10, t::smlCategory(3), true, QStringLiteral("n"), false},
        {"sqlite category: its categoryId is -1, as for no category", 1, 10, t::ormCategory(3), true,
         QStringLiteral("n"), true},
        {"visible differs", 1, 10, nullptr, false, QStringLiteral("n"), false},
        {"name differs", 1, 10, nullptr, true, QStringLiteral("x"), false},
    };
    for (const auto& row : rows) {
        const auto other = markId(row.id, row.dbId, row.category, row.name, row.visible);
        EXPECT_EQ(base.isEqualInternal(*other), row.expected) << row.what;
    }
}

TEST(MarkIdSml, AccessorsReturnTheConstructorArguments) {
    const auto category = t::smlCategory(3, 4);
    const m::MarkId mark(t::converter(), 0x1234, m::MarkIdType::Poly, QStringLiteral("N"),
                         QStringLiteral("D"), 77, 99, 4, category, false);
    EXPECT_EQ(mark.hash(), 0x1234u);
    EXPECT_EQ(mark.markType(), m::MarkIdType::Poly);
    EXPECT_EQ(mark.name(), QStringLiteral("N"));
    EXPECT_EQ(mark.desc(), QStringLiteral("D"));
    EXPECT_EQ(mark.id(), 77);
    EXPECT_EQ(mark.dbId(), 99u);
    EXPECT_EQ(mark.multiGeometryCount(), 4);
    EXPECT_EQ(mark.category(), category);
    EXPECT_EQ(mark.categoryId(), 3);
    EXPECT_FALSE(mark.visible());
    EXPECT_EQ(mark.hintText(), QStringLiteral("N|D")) << "legacy GetHintText: Convert(FName, FDesc) (:158)";
    EXPECT_EQ(mark.infoCaption(), QStringLiteral("N")) << "legacy GetInfoCaption: FName (:168)";
}

TEST(MarkIdSml, InfoHtmlIsWrappedOnlyForANonEmptyDesc) {
    struct Row {
        const char* what;
        QString desc;
        QString expected;
    };
    const std::vector<Row> rows{
        {"empty desc gives an empty text", QString(), QString()},
        {"non-empty desc is wrapped", QStringLiteral("Город"),
         QStringLiteral("<HTML><BODY>Город</BODY></HTML>")},
    };
    for (const auto& row : rows) {
        const m::MarkId mark(t::converter(), 5, m::MarkIdType::Point, QStringLiteral("n"), row.desc, 10, 1, 1,
                             nullptr, true);
        EXPECT_EQ(mark.infoHtml(), row.expected) << row.what;
        EXPECT_EQ(mark.infoUrl(), QStringLiteral("sas://Placemarks/10/"))
            << "CMarksSystemInternalURL + IntToStr(id) + '/' (:181-187)";
    }
}

TEST(MarkIdSml, SetVisibleChangesTheSharedObject) {
    const auto mark = markId(10);
    ASSERT_TRUE(mark->visible());
    mark->setVisible(false);
    EXPECT_FALSE(mark->visible());
}

} // namespace
