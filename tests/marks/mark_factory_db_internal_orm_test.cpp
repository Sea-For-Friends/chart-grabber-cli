// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkFactoryDbInternalORM.pas (485): CreatePoint / CreateLine / CreatePoly, both CreateMark
// overloads, GeoTypeToMarkType, CreateMarkId, CreateInternalMark (docs/specs/marks.md section 2.9.10).
// Expected hashes are assembled from the Pascal recipe and the raw CityHash primitive, not read from the
// factory. This ticket implements no feature (Features: — in its header).
#include "cg/core/hash.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mormot/marks/mark_db_internal_orm.h"
#include "cg/marks/mormot/marks/mark_factory_db_internal_orm.h"
#include "cg/marks/mormot/marks/mark_id_orm.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/appearance_of_vector_item.h"
#include "cg/vector/vector_data_item_base.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
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

namespace {

constexpr quintptr kDbId = 0x51;      // the database the factory belongs to
constexpr quintptr kOtherDbId = 0x77; // another database

QString qs(const char* utf8) {
    return QString::fromUtf8(utf8);
}

using t::FakePictureList;

// This database's categories (the ORM category database is ticket O7, not landed): look-ups by row id and by
// name, and a count of the row-id look-ups (legacy GetCategoryByID runs once per CreateMark).
class FakeCategoryDb final : public m::IMarkCategoryDbInternalORM {
public:
    std::vector<m::MarkCategoryPtr> categories; // in list order
    mutable int lookups = 0;                    // getCategoryById calls
    mutable std::vector<QString> namesAsked;    // getFirstCategoryByName arguments, in call order

    bool isCategoryFromThisDb(const v::CategoryPtr& category) const override {
        for (const auto& c : categories) {
            if (c.get() == category.get()) {
                return true;
            }
        }
        return false;
    }
    m::MarkCategoryPtr getCategoryById(qint64 id) const override {
        ++lookups;
        for (const auto& c : categories) {
            if (rowId(*c) == id) {
                return c;
            }
        }
        return nullptr;
    }
    m::MarkCategoryPtr getFirstCategoryByName(const QString& name) const override {
        namesAsked.push_back(name);
        for (const auto& c : categories) {
            if (c->name() == name) {
                return c;
            }
        }
        return nullptr;
    }
    int getCategoryByNameCount(const QString& name) const override {
        int count = 0;
        for (const auto& c : categories) {
            count += c->name() == name ? 1 : 0;
        }
        return count;
    }

private:
    static qint64 rowId(const m::IMarkCategory& category) {
        const auto* internal = dynamic_cast<const m::IMarkCategoryInternalORM*>(&category);
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
    m::MarkFactoryDbInternalORM factory{kDbId,       pictures,       appearances,
                                        markFactory, t::converter(), categories};
};

g::LonLatPointsPtr twoPoints() {
    return std::make_shared<const std::vector<g::LonLat>>(
        std::vector<g::LonLat>{g::LonLat{1.0, 2.0}, g::LonLat{3.0, 4.0}});
}

g::GeometryLonLatPtr pointGeom(HashValue hash) {
    return std::make_shared<const g::GeometryLonLatPoint>(hash, g::LonLat{1.0, 2.0});
}

g::GeometryLonLatPtr lineGeom(HashValue hash) {
    return std::make_shared<const g::GeometryLonLatSingleLine>(hash, g::LonLatRect{}, twoPoints(),
                                                               g::PointsMeta{});
}

g::GeometryLonLatPtr multiLineGeom(HashValue hash) {
    auto first = std::make_shared<const g::GeometryLonLatSingleLine>(hash + 1, g::LonLatRect{}, twoPoints(),
                                                                     g::PointsMeta{});
    auto second = std::make_shared<const g::GeometryLonLatSingleLine>(hash + 2, g::LonLatRect{}, twoPoints(),
                                                                      g::PointsMeta{});
    return std::make_shared<const g::GeometryLonLatMultiLine>(
        hash, g::LonLatRect{}, std::vector<g::GeometryLonLatSingleLinePtr>{first, second});
}

g::GeometryLonLatPtr polyGeom(HashValue hash) {
    auto outer = std::make_shared<const g::GeometryLonLatContour>(hash, g::LonLatRect{}, twoPoints());
    return std::make_shared<const g::GeometryLonLatSinglePolygon>(hash, g::LonLatRect{}, outer,
                                                                  std::vector<g::GeometryLonLatContourPtr>{});
}

g::GeometryLonLatPtr multiPointGeom(HashValue hash) {
    return std::make_shared<const g::GeometryLonLatMultiPoint>(hash, g::LonLatRect{}, twoPoints());
}

// The main-info hash recipe (:200-201): hash of the name, then the description chained onto it.
HashValue mainInfoHash(const QString& name, const QString& desc) {
    const std::string n = t::utf16LeBytes(name);
    const std::string d = t::utf16LeBytes(desc);
    return t::hashChainBytes(cg::core::cityHash64(n.data(), n.size()), d.data(), d.size());
}

const v::IVectorDataItemWithCategory* withCategory(const v::VectorDataItemMainInfoPtr& info) {
    return dynamic_cast<const v::IVectorDataItemWithCategory*>(info.get());
}

// An SQLite-style mark main info of database `dbId` with `category` (the MarkIdORM of O8).
std::shared_ptr<const m::MarkIdORM> markIdOf(quintptr dbId, v::CategoryPtr category,
                                             const QString& name = qs("N"), const QString& desc = qs("D")) {
    return std::make_shared<const m::MarkIdORM>(t::converter(), 9, m::MarkIdType::Point, name, desc, 5, dbId,
                                                1, std::move(category), true);
}

v::VectorDataItemPtr itemWith(v::VectorDataItemMainInfoPtr info, v::AppearancePtr appearance = nullptr) {
    return std::make_shared<const v::VectorDataItem>(7, std::move(appearance), std::move(info), t::point());
}

TEST(MarkFactoryDbInternalORM, CreateMarkBuildsEachKindWithItsHashRecipe) {
    Fixture f;
    f.categories->categories.push_back(t::ormCategory(7, kDbId, qs("Cat")));
    struct Row {
        const char* what;
        g::GeometryLonLatPtr geometry;
        m::MarkIdType type;
        int multiGeometryCount;
    };
    const Row rows[] = {
        {"point", pointGeom(0x1111), m::MarkIdType::Point, 1},
        {"line", lineGeom(0x2222), m::MarkIdType::Line, 1},
        {"polygon", polyGeom(0x3333), m::MarkIdType::Poly, 1},
        {"multi-line of two", multiLineGeom(0x4444), m::MarkIdType::Line, 2},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.what);
        const auto item = f.factory.createMark(101, qs("Город"), true, qs("a.png"), 7, qs("город"),
                                               row.geometry, 0x11U, 0x22U, 3, 4);
        ASSERT_NE(item, nullptr);
        EXPECT_EQ(item->name(), qs("Город"));
        EXPECT_EQ(item->desc(), qs("город"));
        EXPECT_EQ(item->geometry(), row.geometry);

        const auto* id = dynamic_cast<const m::IMarkId*>(item->mainInfo().get());
        ASSERT_NE(id, nullptr);
        EXPECT_EQ(id->markType(), row.type);
        EXPECT_EQ(id->multiGeometryCount(), row.multiGeometryCount);
        const auto* internal = dynamic_cast<const m::IMarkInternalORM*>(item->mainInfo().get());
        ASSERT_NE(internal, nullptr);
        EXPECT_EQ(internal->id(), 101);
        EXPECT_EQ(internal->dbId(), kDbId);
        EXPECT_EQ(internal->categoryId(), 7);
        EXPECT_TRUE(internal->visible());
        ASSERT_NE(withCategory(item->mainInfo()), nullptr);
        EXPECT_EQ(withCategory(item->mainInfo())->category(), f.categories->categories[0]);
        EXPECT_EQ(item->hintText(), qs("Город|город"));

        // main info: name, then description (:200-201); item: geometry, main info, appearance (:216-218)
        const HashValue mainHash = mainInfoHash(qs("Город"), qs("город"));
        EXPECT_EQ(item->mainInfo()->hash(), mainHash);
        ASSERT_NE(item->appearance(), nullptr);
        EXPECT_EQ(item->hash(), t::hashChainU64(t::hashChainU64(row.geometry->hash(), mainHash),
                                                item->appearance()->hash()));
    }
}

TEST(MarkFactoryDbInternalORM, CreateMarkMapsColoursAndScalesToTheRightFields) {
    Fixture f;
    f.categories->categories.push_back(t::ormCategory(7, kDbId, qs("Cat")));
    // color1 = 0x11000001, color2 = 0x22000002, scale1 = 3, scale2 = 4: each swapped argument shows up.
    const auto point = f.factory.createMark(1, qs("p"), true, qs(""), 7, qs(""), pointGeom(1), 0x11000001U,
                                            0x22000002U, 3, 4);
    const auto* caption = dynamic_cast<const v::IAppearancePointCaption*>(point->appearance().get());
    const auto* icon = dynamic_cast<const v::IAppearancePointIcon*>(point->appearance().get());
    ASSERT_NE(caption, nullptr);
    ASSERT_NE(icon, nullptr);
    EXPECT_EQ(caption->textColor(), 0x11000001U);
    EXPECT_EQ(caption->textBgColor(), 0x22000002U);
    EXPECT_EQ(caption->fontSize(), 3);
    EXPECT_EQ(icon->markerSize(), 4);

    const auto line = f.factory.createMark(2, qs("l"), true, qs(""), 7, qs(""), lineGeom(2), 0x11000001U,
                                           0x22000002U, 3, 4);
    const auto* lineApp = dynamic_cast<const v::IAppearanceLine*>(line->appearance().get());
    ASSERT_NE(lineApp, nullptr);
    EXPECT_EQ(lineApp->lineColor(), 0x11000001U); // colour2 and scale2 are not used by a line
    EXPECT_EQ(lineApp->lineWidth(), 3);

    const auto poly = f.factory.createMark(3, qs("y"), true, qs(""), 7, qs(""), polyGeom(3), 0x11000001U,
                                           0x22000002U, 3, 4);
    const auto* border = dynamic_cast<const v::IAppearancePolygonBorder*>(poly->appearance().get());
    const auto* fill = dynamic_cast<const v::IAppearancePolygonFill*>(poly->appearance().get());
    ASSERT_NE(border, nullptr);
    ASSERT_NE(fill, nullptr);
    EXPECT_EQ(border->lineColor(), 0x11000001U);
    EXPECT_EQ(border->lineWidth(), 3);
    EXPECT_EQ(fill->fillColor(), 0x22000002U);
}

TEST(MarkFactoryDbInternalORM, PointPictureFallbackTable) {
    // Legacy :176-187: the index of the name, then get(index); no index gives the default picture; a null
    // picture keeps the requested name (the default is not used when the index was found).
    const auto picA = std::make_shared<v::test::FakeMarkPicture>(qs("a.png"), 1);
    const auto picB = std::make_shared<v::test::FakeMarkPicture>(qs("b.png"), 2);
    const auto picDefault = std::make_shared<v::test::FakeMarkPicture>(qs("default.png"), 3);
    struct Row {
        const char* what;
        std::vector<QString> names;
        std::vector<v::MarkPicturePtr> items;
        v::MarkPicturePtr fallback;
        QString requested;
        QString expectName;
        v::MarkPicturePtr expectPic;
    };
    const Row rows[] = {
        {"found by name",
         {qs("a.png"), qs("b.png")},
         {picA, picB},
         picDefault,
         qs("b.png"),
         qs("b.png"),
         picB},
        {"missing, default exists",
         {qs("a.png"), qs("b.png")},
         {picA, picB},
         picDefault,
         qs("zzz.png"),
         qs("default.png"),
         picDefault},
        {"missing, no default", {qs("a.png")}, {picA}, nullptr, qs("zzz.png"), qs("zzz.png"), nullptr},
        {"found, picture null", {qs("x.png")}, {nullptr}, picDefault, qs("x.png"), qs("x.png"), nullptr},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.what);
        Fixture f;
        f.pictures->names = row.names;
        f.pictures->items = row.items;
        f.pictures->fallback = row.fallback;
        f.categories->categories.push_back(t::ormCategory(7, kDbId, qs("Cat")));
        const auto item =
            f.factory.createMark(1, qs("p"), true, row.requested, 7, qs(""), pointGeom(1), 1, 2, 3, 4);
        ASSERT_NE(item, nullptr);
        const auto* icon = dynamic_cast<const v::IAppearancePointIcon*>(item->appearance().get());
        ASSERT_NE(icon, nullptr);
        EXPECT_EQ(icon->picName(), row.expectName);
        EXPECT_EQ(icon->pic(), row.expectPic);
    }
}

TEST(MarkFactoryDbInternalORM, NoPointLineOrPolygonGivesNullAndLookupOnlyWhenThereIsAGeometry) {
    // :346-348: a nil geometry gives nil before the category look-up. A multi-point has no branch: nil
    // after the look-up, silently (:346 Result := nil).
    struct Row {
        const char* what;
        g::GeometryLonLatPtr geometry;
        int expectLookups;
    };
    const Row rows[] = {
        {"nil geometry", nullptr, 0},
        {"multi-point (no branch)", multiPointGeom(1), 1},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.what);
        Fixture f;
        f.categories->categories.push_back(t::ormCategory(7, kDbId, qs("Cat")));
        EXPECT_EQ(f.factory.createMark(1, qs("p"), true, qs("a.png"), 7, qs("d"), row.geometry, 1, 2, 3, 4),
                  nullptr);
        EXPECT_EQ(f.categories->lookups, row.expectLookups);
    }
}

TEST(MarkFactoryDbInternalORM, RecordOverloadPassesEveryField) {
    Fixture f;
    f.categories->categories.push_back(t::ormCategory(7, kDbId, qs("Cat")));
    m::OrmMarkRec rec;
    rec.markId = 33;
    rec.categoryId = 7;
    rec.picName = qs("zzz.png"); // no picture list entry: the name is kept
    rec.name = qs("N");
    rec.desc = qs("D");
    rec.visible = false;
    rec.color1 = 0xA1000001U;
    rec.color2 = 0xB2000002U;
    rec.scale1 = 5;
    rec.scale2 = 6;
    rec.geometry = pointGeom(0x33);
    rec.geoType = m::OrmGeoType::Point;
    rec.geoCount = 9; // not used by createMark: the multi-geometry count is calculated from the geometry

    const auto item = f.factory.createMark(rec);
    ASSERT_NE(item, nullptr);
    const auto* internal = dynamic_cast<const m::IMarkInternalORM*>(item->mainInfo().get());
    ASSERT_NE(internal, nullptr);
    EXPECT_EQ(internal->id(), 33);
    EXPECT_EQ(internal->categoryId(), 7);
    EXPECT_FALSE(internal->visible());
    EXPECT_EQ(internal->name(), qs("N"));
    EXPECT_EQ(item->desc(), qs("D"));
    EXPECT_EQ(item->geometry(), rec.geometry);
    const auto* caption = dynamic_cast<const v::IAppearancePointCaption*>(item->appearance().get());
    const auto* icon = dynamic_cast<const v::IAppearancePointIcon*>(item->appearance().get());
    ASSERT_NE(caption, nullptr);
    ASSERT_NE(icon, nullptr);
    EXPECT_EQ(caption->textColor(), 0xA1000001U);
    EXPECT_EQ(caption->textBgColor(), 0xB2000002U);
    EXPECT_EQ(caption->fontSize(), 5);
    EXPECT_EQ(icon->markerSize(), 6);
    EXPECT_EQ(icon->picName(), qs("zzz.png"));
    EXPECT_EQ(dynamic_cast<const m::IMarkId*>(item->mainInfo().get())->multiGeometryCount(), 1);
}

TEST(MarkFactoryDbInternalORM, CreateMarkIdTakesTheTypeAndHashesNameThenDesc) {
    Fixture f;
    const auto own = t::ormCategory(7, kDbId, qs("Cat"));
    f.categories->categories.push_back(own);
    struct Row {
        const char* what;
        m::OrmGeoType geoType;
        qint64 categoryId;
        m::MarkIdType type;
        v::CategoryPtr category;
    };
    const Row rows[] = {
        {"point", m::OrmGeoType::Point, 7, m::MarkIdType::Point, own},
        {"line", m::OrmGeoType::Line, 7, m::MarkIdType::Line, own},
        {"polygon", m::OrmGeoType::Poly, 7, m::MarkIdType::Poly, own},
        {"unknown category id", m::OrmGeoType::Point, 99, m::MarkIdType::Point, nullptr},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.what);
        m::OrmMarkRec rec;
        rec.markId = 44;
        rec.categoryId = row.categoryId;
        rec.name = qs("Имя");
        rec.desc = qs("опис");
        rec.visible = false;
        rec.geoType = row.geoType;
        rec.geoCount = 5; // passed through as legacy does (FGeoCount)
        const m::MarkIdPtr id = f.factory.createMarkId(rec);
        ASSERT_NE(id, nullptr);
        EXPECT_EQ(id->markType(), row.type);
        EXPECT_EQ(id->name(), qs("Имя"));
        EXPECT_EQ(id->multiGeometryCount(), 5);
        EXPECT_EQ(id->category(), row.category);
        const auto* internal = dynamic_cast<const m::IMarkInternalORM*>(id.get());
        ASSERT_NE(internal, nullptr);
        EXPECT_EQ(internal->id(), 44);
        EXPECT_EQ(internal->dbId(), kDbId);
        EXPECT_FALSE(internal->visible());
        const auto* info = dynamic_cast<const v::IVectorDataItemMainInfo*>(id.get());
        ASSERT_NE(info, nullptr);
        EXPECT_EQ(info->hash(), mainInfoHash(qs("Имя"), qs("опис")));
    }
}

TEST(MarkFactoryDbInternalORM, UndefinedAndOutOfRangeGeoTypeThrowAfterTheLookup) {
    // :413-423. Legacy raises plain Exception; the port throws std::runtime_error with the same message.
    struct Row {
        const char* what;
        m::OrmGeoType geoType;
        const char* message;
    };
    const Row rows[] = {
        {"undefined", m::OrmGeoType::Undef, "MarkSystemORM: Undefined GeoType!"},
        {"out of range", static_cast<m::OrmGeoType>(7), "MarkSystemORM: Unknown GeoType!"},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.what);
        Fixture f;
        m::OrmMarkRec rec;
        rec.geoType = row.geoType;
        try {
            (void)f.factory.createMarkId(rec);
            ADD_FAILURE() << "no exception";
        } catch (const std::runtime_error& e) {
            EXPECT_STREQ(e.what(), row.message);
        }
        EXPECT_EQ(f.categories->lookups, 1); // :435 runs before the conversion at :441
    }
}

TEST(MarkFactoryDbInternalORM, CreateInternalMarkReturnsTheMarkItselfWhenItStaysValid) {
    // :462-482. Each row is a mark that CreateInternalMark leaves as it is; the first row carries a foreign
    // category whose name exists in this database, so a look-up by name would change it.
    Fixture f;
    const auto own = t::ormCategory(7, kDbId, qs("Own"));
    f.categories->categories.push_back(own);
    const auto foreignNamedOwn = std::make_shared<const m::MarkCategory>(qs("Own"), true, 3, 23);
    struct Row {
        const char* what;
        v::VectorDataItemPtr mark;
    };
    const Row rows[] = {
        {"same database, foreign category", itemWith(markIdOf(kDbId, foreignNamedOwn))},
        {"other database, category of this database", itemWith(markIdOf(kOtherDbId, own))},
        {"no category information",
         itemWith(std::make_shared<const v::VectorDataItemMainInfo>(1, t::converter(), qs("n"), qs("d")))},
        {"other database, nil category", itemWith(markIdOf(kOtherDbId, nullptr))},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.what);
        EXPECT_EQ(f.factory.createInternalMark(row.mark), row.mark);
    }
    EXPECT_TRUE(f.categories->namesAsked.empty()); // no look-up by name in any of these rows
    EXPECT_EQ(f.factory.createInternalMark(nullptr), nullptr);
}

TEST(MarkFactoryDbInternalORM, CreateInternalMarkReplacesAForeignCategoryByNameOrKeepsIt) {
    // :469-476. A category of another database is replaced by this database's category of the same name
    // through IMarkFactory::replaceCategory; without one, the mark is returned unchanged.
    struct Row {
        const char* what;
        QString categoryName;
        bool replaced;
    };
    const Row rows[] = {
        {"same name exists in this database", qs("Own"), true},
        {"no such name in this database", qs("Missing"), false},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.what);
        Fixture f; // a fresh database per row, so the names asked are this row's only
        const auto own = t::ormCategory(7, kDbId, qs("Own"));
        f.categories->categories.push_back(own);
        const auto appearance = f.appearances->createPointAppearance(1, 2, 3, qs("a.png"), nullptr, 4);
        const auto foreign = std::make_shared<const m::MarkCategory>(row.categoryName, true, 3, 23);
        const auto mark = itemWith(markIdOf(kOtherDbId, foreign), appearance);
        const auto result = f.factory.createInternalMark(mark);
        ASSERT_NE(result, nullptr);
        ASSERT_EQ(f.categories->namesAsked.size(), 1u);
        EXPECT_EQ(f.categories->namesAsked[0], row.categoryName);
        if (row.replaced) {
            EXPECT_NE(result, mark);
            ASSERT_NE(withCategory(result->mainInfo()), nullptr);
            EXPECT_EQ(withCategory(result->mainInfo())->category(), own);
            EXPECT_EQ(result->name(), mark->name());
            EXPECT_EQ(result->geometry(), mark->geometry());
            EXPECT_EQ(result->appearance(), mark->appearance());
        } else {
            EXPECT_EQ(result, mark);
        }
    }
}

} // namespace
