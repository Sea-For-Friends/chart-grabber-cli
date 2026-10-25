// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkFactorySmlDbInternal.pas (424): CalcMultiGeometryCount, CreatePoint / CreateLine / CreatePoly,
// CreateMark, CreateInternalMark (docs/specs/marks.md section 2.8.4). The main-info hashes are literals
// computed from the Pascal recipe (CityHash64 of the UTF-16LE bytes, the description chained on the name
// hash); the item hash chain is written with the legacy UpdateHashByHash helper. This ticket implements no
// feature (Features: — in its header).
#include "cg/core/hash.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mormot/marks/mark_id_orm.h"
#include "cg/marks/sml/mark_db_sml_internal.h"
#include "cg/marks/sml/mark_factory_sml_db_internal.h"
#include "cg/marks/sml/mark_factory_sml_internal.h"
#include "cg/marks/sml/mark_id.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/appearance_of_vector_item.h"
#include "cg/vector/vector_data_item_base.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "../vector/fake_mark_picture.h"
#include "mark_test_util.h"

namespace m = cg::marks;
namespace v = cg::vector;
namespace g = cg::geo;
namespace t = cg::marks::testutil;
using cg::core::HashValue;
using t::FakePictureList;

namespace {

constexpr quintptr kDbId = 0x51;      // the database the factory belongs to
constexpr quintptr kOtherDbId = 0x77; // another database

QString qs(const char* utf8) {
    return QString::fromUtf8(utf8);
}

// This database's SML categories: look-ups by integer id and by name, a count of the id look-ups (legacy
// GetCategoryByID runs once per CreateMark) and the names asked by the name look-up, in call order.
class FakeCategoryDb final : public m::IMarkCategoryDBSmlInternal {
public:
    std::vector<m::MarkCategoryPtr> categories; // in list order
    mutable int lookups = 0;                    // categoryById calls
    mutable std::vector<QString> namesAsked;    // firstCategoryByName arguments

    void initialize(std::stop_token) override {}
    bool isCategoryFromThisDb(const v::CategoryPtr& category) const override {
        for (const auto& c : categories) {
            if (c.get() == category.get()) {
                return true;
            }
        }
        return false;
    }
    m::MarkCategoryPtr categoryById(int id) const override {
        ++lookups;
        for (const auto& c : categories) {
            if (idOf(*c) == id) {
                return c;
            }
        }
        return nullptr;
    }
    m::MarkCategoryPtr firstCategoryByName(const QString& name) const override {
        namesAsked.push_back(name);
        for (const auto& c : categories) {
            if (c->name() == name) {
                return c;
            }
        }
        return nullptr;
    }
    int categoryByNameCount(const QString& name) const override {
        int count = 0;
        for (const auto& c : categories) {
            count += c->name() == name ? 1 : 0;
        }
        return count;
    }

private:
    static int idOf(const m::IMarkCategory& category) {
        const auto* internal = dynamic_cast<const m::IMarkCategorySMLInternal*>(&category);
        return internal != nullptr ? internal->id() : -1;
    }
};

struct Fixture {
    std::shared_ptr<FakePictureList> pictures = std::make_shared<FakePictureList>();
    std::shared_ptr<FakeCategoryDb> categories = std::make_shared<FakeCategoryDb>();
    v::AppearanceOfMarkFactoryPtr appearances = std::make_shared<v::AppearanceOfMarkFactory>();
    // replaceCategory reads neither the config nor the picture list, so both are null here.
    std::shared_ptr<const m::IMarkFactory> markFactory =
        std::make_shared<m::MarkFactory>(nullptr, nullptr, appearances, t::converter());
    m::MarkFactorySmlDbInternal factory{kDbId,       pictures,       appearances,
                                        markFactory, t::converter(), categories};
};

g::LonLatPointsPtr twoPoints() {
    return std::make_shared<const std::vector<g::LonLat>>(
        std::vector<g::LonLat>{g::LonLat{1.0, 2.0}, g::LonLat{3.0, 4.0}});
}

// Two parts: a multi line of two single lines.
g::GeometryLonLatPtr multiLineGeom() {
    auto first =
        std::make_shared<const g::GeometryLonLatSingleLine>(1, g::LonLatRect{}, twoPoints(), g::PointsMeta{});
    auto second =
        std::make_shared<const g::GeometryLonLatSingleLine>(2, g::LonLatRect{}, twoPoints(), g::PointsMeta{});
    return std::make_shared<const g::GeometryLonLatMultiLine>(
        3, g::LonLatRect{}, std::vector<g::GeometryLonLatSingleLinePtr>{first, second});
}

// Three parts: a multi polygon of three single polygons, each with an outer contour and no holes.
g::GeometryLonLatPtr multiPolyGeom() {
    std::vector<g::GeometryLonLatSinglePolygonPtr> parts;
    for (HashValue i = 1; i <= 3; ++i) {
        auto outer = std::make_shared<const g::GeometryLonLatContour>(i, g::LonLatRect{}, twoPoints());
        parts.push_back(std::make_shared<const g::GeometryLonLatSinglePolygon>(
            i, g::LonLatRect{}, outer, std::vector<g::GeometryLonLatContourPtr>{}));
    }
    return std::make_shared<const g::GeometryLonLatMultiPolygon>(9, g::LonLatRect{}, std::move(parts));
}

const v::IVectorDataItemWithCategory* withCategory(const v::VectorDataItemMainInfoPtr& info) {
    return dynamic_cast<const v::IVectorDataItemWithCategory*>(info.get());
}

// A database mark of database `dbId` with `category` and `appearance` (a point appearance, so that
// replaceCategory, which needs an appearance, can rebuild it).
v::VectorDataItemPtr smlMark(Fixture& f, quintptr dbId, v::CategoryPtr category) {
    auto info = std::make_shared<const m::MarkId>(t::converter(), 9, m::MarkIdType::Point, qs("Город"),
                                                  qs("город"), 3, dbId, 0, std::move(category), true);
    auto appearance = f.appearances->createPointAppearance(0x1U, 0x2U, 3, qs("p"), nullptr, 4);
    return std::make_shared<const v::VectorDataItem>(7, std::move(appearance), std::move(info), t::point());
}

TEST(MarkFactorySmlDbInternal, MainInfoHashIsTheLegacyRecipeLiteral) {
    Fixture f;
    f.categories->categories.push_back(t::smlCategory(7, kDbId, qs("Cat")));
    // main info: hashString(name), then UpdateHashByString(desc) (:200-201); both are skipped when empty
    // (u_HashFunctionByImpl.pas:215-218). Values from an independent CityHash64 port, checked against the
    // upstream golden vectors of tests/core/hash_test.cpp.
    struct Row {
        const char* what;
        QString name;
        QString desc;
        HashValue mainHash;
    };
    const Row rows[] = {
        {"non-ASCII name and description", qs("Город"), qs("город"), 0x52c0ab0f60403d2eULL},
        {"name only", qs("A"), qs(""), 0x3f317f46466233afULL},
        {"description only: the name hash is 0", qs(""), qs("x"), 0xe5544da9333112c9ULL},
        {"empty name and description: both hashes 0", qs(""), qs(""), 0ULL},
    };
    // the same recipe for the point (:200-218), the line (:284-302) and the polygon (:335-353)
    struct Geometry {
        const char* what;
        g::GeometryLonLatPtr geometry;
    };
    const Geometry geometries[] = {
        {"point", t::point()},
        {"line", t::lineGeometry()},
        {"polygon", t::polygonGeometry()},
    };
    for (const Row& row : rows) {
        for (const Geometry& kind : geometries) {
            SCOPED_TRACE(std::string(row.what) + " / " + kind.what);
            const auto item = f.factory.createMark(101, row.name, true, qs(""), 7, row.desc, kind.geometry,
                                                   0x11U, 0x22U, 3, 4);
            ASSERT_NE(item, nullptr);
            EXPECT_EQ(item->mainInfo()->hash(), row.mainHash);
            // item hash: geometry, then main info hash, then appearance hash (:216-218); the appearance hash
            // is read from the landed appearance object (vector module, not this recipe)
            ASSERT_NE(item->appearance(), nullptr);
            EXPECT_EQ(item->hash(), t::hashChainU64(t::hashChainU64(item->geometry()->hash(), row.mainHash),
                                                    item->appearance()->hash()));
        }
    }
}

TEST(MarkFactorySmlDbInternal, CreateMarkBuildsEachKindWithItsFieldsAndMultiGeometryCount) {
    Fixture f;
    f.categories->categories.push_back(t::smlCategory(7, kDbId, qs("Cat")));
    enum class Kind { Point, Line, Poly };
    struct Row {
        const char* what;
        g::GeometryLonLatPtr geometry;
        Kind kind;
        m::MarkIdType type;
        int multiGeometryCount;
        bool visible;
    };
    const Row rows[] = {
        {"point, visible", t::point(), Kind::Point, m::MarkIdType::Point, 0, true},
        {"point, hidden", t::point(), Kind::Point, m::MarkIdType::Point, 0, false},
        {"line, visible", t::lineGeometry(), Kind::Line, m::MarkIdType::Line, 0, true},
        {"line, hidden", t::lineGeometry(), Kind::Line, m::MarkIdType::Line, 0, false},
        {"polygon, visible", t::polygonGeometry(), Kind::Poly, m::MarkIdType::Poly, 0, true},
        {"polygon, hidden", t::polygonGeometry(), Kind::Poly, m::MarkIdType::Poly, 0, false},
        {"multi-line of two, visible", multiLineGeom(), Kind::Line, m::MarkIdType::Line, 2, true},
        {"multi-line of two, hidden", multiLineGeom(), Kind::Line, m::MarkIdType::Line, 2, false},
        {"multi-polygon of three, visible", multiPolyGeom(), Kind::Poly, m::MarkIdType::Poly, 3, true},
        {"multi-polygon of three, hidden", multiPolyGeom(), Kind::Poly, m::MarkIdType::Poly, 3, false},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.what);
        // colour1 = 0x11000001, colour2 = 0x22000002, scale1 = 3, scale2 = 4: each swapped argument shows up
        const auto item = f.factory.createMark(101, qs("Город"), row.visible, qs(""), 7, qs("город"),
                                               row.geometry, 0x11000001U, 0x22000002U, 3, 4);
        ASSERT_NE(item, nullptr);
        EXPECT_EQ(item->name(), qs("Город"));
        EXPECT_EQ(item->desc(), qs("город"));
        EXPECT_EQ(item->geometry(), row.geometry);
        EXPECT_EQ(item->hintText(), qs("Город|город"));

        const auto* id = dynamic_cast<const m::IMarkId*>(item->mainInfo().get());
        ASSERT_NE(id, nullptr);
        EXPECT_EQ(id->markType(), row.type);
        EXPECT_EQ(id->multiGeometryCount(), row.multiGeometryCount);
        const auto* internal = dynamic_cast<const m::IMarkSMLInternal*>(item->mainInfo().get());
        ASSERT_NE(internal, nullptr);
        EXPECT_EQ(internal->id(), 101);
        EXPECT_EQ(internal->dbId(), kDbId);
        EXPECT_EQ(internal->categoryId(), 7);
        EXPECT_EQ(internal->visible(), row.visible);
        ASSERT_NE(withCategory(item->mainInfo()), nullptr);
        EXPECT_EQ(withCategory(item->mainInfo())->category(), f.categories->categories[0]);

        switch (row.kind) {
        case Kind::Point: {
            const auto* caption = dynamic_cast<const v::IAppearancePointCaption*>(item->appearance().get());
            const auto* icon = dynamic_cast<const v::IAppearancePointIcon*>(item->appearance().get());
            ASSERT_NE(caption, nullptr);
            ASSERT_NE(icon, nullptr);
            EXPECT_EQ(caption->textColor(), 0x11000001U);   // colour1: text colour
            EXPECT_EQ(caption->textBgColor(), 0x22000002U); // colour2: text background
            EXPECT_EQ(caption->fontSize(), 3);              // scale1: font size
            EXPECT_EQ(icon->markerSize(), 4);               // scale2: marker size
            break;
        }
        case Kind::Line: {
            const auto* line = dynamic_cast<const v::IAppearanceLine*>(item->appearance().get());
            ASSERT_NE(line, nullptr);
            EXPECT_EQ(line->lineColor(),
                      0x11000001U);          // colour1: line colour; colour2 and scale2 are not used
            EXPECT_EQ(line->lineWidth(), 3); // scale1: line width
            break;
        }
        case Kind::Poly: {
            const auto* border = dynamic_cast<const v::IAppearancePolygonBorder*>(item->appearance().get());
            const auto* fill = dynamic_cast<const v::IAppearancePolygonFill*>(item->appearance().get());
            ASSERT_NE(border, nullptr);
            ASSERT_NE(fill, nullptr);
            EXPECT_EQ(border->lineColor(), 0x11000001U); // colour1: border colour
            EXPECT_EQ(border->lineWidth(), 3);           // scale1: border width
            EXPECT_EQ(fill->fillColor(), 0x22000002U);   // colour2: fill colour
            break;
        }
        }
    }
}

TEST(MarkFactorySmlDbInternal, CreatePointUsesTheFoundPictureOrTheDefaultOrTheRequestedName) {
    auto picA = std::make_shared<v::test::FakeMarkPicture>(qs("a-pic"), 1);
    auto picB = std::make_shared<v::test::FakeMarkPicture>(qs("b-pic"), 2);
    auto picDefault = std::make_shared<v::test::FakeMarkPicture>(qs("default-pic"), 3);
    struct Row {
        const char* what;
        std::vector<QString> names;
        std::vector<v::MarkPicturePtr> items;
        v::MarkPicturePtr fallback;
        QString requested;
        QString expectedName;
        v::MarkPicturePtr expectedPic;
    };
    const Row rows[] = {
        {"found by name: the found picture's name",
         {qs("a.png"), qs("b.png")},
         {picA, picB},
         picDefault,
         qs("b.png"),
         qs("b-pic"),
         picB},
        {"not found: the default picture's name",
         {qs("a.png")},
         {picA},
         picDefault,
         qs("zz.png"),
         qs("default-pic"),
         picDefault},
        {"not found, no default: the requested name and no picture",
         {qs("a.png")},
         {picA},
         nullptr,
         qs("zz.png"),
         qs("zz.png"),
         nullptr},
        {"found, picture null: the requested name, the default is not asked",
         {qs("n.png")},
         {nullptr},
         picDefault,
         qs("n.png"),
         qs("n.png"),
         nullptr},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.what);
        Fixture f;
        f.pictures->names = row.names;
        f.pictures->items = row.items;
        f.pictures->fallback = row.fallback;
        f.categories->categories.push_back(t::smlCategory(7, kDbId, qs("Cat")));
        const auto item =
            f.factory.createMark(1, qs("p"), true, row.requested, 7, qs(""), t::point(), 1U, 2U, 3, 4);
        ASSERT_NE(item, nullptr);
        const auto* icon = dynamic_cast<const v::IAppearancePointIcon*>(item->appearance().get());
        ASSERT_NE(icon, nullptr);
        EXPECT_EQ(icon->picName(), row.expectedName);
        EXPECT_EQ(icon->pic(), row.expectedPic);
    }
}

TEST(MarkFactorySmlDbInternal, CreateMarkGivesNullForNoGeometryOrAMultiPointAndLooksUpTheCategoryFirst) {
    Fixture f;
    f.categories->categories.push_back(t::smlCategory(7, kDbId, qs("Cat")));
    // no geometry: nil, and the category is not looked up (:377-379)
    EXPECT_EQ(f.factory.createMark(1, qs("n"), true, qs(""), 7, qs(""), nullptr, 1U, 2U, 3, 4), nullptr);
    EXPECT_EQ(f.categories->lookups, 0);
    // a multi-point: the category is looked up (:379), then nil (no branch of :380-420 takes it)
    EXPECT_EQ(
        f.factory.createMark(2, qs("m"), true, qs(""), 7, qs(""), t::multiPointGeometry(), 1U, 2U, 3, 4),
        nullptr);
    EXPECT_EQ(f.categories->lookups, 1);
    // a category id that is not in this database: the mark is built with no category (categoryById gives
    // nil), and its categoryId is -1 (the SML "no category" id, u_MarkId.pas:122)
    const auto item = f.factory.createMark(3, qs("p"), true, qs(""), 99, qs(""), t::point(), 1U, 2U, 3, 4);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(f.categories->lookups, 2);
    ASSERT_NE(withCategory(item->mainInfo()), nullptr);
    EXPECT_EQ(withCategory(item->mainInfo())->category(), nullptr);
    const auto* internal = dynamic_cast<const m::IMarkSMLInternal*>(item->mainInfo().get());
    ASSERT_NE(internal, nullptr);
    EXPECT_EQ(internal->categoryId(), -1);
}

TEST(MarkFactorySmlDbInternal, CreateInternalMarkKeepsOwnMarksAndReplacesAForeignCategoryByName) {
    struct Row {
        const char* what;
        // builds the mark from the fixture's collaborators; `this` / `other` are the categories of the two
        // DBs
        v::VectorDataItemPtr (*build)(Fixture& f, const m::MarkCategoryPtr& thisCat,
                                      const m::MarkCategoryPtr& otherCity,
                                      const m::MarkCategoryPtr& otherRiver);
        bool replaced; // true: a new mark with the category of this database
        std::vector<QString> namesAsked;
    };
    const Row rows[] = {
        {"own database mark, foreign category: kept (:239-243)",
         [](Fixture& f, const m::MarkCategoryPtr&, const m::MarkCategoryPtr& otherCity,
            const m::MarkCategoryPtr&) { return smlMark(f, kDbId, otherCity); },
         false,
         {}},
        {"other database, category of this database: kept (:249 not taken)",
         [](Fixture& f, const m::MarkCategoryPtr& thisCat, const m::MarkCategoryPtr&,
            const m::MarkCategoryPtr&) { return smlMark(f, kOtherDbId, thisCat); },
         false,
         {}},
        {"other database, foreign category with the same name here: replaced (:249-253)",
         [](Fixture& f, const m::MarkCategoryPtr&, const m::MarkCategoryPtr& otherCity,
            const m::MarkCategoryPtr&) { return smlMark(f, kOtherDbId, otherCity); },
         true,
         {qs("Город")}},
        {"other database, foreign category with no name here: kept (:258)",
         [](Fixture& f, const m::MarkCategoryPtr&, const m::MarkCategoryPtr&,
            const m::MarkCategoryPtr& otherRiver) { return smlMark(f, kOtherDbId, otherRiver); },
         false,
         {qs("Река")}},
        {"other database, no category: kept (:249 not taken)",
         [](Fixture& f, const m::MarkCategoryPtr&, const m::MarkCategoryPtr&, const m::MarkCategoryPtr&) {
             return smlMark(f, kOtherDbId, nullptr);
         },
         false,
         {}},
        {"SQLite mark, foreign category with the name here: replaced (:239 not taken)",
         [](Fixture& f, const m::MarkCategoryPtr&, const m::MarkCategoryPtr&,
            const m::MarkCategoryPtr&) -> v::VectorDataItemPtr {
             auto info = std::make_shared<const m::MarkIdORM>(
                 t::converter(), 9, m::MarkIdType::Point, qs("Город"), qs("город"), 3, kOtherDbId, 1,
                 t::ormCategory(5, kOtherDbId, qs("Город")), true);
             auto appearance = f.appearances->createPointAppearance(0x1U, 0x2U, 3, qs("p"), nullptr, 4);
             return std::make_shared<const v::VectorDataItem>(7, std::move(appearance), std::move(info),
                                                              t::point());
         },
         true,
         {qs("Город")}},
        {"plain main info without a category: kept (:246 not taken)",
         [](Fixture&, const m::MarkCategoryPtr&, const m::MarkCategoryPtr&, const m::MarkCategoryPtr&) {
             return t::dataItem(qs("Город"), t::point());
         },
         false,
         {}},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.what);
        Fixture f;
        const auto thisCat = t::smlCategory(7, kDbId, qs("Город"));
        const auto otherCity = t::smlCategory(9, kOtherDbId, qs("Город"));
        const auto otherRiver = t::smlCategory(10, kOtherDbId, qs("Река"));
        f.categories->categories.push_back(thisCat);
        const auto mark = row.build(f, thisCat, otherCity, otherRiver);
        const auto result = f.factory.createInternalMark(mark);
        EXPECT_EQ(f.categories->namesAsked, row.namesAsked);
        if (!row.replaced) {
            EXPECT_EQ(result, mark);
            continue;
        }
        ASSERT_NE(result, nullptr);
        EXPECT_NE(result, mark);
        EXPECT_EQ(result->name(), qs("Город"));
        EXPECT_EQ(result->desc(), qs("город"));
        ASSERT_NE(withCategory(result->mainInfo()), nullptr);
        EXPECT_EQ(withCategory(result->mainInfo())->category(), thisCat);
    }
    // a null mark gives a null result (every branch is skipped, :239-259)
    Fixture f;
    EXPECT_EQ(f.factory.createInternalMark(nullptr), nullptr);
}

} // namespace
