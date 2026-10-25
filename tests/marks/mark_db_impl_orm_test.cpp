// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports no legacy test (there is none): u_MarkDbImplORM.pas :1-750 (docs/specs/marks.md §2.9.10, §2.9.2,
// §2.9.12) against a copy of tests/fixtures/marks/Marks.db3. Expected values are literals read back with
// plain SQL on a second connection (WAL), never through the unit's own caches. This ticket implements no
// feature (Features: — in its header).
//
// The transaction object is wrapped in the shared SpyTransaction (mark_test_util.h): it records start /
// commit / rollback, counts the BEGINs (a start() whose record is `isInternal` is the call that executes
// `BEGIN TRANSACTION`, mark_system_orm_transaction.cpp:17-38; SqliteDb has no sqlite3_trace hook) and, at
// every real COMMIT, records what a second connection sees (`probe`: by default the number of rows in the
// Mark table).
#include "cg/core/read_write_state_internal.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mormot/categories/mark_category_db_impl_orm.h"
#include "cg/marks/mormot/marks/mark_db_impl_orm.h"
#include "cg/marks/mormot/marks/mark_factory_db_internal_orm.h"
#include "cg/marks/mormot/marks/mark_id_orm.h"
#include "cg/marks/sml/mark_id.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/vector_data_item_base.h"

#include <QStringList>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "../corpus_skip.h"
#include "mark_db_impl_orm_helper_test_util.h"
#include "mark_test_util.h"

namespace cg::marks {
// Declared a friend by the class: the private methods.
struct MarkDbImplORMTestAccess {
    static OrmMarkRec ormMarkRecFromMark(const vector::IVectorDataItem& m) {
        return MarkDbImplORM::ormMarkRecFromMark(m);
    }
    static qint64 getCategoryId(const vector::CategoryPtr& c) { return MarkDbImplORM::getCategoryId(c); }
    static bool getMarkInternalId(const MarkDbImplORM& db, const MarkOrId& m, qint64& id) {
        return db.getMarkInternalId(m, id);
    }
};
} // namespace cg::marks

namespace m = cg::marks;
namespace v = cg::vector;
namespace g = cg::geo;
namespace tu = cg::marks::testutil;
using cg::core::ReadWriteStateInternal;

namespace {

constexpr quintptr kDbId = 7;
constexpr quintptr kOtherDbId = 8;
const QString kTown = QString::fromUtf8("Город");
const QString kStreet = QString::fromUtf8("Улица");
const QString kDesc = QString::fromUtf8("Описание");

class SpyProvider final : public m::IMarkSystemImplORMClientProvider {
public:
    SpyProvider(m::IMarkSystemImplORMClientProvider& real, std::function<qint64()> markRows)
        : real_(real), tx_(real.transaction(), std::move(markRows)) {}
    [[nodiscard]] qint64 userId() const override { return real_.userId(); }
    [[nodiscard]] cg::storage::sqlite::SqliteDb& db() override { return real_.db(); }
    [[nodiscard]] std::recursive_mutex& dbMutex() override { return real_.dbMutex(); }
    [[nodiscard]] m::IMarkSystemORMTransaction& transaction() override { return tx_; }
    tu::SpyTransaction& spy() { return tx_; }

private:
    m::IMarkSystemImplORMClientProvider& real_;
    tu::SpyTransaction tx_;
};

// An item whose geometry is null (legacy allows it; the landed VectorDataItem does not).
class NoGeometryItem final : public v::IVectorDataItem {
public:
    explicit NoGeometryItem(v::VectorDataItemMainInfoPtr info) : info_(std::move(info)) {}
    [[nodiscard]] cg::core::HashValue hash() const noexcept override { return 0; }
    [[nodiscard]] v::VectorDataItemMainInfoPtr mainInfo() const override { return info_; }
    [[nodiscard]] QString name() const override { return info_->name(); }
    [[nodiscard]] QString desc() const override { return info_->desc(); }
    [[nodiscard]] g::GeometryLonLatPtr geometry() const override { return nullptr; }
    [[nodiscard]] v::AppearancePtr appearance() const override { return nullptr; }
    [[nodiscard]] bool isEqual(const v::IVectorDataItem*) const override { return false; }
    [[nodiscard]] QString hintText() const override { return {}; }
    [[nodiscard]] QString infoUrl() const override { return {}; }
    [[nodiscard]] QString infoHtml() const override { return {}; }
    [[nodiscard]] QString infoCaption() const override { return {}; }

private:
    v::VectorDataItemMainInfoPtr info_;
};

// The real category database and the real mark factory over the spy provider: the marks are created through
// the unit under test, the categories through the category database.
struct Rig {
    tu::Env env;
    std::shared_ptr<ReadWriteStateInternal> state = std::make_shared<ReadWriteStateInternal>(true, true);
    SpyProvider spyProvider{env.provider(), [this] { return env.count("Mark"); }};
    std::shared_ptr<m::MarkCategoryDbImplORM> categoryDb =
        std::make_shared<m::MarkCategoryDbImplORM>(kDbId, state, spyProvider);
    v::AppearanceOfMarkFactoryPtr appearances = std::make_shared<v::AppearanceOfMarkFactory>();
    std::shared_ptr<const m::IMarkFactory> markFactory =
        std::make_shared<m::MarkFactory>(nullptr, nullptr, appearances, tu::converter());
    std::shared_ptr<const m::MarkFactoryDbInternalORM> factory =
        std::make_shared<m::MarkFactoryDbInternalORM>(kDbId, std::make_shared<tu::FakePictureList>(),
                                                      appearances, markFactory, tu::converter(), categoryDb);
    std::unique_ptr<m::MarkDbImplORM> db;
    int notifications = 0;
    cg::core::Subscription notifySub;

    Rig() { makeDb(); }
    void makeDb() {
        notifySub = {}; // before the database it listens to goes away
        db.reset();
        db = std::make_unique<m::MarkDbImplORM>(kDbId, 1, state, spyProvider, factory, tu::kFactory,
                                                std::make_shared<v::VectorItemSubsetBuilderFactory>());
        notifySub = db->changeNotifier().subscribe([this] { ++notifications; });
    }
    tu::SpyTransaction& spy() { return spyProvider.spy(); }

    m::MarkCategoryPtr category(const QString& name) {
        return categoryDb->updateCategory(nullptr,
                                          std::make_shared<const m::MarkCategory>(name, true, 3, 23));
    }

    // A new (not yet stored) mark of the MarkFactory: plain main info, the category object as given.
    v::VectorDataItemPtr newMark(g::GeometryLonLatPtr geometry, const QString& name,
                                 const m::MarkCategoryPtr& cat, v::AppearancePtr appearance = nullptr,
                                 const QString& desc = {}) const {
        if (!appearance) {
            appearance = dynamic_cast<const g::IGeometryLonLatPoint*>(geometry.get()) != nullptr
                             ? appearances->createPointAppearance(0xFF112233U, 0xFF445566U, 13,
                                                                  QStringLiteral("pin"), nullptr, 17)
                             : appearances->createLineAppearance(0xFF778899U, 5);
        }
        return markFactory->createMark(std::move(geometry), name, desc, cat, std::move(appearance));
    }
    v::VectorDataItemPtr newPoint(const QString& name, const m::MarkCategoryPtr& cat) const {
        return newMark(tu::makePoint(), name, cat);
    }
};

qint64 idOf(const v::VectorDataItemPtr& mark) {
    if (!mark) {
        return -2; // reported instead of a crash
    }
    const auto info = mark->mainInfo();
    const auto* internal = dynamic_cast<const m::IMarkInternalORM*>(info.get());
    return internal != nullptr ? internal->id() : -1;
}

QStringList markNames(Rig& rig) {
    return rig.env.rows("SELECT ID,mCategory,mName FROM Mark ORDER BY ID");
}

m::MarkList list(std::vector<v::VectorDataItemPtr> items) {
    return std::make_shared<const std::vector<v::VectorDataItemPtr>>(std::move(items));
}

// A stored-looking mark that has no description row: a MarkIdORM main info with the given ids.
v::VectorDataItemPtr internalItem(qint64 id, quintptr dbId, const v::CategoryPtr& category,
                                  g::GeometryLonLatPtr geometry, v::AppearancePtr appearance = nullptr,
                                  bool visible = true) {
    auto info = std::make_shared<const m::MarkIdORM>(tu::converter(), 0, m::MarkIdType::Point, kTown, kDesc,
                                                     id, dbId, 1, category, visible);
    return std::make_shared<const v::VectorDataItem>(0, std::move(appearance), std::move(info),
                                                     std::move(geometry));
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// ormMarkRecFromMark (:409-529): the appearance mapping of §2.9.2, one table
// ---------------------------------------------------------------------------------------------------------------

namespace {

struct RecRow {
    const char* label;
    int kind; // 0 point, 1 line, 2 multi-line, 3 polygon, 4 multi-polygon
    quint32 c1, c2;
    qint32 s1, s2;
    m::OrmGeoType geoType;
    quint32 e1, e2;
    qint32 es1, es2;
    qint32 geoCount;
    const char* pic;
};

g::GeometryLonLatPtr geometryOfKind(int kind) {
    switch (kind) {
    case 0:
        return tu::makePoint();
    case 1:
        return tu::makeLine();
    case 2:
        return tu::makeMultiLine();
    case 3:
        return tu::makePolygon();
    default: {
        g::GeometryLonLatPolygonBuilder builder; // two outer contours
        builder.addOuter(std::make_shared<const std::vector<g::LonLat>>(
            std::vector<g::LonLat>{{1.5, 2.5}, {3.5, 2.5}, {3.5, 4.5}}));
        builder.addOuter(std::make_shared<const std::vector<g::LonLat>>(
            std::vector<g::LonLat>{{11.5, 12.5}, {13.5, 12.5}, {13.5, 14.5}}));
        return builder.makeStaticAndClear();
    }
    }
}

} // namespace

TEST(MarkDbImplORM, OrmMarkRecFromMarkTable) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr cat = rig.category(kStreet);
    const qint64 catId = dynamic_cast<const m::IMarkCategoryInternalORM*>(cat.get())->id();
    ASSERT_GT(catId, 0);

    // c1 / c2 / s1 / s2 are the arguments of MarkFactoryDbInternalORM::createMark: the point takes text
    // colour, text background, font size, marker size; the line takes colour 1 and width 1 (colour 2 / scale
    // 2 are not used); the polygon takes border, fill, width. The expectations are written from :463-525.
    const RecRow rows[] = {
        {"point", 0, 0xFF112233U, 0xFF445566U, 13, 17, m::OrmGeoType::Point, 0xFF112233U, 0xFF445566U, 13, 17,
         1, "pin.png"},
        {"line", 1, 0xFF778899U, 0xFF010203U, 5, 9, m::OrmGeoType::Line, 0xFF778899U, 0, 5, 0, 1, ""},
        {"multi line", 2, 0xFF778899U, 0xFF010203U, 6, 9, m::OrmGeoType::Line, 0xFF778899U, 0, 6, 0, 2, ""},
        {"polygon", 3, 0xFFAABBCCU, 0xFFDDEEFFU, 7, 9, m::OrmGeoType::Poly, 0xFFAABBCCU, 0xFFDDEEFFU, 7, 0, 1,
         ""},
        {"multi polygon", 4, 0xFFAABBCCU, 0xFFDDEEFFU, 8, 9, m::OrmGeoType::Poly, 0xFFAABBCCU, 0xFFDDEEFFU, 8,
         0, 2, ""},
    };
    for (const RecRow& row : rows) {
        SCOPED_TRACE(row.label);
        // the main info of the database (IMarkInternalORM): id 42, hidden, the category row id
        const auto mark =
            rig.factory->createMark(42, kTown, false, QString::fromLatin1(row.pic[0] != 0 ? row.pic : ""),
                                    catId, kDesc, geometryOfKind(row.kind), row.c1, row.c2, row.s1, row.s2);
        ASSERT_TRUE(mark);
        const m::OrmMarkRec rec = m::MarkDbImplORMTestAccess::ormMarkRecFromMark(*mark);
        EXPECT_EQ(rec.markId, 42);
        EXPECT_EQ(rec.categoryId, catId);
        EXPECT_FALSE(rec.visible);
        EXPECT_EQ(rec.name, kTown);
        EXPECT_EQ(rec.desc, kDesc);
        EXPECT_EQ(rec.geometry.get(), mark->geometry().get());
        EXPECT_EQ(rec.geoType, row.geoType);
        EXPECT_EQ(rec.color1, row.e1);
        EXPECT_EQ(rec.color2, row.e2);
        EXPECT_EQ(rec.scale1, row.es1);
        EXPECT_EQ(rec.scale2, row.es2);
        EXPECT_EQ(rec.geoCount, row.geoCount);
        EXPECT_EQ(rec.picName, QString::fromLatin1(row.pic));
        EXPECT_EQ(rec.picId, 0);
        EXPECT_EQ(rec.appearanceId, 0);
        EXPECT_EQ(rec.viewId, 0);
    }
}

TEST(MarkDbImplORM, OrmMarkRecFromMarkCategoryRouteAndMissingAppearance) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr cat = rig.category(kStreet);
    const qint64 catId = dynamic_cast<const m::IMarkCategoryInternalORM*>(cat.get())->id();

    // :442-446: a mark of the MarkFactory has no IMarkInternalORM; its category gives the row id; markId 0
    // and visible stay at their defaults
    const auto fresh = rig.newPoint(kTown, cat);
    ASSERT_TRUE(fresh);
    m::OrmMarkRec rec = m::MarkDbImplORMTestAccess::ormMarkRecFromMark(*fresh);
    EXPECT_EQ(rec.markId, 0);
    EXPECT_EQ(rec.categoryId, catId);
    EXPECT_TRUE(rec.visible);
    EXPECT_EQ(rec.name, kTown);
    EXPECT_EQ(rec.geoType, m::OrmGeoType::Point);
    EXPECT_EQ(rec.picName, QStringLiteral("pin"));
    EXPECT_EQ(rec.color1, 0xFF112233U);
    EXPECT_EQ(rec.scale1, 13);
    EXPECT_EQ(rec.scale2, 17);

    // no appearance (nil): colours and scales stay 0, no picture name; one row per geometry kind
    const std::pair<g::GeometryLonLatPtr, m::OrmGeoType> bare[] = {{tu::makePoint(), m::OrmGeoType::Point},
                                                                   {tu::makeLine(), m::OrmGeoType::Line},
                                                                   {tu::makePolygon(), m::OrmGeoType::Poly}};
    for (const auto& [geometry, type] : bare) {
        rec = m::MarkDbImplORMTestAccess::ormMarkRecFromMark(*internalItem(5, kDbId, cat, geometry));
        EXPECT_EQ(rec.geoType, type);
        EXPECT_EQ(rec.color1, 0U);
        EXPECT_EQ(rec.color2, 0U);
        EXPECT_EQ(rec.scale1, 0);
        EXPECT_EQ(rec.scale2, 0);
        EXPECT_TRUE(rec.picName.isEmpty());
        EXPECT_EQ(rec.markId, 5);
    }
    // a point with a line appearance: neither caption nor icon facet -> zeros
    rec = m::MarkDbImplORMTestAccess::ormMarkRecFromMark(
        *internalItem(5, kDbId, cat, tu::makePoint(), rig.appearances->createLineAppearance(0xFF778899U, 5)));
    EXPECT_EQ(rec.color1, 0U);
    EXPECT_EQ(rec.scale1, 0);
    // a polygon with a line appearance: neither border nor fill facet -> zeros
    rec = m::MarkDbImplORMTestAccess::ormMarkRecFromMark(*internalItem(
        5, kDbId, cat, tu::makePolygon(), rig.appearances->createLineAppearance(0xFF778899U, 5)));
    EXPECT_EQ(rec.color1, 0U);
    EXPECT_EQ(rec.color2, 0U);
    // a line with a polygon appearance: no line facet -> zeros
    rec = m::MarkDbImplORMTestAccess::ormMarkRecFromMark(
        *internalItem(5, kDbId, cat, tu::makeLine(),
                      rig.appearances->createPolygonAppearance(0xFFAABBCCU, 7, 0xFFDDEEFFU)));
    EXPECT_EQ(rec.color1, 0U);
    EXPECT_EQ(rec.scale1, 0);
}

TEST(MarkDbImplORM, OrmMarkRecFromMarkExitsAndErrors) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr cat = rig.category(kStreet);

    // :449-452 (Assert(False); Exit is off in the shipped build): a mark whose category is not an SQLite
    // category keeps the ids of its main info and nothing else (no name, no geometry)
    const v::CategoryPtr plain = std::make_shared<const m::MarkCategory>(kTown, true, 3, 23);
    m::OrmMarkRec rec = m::MarkDbImplORMTestAccess::ormMarkRecFromMark(
        *internalItem(42, kDbId, plain, tu::makePoint(), nullptr, false));
    EXPECT_EQ(rec.markId, 42);
    EXPECT_EQ(rec.categoryId, 0);
    EXPECT_FALSE(rec.visible);
    EXPECT_TRUE(rec.name.isEmpty());
    EXPECT_FALSE(rec.geometry);
    // a main info without any category interface: also the empty record
    rec = m::MarkDbImplORMTestAccess::ormMarkRecFromMark(*tu::dataItem(kTown, tu::makePoint()));
    EXPECT_EQ(rec.markId, 0);
    EXPECT_EQ(rec.categoryId, 0);
    EXPECT_TRUE(rec.name.isEmpty());

    // :454 AV on a nil geometry: reproduced as an exception
    // (VectorDataItem asserts a geometry, so the item is a fake)
    const NoGeometryItem noGeometry(internalItem(5, kDbId, cat, tu::makePoint())->mainInfo());
    EXPECT_THROW(std::ignore = m::MarkDbImplORMTestAccess::ormMarkRecFromMark(noGeometry),
                 m::MarkSystemORMError);
    // :526-528: a multi-point is none of point / line / polygon
    try {
        std::ignore = m::MarkDbImplORMTestAccess::ormMarkRecFromMark(
            *internalItem(5, kDbId, cat, tu::multiPointGeometry()));
        FAIL() << "no exception";
    } catch (const m::MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: Unknown geometry type!");
    }
}

TEST(MarkDbImplORM, GetCategoryId) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr cat = rig.category(kStreet);
    EXPECT_EQ(m::MarkDbImplORMTestAccess::getCategoryId(cat), 1);
    EXPECT_EQ(m::MarkDbImplORMTestAccess::getCategoryId(tu::ormCategory(77, kOtherDbId)), 77); // any database
    EXPECT_EQ(m::MarkDbImplORMTestAccess::getCategoryId(std::make_shared<const v::Category>(kTown)), 0);
    EXPECT_EQ(m::MarkDbImplORMTestAccess::getCategoryId(nullptr), 0);
}

// ---------------------------------------------------------------------------------------------------------------
// getMarkInternalId (:531-550)
// ---------------------------------------------------------------------------------------------------------------

TEST(MarkDbImplORM, GetMarkInternalIdTable) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr cat = rig.category(kStreet);
    const auto get = [&](const m::MarkOrId& mark) {
        qint64 id = -5;
        const bool ok = m::MarkDbImplORMTestAccess::getMarkInternalId(*rig.db, mark, id);
        return std::make_pair(ok, id);
    };
    using Result = std::pair<bool, qint64>;
    // a mark of this database
    EXPECT_EQ(get(internalItem(31, kDbId, cat, tu::makePoint())), (Result{true, 31}));
    // a mark of another database
    EXPECT_EQ(get(internalItem(31, kOtherDbId, cat, tu::makePoint())), (Result{false, 0}));
    // a mark of this database with id 0
    EXPECT_EQ(get(internalItem(0, kDbId, cat, tu::makePoint())), (Result{false, 0}));
    // a mark that is not an SQLite mark
    EXPECT_EQ(get(rig.newPoint(kTown, cat)), (Result{false, 0}));
    // a mark id: the object itself is the IMarkInternalORM
    const m::MarkIdPtr idOfThis = std::make_shared<const m::MarkIdORM>(
        tu::converter(), 0, m::MarkIdType::Line, kTown, kDesc, 52, kDbId, 1, cat, true);
    const m::MarkIdPtr idOfOther = std::make_shared<const m::MarkIdORM>(
        tu::converter(), 0, m::MarkIdType::Line, kTown, kDesc, 52, kOtherDbId, 1, cat, true);
    EXPECT_EQ(get(idOfThis), (Result{true, 52}));
    EXPECT_EQ(get(idOfOther), (Result{false, 0}));
    // nil entries
    EXPECT_EQ(get(v::VectorDataItemPtr{}), (Result{false, 0}));
    EXPECT_EQ(get(m::MarkIdPtr{}), (Result{false, 0}));
}

// ---------------------------------------------------------------------------------------------------------------
// constructor, destructor, onStateChange (:230-305)
// ---------------------------------------------------------------------------------------------------------------

TEST(MarkDbImplORM, ReadOnlyFromTheStateAtConstructionAndAfterAChange) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr cat = rig.category(kStreet);
    const auto oldList = [&] {
        const auto stored = rig.db->updateMark(nullptr, rig.newPoint(kTown, cat));
        EXPECT_TRUE(stored);
        return m::MarkOrIdList(list({stored}));
    }();
    // :672 Start(..., FHelper.IsReadOnly): writable
    rig.spy().reset();
    EXPECT_FALSE(rig.db->updateMarkList(oldList, list({})));
    EXPECT_EQ(rig.spy().events.front(), "start:rw");

    // :297-305: a change of the state flips the helper
    rig.state->setWriteAccess(false);
    rig.spy().reset();
    EXPECT_FALSE(rig.db->updateMarkList(oldList, list({})));
    EXPECT_EQ(rig.spy().events, (QStringList{"start:ro", "commit"}));
    rig.state->setWriteAccess(true);
    rig.spy().reset();
    EXPECT_FALSE(rig.db->updateMarkList(oldList, list({})));
    EXPECT_EQ(rig.spy().events.front(), "start:rw");

    // :261: a database created while the state is read-only starts read-only
    rig.state->setWriteAccess(false);
    rig.makeDb();
    rig.spy().reset();
    EXPECT_FALSE(rig.db->updateMarkList(oldList, list({})));
    EXPECT_EQ(rig.spy().events, (QStringList{"start:ro", "commit"}));
}

TEST(MarkDbImplORM, DestroyedDatabaseIsUnsubscribedFromTheState) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    rig.db.reset();
    rig.state->setWriteAccess(false); // would call into the destroyed object (ASan)
    rig.state->setWriteAccess(true);
    SUCCEED();
}

// ---------------------------------------------------------------------------------------------------------------
// readers (:324-403)
// ---------------------------------------------------------------------------------------------------------------

TEST(MarkDbImplORM, SingleMarkReaders) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr town = rig.category(kTown);
    const m::MarkCategoryPtr street = rig.category(kStreet);
    const auto first = rig.db->updateMark(nullptr, rig.newPoint(kStreet, town));    // id 1
    const auto second = rig.db->updateMark(nullptr, rig.newPoint(kStreet, street)); // id 2, the same name
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_EQ(idOf(first), 1);
    ASSERT_EQ(idOf(second), 2);
    rig.notifications = 0;

    // getById (:339-350)
    EXPECT_EQ(idOf(rig.db->getById(2)), 2);
    EXPECT_EQ(rig.db->getById(2)->name(), kStreet);
    EXPECT_FALSE(rig.db->getById(0));
    EXPECT_FALSE(rig.db->getById(-3));
    EXPECT_FALSE(rig.db->getById(99)); // not in the database

    // markById (:352-372): the id of the mark id object
    EXPECT_EQ(idOf(rig.db->markById(std::dynamic_pointer_cast<const m::IMarkId>(second->mainInfo()))), 2);
    EXPECT_FALSE(rig.db->markById(nullptr));
    const m::MarkIdPtr zero = std::make_shared<const m::MarkIdORM>(tu::converter(), 0, m::MarkIdType::Point,
                                                                   kTown, kDesc, 0, kDbId, 1, town, true);
    EXPECT_FALSE(rig.db->markById(zero));
    const m::MarkIdPtr missing = std::make_shared<const m::MarkIdORM>(
        tu::converter(), 0, m::MarkIdType::Point, kTown, kDesc, 99, kDbId, 1, town, true);
    EXPECT_FALSE(rig.db->markById(missing));
    // an id of another database is still looked up by its number (:360-363 do not compare dbId)
    const m::MarkIdPtr foreign = std::make_shared<const m::MarkIdORM>(
        tu::converter(), 0, m::MarkIdType::Point, kTown, kDesc, 1, kOtherDbId, 1, town, true);
    EXPECT_EQ(idOf(rig.db->markById(foreign)), 1);

    // firstMarkByName (:374-403)
    EXPECT_EQ(idOf(rig.db->firstMarkByName(kStreet, nullptr)), 1); // no category: first by id
    EXPECT_EQ(idOf(rig.db->firstMarkByName(kStreet, street)), 2);  // in the category
    EXPECT_FALSE(rig.db->firstMarkByName(kTown, street));          // no such name there
    EXPECT_EQ(idOf(rig.db->firstMarkByName(kStreet, tu::ormCategory(2, kOtherDbId))),
              1); // other database: ignored
    EXPECT_EQ(idOf(rig.db->firstMarkByName(kStreet, tu::ormCategory(0, kDbId))), 1); // id 0: ignored
    EXPECT_EQ(idOf(rig.db->firstMarkByName(kStreet, std::make_shared<const v::Category>(kStreet))),
              1); // plain category
    EXPECT_FALSE(rig.db->firstMarkByName(QStringLiteral("nothing"), nullptr));
    EXPECT_EQ(rig.notifications, 0); // readers do not notify
}

// ---------------------------------------------------------------------------------------------------------------
// updateMark (:552-645)
// ---------------------------------------------------------------------------------------------------------------

TEST(MarkDbImplORM, UpdateMarkInsertUpdateEqualDelete) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr town = rig.category(kTown);
    const m::MarkCategoryPtr street = rig.category(kStreet);
    rig.spy().reset();

    // INSERT (:594-600)
    const auto stored =
        rig.db->updateMark(nullptr, rig.newMark(tu::makePoint(), kStreet, town, nullptr, kDesc));
    ASSERT_TRUE(stored);
    EXPECT_EQ(idOf(stored), 1);
    EXPECT_EQ(stored->name(), kStreet);
    EXPECT_EQ(markNames(rig), (QStringList{"1|1|Улица"}));
    EXPECT_EQ(rig.notifications, 1);
    EXPECT_EQ(rig.spy().events, (QStringList{"start:rw", "commit"})); // useTransactions = True

    // UPDATE (:616-622): name and category change
    rig.spy().reset();
    const auto changed = rig.db->updateMark(
        stored,
        rig.markFactory->createMark(tu::makePoint(), kTown, kDesc, street,
                                    rig.appearances->createPointAppearance(
                                        0xFF112233U, 0xFF445566U, 13, QStringLiteral("pin"), nullptr, 17)));
    ASSERT_TRUE(changed);
    EXPECT_EQ(idOf(changed), 1);
    EXPECT_EQ(changed->name(), kTown);
    EXPECT_EQ(markNames(rig), (QStringList{"1|2|Город"}));
    EXPECT_EQ(rig.notifications, 2);

    // equal (:612-614): the old mark is returned, no write, no notification
    rig.spy().reset();
    const auto same = rig.db->updateMark(changed, changed);
    ASSERT_TRUE(same);
    EXPECT_EQ(idOf(same), 1);
    EXPECT_EQ(same->name(), kTown);
    EXPECT_EQ(rig.notifications, 2);
    EXPECT_TRUE(rig.spy().events.isEmpty());
    EXPECT_EQ(markNames(rig), (QStringList{"1|2|Город"}));

    // DELETE (:602-605)
    rig.spy().reset();
    EXPECT_FALSE(rig.db->updateMark(changed, nullptr));
    EXPECT_TRUE(markNames(rig).isEmpty());
    EXPECT_EQ(rig.notifications, 3);
    EXPECT_EQ(rig.spy().events, (QStringList{"start:rw", "commit"}));
}

TEST(MarkDbImplORM, UpdateMarkNothingToDo) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr town = rig.category(kTown);
    const auto stored = rig.db->updateMark(nullptr, rig.newPoint(kStreet, town));
    ASSERT_TRUE(stored);
    rig.notifications = 0;
    rig.spy().reset();

    // both nil: no branch applies
    EXPECT_FALSE(rig.db->updateMark(nullptr, nullptr));
    // :569-572: an old mark with no id of this database (not an SQLite mark / another database / id 0)
    EXPECT_FALSE(rig.db->updateMark(rig.newPoint(kStreet, town), nullptr));
    EXPECT_FALSE(rig.db->updateMark(internalItem(1, kOtherDbId, town, tu::makePoint()), nullptr));
    EXPECT_FALSE(
        rig.db->updateMark(internalItem(0, kDbId, town, tu::makePoint()), rig.newPoint(kTown, town)));
    // :608-611: the old mark is not in the database
    EXPECT_FALSE(
        rig.db->updateMark(internalItem(99, kDbId, town, tu::makePoint()), rig.newPoint(kTown, town)));
    EXPECT_EQ(markNames(rig), (QStringList{"1|1|Улица"}));
    EXPECT_EQ(rig.notifications, 0);
    EXPECT_TRUE(rig.spy().events.isEmpty());

    // :578-580: no write access: nothing is done for a given new mark, whatever the old one is (without the
    // check an unchanged pair would return the old mark, and a changed one would go on to the helper)
    const QStringList viewsBefore = rig.env.views();
    const auto hiddenCopy = internalItem(1, kDbId, town, tu::makePoint(), nullptr, false);
    rig.state->setWriteAccess(false);
    EXPECT_FALSE(rig.db->updateMark(nullptr, rig.newPoint(kTown, town)));
    EXPECT_FALSE(rig.db->updateMark(stored, stored));
    EXPECT_FALSE(rig.db->updateMark(stored, hiddenCopy));
    EXPECT_FALSE(rig.db->updateMark(stored, rig.markFactory->modifyName(stored, kTown)));
    // a delete has no new mark: it reaches the helper, which refuses it while read-only
    EXPECT_FALSE(rig.db->updateMark(stored, nullptr));
    rig.state->setWriteAccess(true);
    EXPECT_EQ(markNames(rig), (QStringList{"1|1|Улица"}));
    EXPECT_EQ(rig.env.views(), viewsBefore);
    EXPECT_EQ(rig.notifications, 0);
}

TEST(MarkDbImplORM, UpdateMarkCategoryOfAnotherDatabaseIsReplacedByName) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr street = rig.category(kStreet);
    const m::MarkCategoryPtr town = rig.category(kTown);
    // createInternalMark (:581): a category object that is not this database's, with the name of one that is
    const m::MarkCategoryPtr foreign = std::make_shared<const m::MarkCategory>(kTown, true, 3, 23);
    const auto stored = rig.db->updateMark(nullptr, rig.newPoint(kStreet, foreign));
    ASSERT_TRUE(stored);
    EXPECT_EQ(markNames(rig),
              (QStringList{"1|2|Улица"})); // under the category "Город" (id 2) of this database

    // no category of that name here: the record has no category (:449-452) and the helper refuses the
    // geometry
    const m::MarkCategoryPtr unknown =
        std::make_shared<const m::MarkCategory>(QStringLiteral("elsewhere"), true, 3, 23);
    EXPECT_THROW(std::ignore = rig.db->updateMark(nullptr, rig.newPoint(kTown, unknown)),
                 m::MarkSystemORMError);
    EXPECT_EQ(markNames(rig), (QStringList{"1|2|Улица"}));
    EXPECT_EQ(rig.notifications, 1);
    // the same for an UPDATE (:617-618): the new record has no category, so it has no geometry and the helper
    // refuses it
    EXPECT_THROW(std::ignore = rig.db->updateMark(stored, rig.newPoint(kStreet, unknown)),
                 m::MarkSystemORMError);
    EXPECT_EQ(markNames(rig), (QStringList{"1|2|Улица"}));
    EXPECT_EQ(rig.notifications, 1);
}

// ---------------------------------------------------------------------------------------------------------------
// updateMarkList (:647-746)
// ---------------------------------------------------------------------------------------------------------------

namespace {

// Names of a result list, "-" for a null entry; "<nil>" for a null list.
QString show(const m::MarkList& l) {
    if (!l) {
        return QStringLiteral("<nil>");
    }
    QStringList out;
    for (const auto& item : *l) {
        out.append(item ? QString::number(idOf(item)) + QLatin1Char(':') + item->name()
                        : QStringLiteral("-"));
    }
    return out.join(QLatin1Char(','));
}

} // namespace

TEST(MarkDbImplORM, UpdateMarkListPairing) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr town = rig.category(kTown);
    const auto make = [&](const QString& n) { return rig.newPoint(n, town); };

    // old nil: every new one is inserted; one notification; one transaction
    rig.spy().reset();
    const m::MarkList stored = rig.db->updateMarkList(
        nullptr, list({make(QStringLiteral("a")), make(QStringLiteral("б")), make(QStringLiteral("c"))}));
    EXPECT_EQ(show(stored), "1:a,2:б,3:c");
    EXPECT_EQ(rig.notifications, 1);
    // the first item changed something at I = 0 (0 mod 1000 = 0, :711-714): committed and reopened
    EXPECT_EQ(rig.spy().events, (QStringList{"start:rw", "commit", "start:rw", "commit"}));
    EXPECT_EQ(markNames(rig), (QStringList{"1|1|a", "2|1|б", "3|1|c"}));

    // old shorter than new (:675-678): the first one is renamed, the tail is inserted
    const m::MarkList second = rig.db->updateMarkList(
        m::MarkOrIdList(list({(*stored)[0]})), list({make(QStringLiteral("A")), make(QStringLiteral("d"))}));
    EXPECT_EQ(show(second), "1:A,4:d");
    EXPECT_EQ(markNames(rig), (QStringList{"1|1|A", "2|1|б", "3|1|c", "4|1|d"}));
    EXPECT_EQ(rig.notifications, 2);

    // old longer than new: the pairs are updated, the tail of the old list is deleted and has no result;
    // unchanged pair (the first, an equal mark) returns the old mark and does not notify by itself
    rig.notifications = 0;
    const m::MarkList third = rig.db->updateMarkList(
        m::MarkOrIdList(list({(*second)[0], (*stored)[1], (*stored)[2]})), list({(*second)[0]}));
    EXPECT_EQ(show(third), "1:A");
    EXPECT_EQ(markNames(rig), (QStringList{"1|1|A", "4|1|d"}));
    EXPECT_EQ(rig.notifications, 1); // the two deletes

    // nothing changes: no notification, the result still has one entry per new mark
    rig.notifications = 0;
    const m::MarkList fourth =
        rig.db->updateMarkList(m::MarkOrIdList(list({(*third)[0]})), list({(*third)[0]}));
    EXPECT_EQ(show(fourth), "1:A");
    EXPECT_EQ(rig.notifications, 0);

    // new list empty (not nil): delete the old ones, the result is nil (an empty list)
    rig.notifications = 0;
    EXPECT_EQ(show(rig.db->updateMarkList(m::MarkOrIdList(list({(*third)[0]})), list({}))), "<nil>");
    EXPECT_EQ(markNames(rig), (QStringList{"4|1|d"}));
    EXPECT_EQ(rig.notifications, 1);

    // null entries in the old list (no id: the entry is skipped with no result) and in the new list
    rig.notifications = 0;
    const m::MarkList fifth = rig.db->updateMarkList(m::MarkOrIdList(list({nullptr, nullptr})),
                                                     list({nullptr, make(QStringLiteral("e"))}));
    EXPECT_EQ(show(fifth), "-,5:e"); // old entry 0 nil + new nil: nothing; old entry 1 nil + new: insert
    EXPECT_EQ(markNames(rig), (QStringList{"4|1|d", "5|1|e"}));
    EXPECT_EQ(rig.notifications, 1);
}

TEST(MarkDbImplORM, UpdateMarkListOldListOfMarkIds) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr town = rig.category(kTown);
    const m::MarkList stored = rig.db->updateMarkList(
        nullptr, list({rig.newPoint(QStringLiteral("a"), town), rig.newPoint(QStringLiteral("b"), town)}));
    ASSERT_TRUE(stored);
    // the old list holds mark ids (decision C3): the id comes from the IMarkInternalORM of the id object
    auto ids = std::make_shared<std::vector<m::MarkIdPtr>>();
    for (const auto& item : *stored) {
        ids->push_back(std::dynamic_pointer_cast<const m::IMarkId>(item->mainInfo()));
    }
    ASSERT_TRUE((*ids)[0] && (*ids)[1]);
    EXPECT_EQ(show(rig.db->updateMarkList(m::MarkOrIdList(m::MarkIdList(ids)),
                                          list({rig.newPoint(QStringLiteral("A"), town), (*stored)[1]}))),
              "1:A,2:b");
    EXPECT_EQ(markNames(rig), (QStringList{"1|1|A", "2|1|b"}));
    // new list nil: delete by id (W-4), no result
    rig.notifications = 0;
    EXPECT_EQ(show(rig.db->updateMarkList(m::MarkOrIdList(m::MarkIdList(ids)), nullptr)), "<nil>");
    EXPECT_TRUE(markNames(rig).isEmpty());
    EXPECT_EQ(rig.notifications, 1);
}

TEST(MarkDbImplORM, UpdateMarkListNewListNil) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr town = rig.category(kTown);
    const m::MarkList stored = rig.db->updateMarkList(
        nullptr, list({rig.newPoint(QStringLiteral("a"), town), rig.newPoint(QStringLiteral("b"), town)}));
    ASSERT_TRUE(stored);
    rig.notifications = 0;
    rig.spy().reset();

    // :733-737: one entry without an id of this database: nothing is deleted at all
    EXPECT_FALSE(rig.db->updateMarkList(
        m::MarkOrIdList(list({(*stored)[0], internalItem(2, kOtherDbId, town, tu::makePoint())})), nullptr));
    EXPECT_EQ(markNames(rig), (QStringList{"1|1|a", "2|1|b"}));
    EXPECT_EQ(rig.notifications, 0);
    // an empty old list: DeleteMarkSQL of no ids reports success (helper :215-217), so legacy notifies
    EXPECT_FALSE(rig.db->updateMarkList(m::MarkOrIdList(list({})), nullptr));
    EXPECT_EQ(rig.notifications, 1);
    // legacy: the old list's Count on nil is an access violation; the facade catches it
    EXPECT_THROW(std::ignore = rig.db->updateMarkList(nullptr, nullptr), std::runtime_error);
    EXPECT_EQ(rig.notifications, 1);

    // both marks: one delete statement set, one notification
    EXPECT_FALSE(rig.db->updateMarkList(m::MarkOrIdList(stored), nullptr));
    EXPECT_TRUE(markNames(rig).isEmpty());
    EXPECT_EQ(rig.notifications, 2);
}

TEST(MarkDbImplORM, UpdateMarkListRollsBackOnAnException) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr town = rig.category(kTown);
    const m::MarkCategoryPtr unknown =
        std::make_shared<const m::MarkCategory>(QStringLiteral("elsewhere"), true, 3, 23);
    rig.spy().reset();
    // the first mark throws (no category here): nothing was written, the transaction is rolled back
    EXPECT_THROW(std::ignore =
                     rig.db->updateMarkList(nullptr, list({rig.newPoint(QStringLiteral("b"), unknown),
                                                           rig.newPoint(QStringLiteral("a"), town)})),
                 m::MarkSystemORMError);
    EXPECT_EQ(rig.spy().events, (QStringList{"start:rw", "rollback"}));
    EXPECT_TRUE(markNames(rig).isEmpty());
    EXPECT_EQ(rig.notifications, 0);

    // the first mark is written and committed at I = 0 (:711-714), the second throws: only the open
    // transaction is rolled back, and no notification is sent (SetChanged is after the commit, :722-724)
    rig.spy().reset();
    EXPECT_THROW(std::ignore =
                     rig.db->updateMarkList(nullptr, list({rig.newPoint(QStringLiteral("a"), town),
                                                           rig.newPoint(QStringLiteral("b"), unknown)})),
                 m::MarkSystemORMError);
    EXPECT_EQ(rig.spy().events, (QStringList{"start:rw", "commit", "start:rw", "rollback"}));
    EXPECT_EQ(markNames(rig), (QStringList{"1|1|a"}));
    EXPECT_EQ(rig.notifications, 0);
}

// 2,001 items: the transaction is committed and reopened when `I mod 1000 = 0` once something has changed
// (:687-690 in the paired part, :711-714 in the tail). Expectations by plain SQL on the file.
TEST(MarkDbImplORM, UpdateMarkListCommitsEveryThousandItems) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr town = rig.category(kTown);
    constexpr int kCount = 2001;
    std::vector<v::VectorDataItemPtr> fresh;
    for (int i = 0; i < kCount; ++i) {
        fresh.push_back(rig.newPoint(QStringLiteral("m%1").arg(i), town));
    }

    // tail part (old nil): the commit is AFTER item 0, 1000 and 2000, then the final one on an empty
    // transaction: four BEGINs; the second connection sees 1, 1001, 2001, 2001 rows at the commits
    rig.spy().reset();
    const m::MarkList stored = rig.db->updateMarkList(nullptr, list(fresh));
    ASSERT_TRUE(stored);
    ASSERT_EQ(stored->size(), static_cast<std::size_t>(kCount));
    EXPECT_EQ(rig.spy().begins, 4);
    EXPECT_EQ(rig.spy().visibleAtCommit, (std::vector<qint64>{1, 1001, 2001, 2001}));
    EXPECT_EQ(rig.env.rows("SELECT COUNT(*),MIN(ID),MAX(ID) FROM Mark"), (QStringList{"2001|1|2001"}));
    EXPECT_EQ(rig.notifications, 1);

    // paired part (old = new length): the commit is BEFORE item 1000 and 2000 (item 0 has changed nothing
    // yet): three BEGINs; at the commits the second connection sees 1000, 2000 and 2001 renamed marks
    std::vector<v::VectorDataItemPtr> renamed;
    for (int i = 0; i < kCount; ++i) {
        renamed.push_back(rig.markFactory->modifyName((*stored)[static_cast<std::size_t>(i)],
                                                      QStringLiteral("n%1").arg(i)));
    }
    rig.spy().reset();
    rig.spy().probe = [&] {
        return rig.env.rows("SELECT COUNT(*) FROM Mark WHERE mName LIKE 'n%'").value(0).toLongLong();
    };
    rig.notifications = 0;
    const m::MarkList updated = rig.db->updateMarkList(m::MarkOrIdList(stored), list(renamed));
    ASSERT_TRUE(updated);
    EXPECT_EQ(updated->size(), static_cast<std::size_t>(kCount));
    EXPECT_EQ(rig.spy().begins, 3);
    // (an update opens its own transaction, which joins the open one: more start/commit pairs, no more
    // BEGINs)
    EXPECT_EQ(rig.spy().visibleAtCommit, (std::vector<qint64>{1000, 2000, 2001}));
    EXPECT_EQ(rig.notifications, 1);
    EXPECT_EQ(rig.env.rows("SELECT mName FROM Mark WHERE ID IN (1,1000,1001,2001) ORDER BY ID"),
              (QStringList{"n0", "n999", "n1000", "n2000"}));
    EXPECT_EQ(rig.env.rows("SELECT COUNT(*) FROM Mark WHERE mName LIKE 'n%'"), (QStringList{"2001"}));

    // nothing changes: no commit in the middle, one transaction
    rig.spy().reset();
    rig.notifications = 0;
    ASSERT_TRUE(rig.db->updateMarkList(
        m::MarkOrIdList(updated), list(std::vector<v::VectorDataItemPtr>(updated->begin(), updated->end()))));
    EXPECT_EQ(rig.spy().begins, 1);
    EXPECT_EQ(rig.notifications, 0);
}

// ---------------------------------------------------------------------------------------------------------------
// threads: only proves that ThreadSanitizer is quiet while two threads use the readers and a writer (every
// public method takes the write lock, §2.9.12); it asserts no ordering.
// ---------------------------------------------------------------------------------------------------------------

TEST(MarkDbImplORM, ConcurrentCallsAreSerialised) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Rig rig;
    const m::MarkCategoryPtr town = rig.category(kTown);
    ASSERT_TRUE(rig.db->updateMark(nullptr, rig.newPoint(kStreet, town)));
    std::latch start(3);
    std::atomic<int> found{0};
    const auto reader = [&] {
        start.arrive_and_wait();
        for (int i = 0; i < 50; ++i) {
            found += rig.db->getById(1) ? 1 : 0;
            found += rig.db->firstMarkByName(kStreet, town) ? 1 : 0;
        }
    };
    std::thread a(reader);
    std::thread b(reader);
    start.arrive_and_wait();
    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(rig.db->updateMark(nullptr, rig.newPoint(QStringLiteral("w%1").arg(i), town)));
    }
    a.join();
    b.join();
    EXPECT_EQ(found.load(), 200);
    EXPECT_EQ(rig.env.count("Mark"), 21);
}

// ---------------------------------------------------------------------------------------------------------------
// :752-1214 (T-0125, O17): id lists, subsets, visibility, text search. One populated database:
//   mark 1 "Город"     category 1 "Город"  point (10,10)  description "Описание"
//   mark 2 "Улица"     category 1          point (20,20)  HIDDEN
//   mark 3 "Alpha one" category 2 "Улица"  point (30,30)
//   mark 4 "beta"      category 3 "Third"  point (40,40)  description "alpha in desc"
// ---------------------------------------------------------------------------------------------------------------

namespace {

g::GeometryLonLatPtr pointAt(double lon, double lat) {
    return tu::kFactory.createPoint(g::LonLat{lon, lat});
}

struct Populated : Rig {
    m::MarkCategoryPtr a, b, c;
    v::VectorDataItemPtr m1, m2, m3, m4;

    Populated() {
        a = category(kTown);
        b = category(kStreet);
        c = category(QStringLiteral("Third"));
        m1 = db->updateMark(nullptr, newMark(pointAt(10, 10), kTown, a, nullptr, kDesc));
        m2 = db->updateMark(nullptr, newMark(pointAt(20, 20), kStreet, a));
        m3 = db->updateMark(nullptr, newMark(pointAt(30, 30), QStringLiteral("Alpha one"), b));
        m4 = db->updateMark(nullptr, newMark(pointAt(40, 40), QStringLiteral("beta"), c, nullptr,
                                             QStringLiteral("alpha in desc")));
        db->setMarkVisible(m2, false);
        notifications = 0;
        spy().reset();
    }
    QStringList views() {
        return env.rows("SELECT mvMark,mvCategory,mvVisible FROM MarkView ORDER BY mvMark");
    }
    // the id object of a mark, as allMarkIdList returns it
    m::MarkIdPtr idObject(const v::VectorDataItemPtr& mark) const {
        return std::dynamic_pointer_cast<const m::IMarkId>(mark->mainInfo());
    }
};

QStringList namesOf(const v::VectorItemSubsetPtr& subset) {
    QStringList out;
    for (int i = 0; subset && i < subset->count(); ++i) {
        out.append(subset->item(i)->name());
    }
    return out;
}

QStringList idsOf(const m::MarkIdList& ids) {
    QStringList out;
    for (const auto& id : ids ? *ids : std::vector<m::MarkIdPtr>{}) {
        out.append(QString::number(dynamic_cast<const m::IMarkInternalORM*>(id.get())->id()) +
                   QLatin1Char(':') + id->name());
    }
    return out;
}

const QString kAlpha = QStringLiteral("Alpha one");
const QString kBeta = QStringLiteral("beta");

m::MarkCategoryListPtr categories(std::vector<m::MarkCategoryPtr> items) {
    return m::MarkCategoryList::build(std::move(items));
}

} // namespace

// :786-800
TEST(MarkDbImplORM, MarkIdListsAllByCategoryAndForeignCategory) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Populated p;
    // hidden marks are included, ascending mark id
    EXPECT_EQ(idsOf(p.db->allMarkIdList()), (QStringList{"1:Город", "2:Улица", "3:Alpha one", "4:beta"}));
    EXPECT_EQ(idsOf(p.db->markIdListByCategory(p.a)), (QStringList{"1:Город", "2:Улица"}));
    EXPECT_EQ(idsOf(p.db->markIdListByCategory(p.c)), (QStringList{"4:beta"}));
    // not of this backend / no id / no category: nil (:795-799), not an empty list
    EXPECT_EQ(p.db->markIdListByCategory(std::make_shared<const v::Category>(kTown)), nullptr);
    EXPECT_EQ(p.db->markIdListByCategory(tu::ormCategory(0, kDbId)), nullptr);
    EXPECT_EQ(p.db->markIdListByCategory(tu::smlCategory(2, kDbId)), nullptr);
    EXPECT_EQ(p.db->markIdListByCategory(nullptr), nullptr);
    // a category of this backend without marks: the list is empty, and an empty list is nil too
    EXPECT_EQ(p.db->markIdListByCategory(tu::ormCategory(77, kDbId)), nullptr);
    EXPECT_EQ(p.notifications, 0);
    // an empty database
    Rig empty;
    EXPECT_EQ(empty.db->allMarkIdList(), nullptr);
}

namespace {
struct SubsetRow {
    const char* label;
    std::function<m::MarkCategoryListPtr(Populated&)> list; // an empty function: not a list call
    std::function<v::CategoryPtr(Populated&)> category;
    bool hidden;
    QStringList expected; // in the order returned
};
} // namespace

// :839-881
TEST(MarkDbImplORM, MarkSubsetsByCategoryAndByCategoryList) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    const auto plain = [](Populated&) -> v::CategoryPtr {
        return std::make_shared<const v::Category>(kTown);
    };
    const QStringList all{kTown, kStreet, kAlpha, kBeta};
    const QStringList visibleOnly{kTown, kAlpha, kBeta};
    const SubsetRow rows[] = {
        {"one category", {}, [](Populated& p) -> v::CategoryPtr { return p.a; }, true, {kTown, kStreet}},
        {"one category, no hidden", {}, [](Populated& p) -> v::CategoryPtr { return p.a; }, false, {kTown}},
        {"null category: every mark", {}, [](Populated&) -> v::CategoryPtr { return nullptr; }, true, all},
        {"null category, no hidden",
         {},
         [](Populated&) -> v::CategoryPtr { return nullptr; },
         false,
         visibleOnly},
        // assert compiled out (:848-853): a category of another backend has id 0, which means every mark
        {"foreign category: every mark", {}, plain, true, all},
        {"category without id: every mark",
         {},
         [](Populated&) -> v::CategoryPtr { return tu::ormCategory(0); },
         false,
         visibleOnly},
        {"null list: every mark", [](Populated&) { return m::MarkCategoryListPtr{}; }, {}, true, all},
        {"null list, no hidden", [](Populated&) { return m::MarkCategoryListPtr{}; }, {}, false, visibleOnly},
        // list order, not mark id order
        {"list c, a", [](Populated& p) { return categories({p.c, p.a}); }, {}, true, {kBeta, kTown, kStreet}},
        {"list c, a, no hidden",
         [](Populated& p) { return categories({p.c, p.a}); },
         {},
         false,
         {kBeta, kTown}},
        // an entry of another backend is skipped (:872-877, assert off)
        {"list with a foreign entry",
         [](Populated& p) { return categories({p.c, tu::smlCategory(5, kDbId), p.b}); },
         {},
         true,
         {kBeta, kAlpha}},
        {"list of foreign entries only",
         [](Populated&) { return categories({tu::smlCategory(5, kDbId)}); },
         {},
         true,
         {}},
        {"list of a category without marks",
         [](Populated&) { return categories({tu::ormCategory(77, kDbId)}); },
         {},
         true,
         {}},
    };
    for (const SubsetRow& row : rows) {
        SCOPED_TRACE(row.label);
        Populated p;
        const auto subset = row.list ? p.db->markSubsetByCategoryList(row.list(p), row.hidden)
                                     : p.db->markSubsetByCategory(row.category(p), row.hidden);
        EXPECT_EQ(namesOf(subset), row.expected);
        EXPECT_EQ(subset == nullptr, row.expected.isEmpty()); // an empty subset is nil
        EXPECT_EQ(p.notifications, 0);
    }
}

TEST(MarkDbImplORM, MarkSubsetMarksAreFullMarks) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Populated p;
    const auto subset = p.db->markSubsetByCategory(p.a, true);
    ASSERT_EQ(subset->count(), 2);
    const auto mark = subset->item(0);
    EXPECT_EQ(mark->desc(), kDesc);
    const auto* point = dynamic_cast<const g::IGeometryLonLatPoint*>(mark->geometry().get());
    ASSERT_NE(point, nullptr);
    EXPECT_EQ(point->point().lon, 10.0);
    EXPECT_EQ(point->point().lat, 10.0);
    const auto* icon = dynamic_cast<const v::IAppearancePointIcon*>(mark->appearance().get());
    ASSERT_NE(icon, nullptr);
    EXPECT_EQ(icon->markerSize(), 17);
    EXPECT_EQ(dynamic_cast<const m::IMarkInternalORM*>(mark->mainInfo().get())->visible(), true);
    EXPECT_EQ(dynamic_cast<const m::IMarkInternalORM*>(subset->item(1)->mainInfo().get())->visible(), false);
}

// :806-837: Capacity := VCount on a builder that already holds marks; a capacity below the count raises
// EListError in every build (TList.SetCapacity), and the landed builder does the same.
TEST(MarkDbImplORM, MarkSubsetByCategoryListWithALaterSmallerCategoryThrowsLikeLegacy) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Populated p;
    // category 1 has two marks (hidden included), category 3 one: Capacity := 1 with 2 marks present
    EXPECT_THROW(std::ignore = p.db->markSubsetByCategoryList(categories({p.a, p.c}), true),
                 std::invalid_argument);
    // the lock was released by the exception
    EXPECT_EQ(namesOf(p.db->markSubsetByCategoryList(categories({p.c, p.a}), true)),
              (QStringList{kBeta, kTown, kStreet}));
    // without the hidden mark both categories have one mark: no shrink
    EXPECT_EQ(namesOf(p.db->markSubsetByCategoryList(categories({p.a, p.c}), false)),
              (QStringList{kTown, kBeta}));
}

namespace {
struct RectRow {
    const char* label;
    g::DoubleRect rect; // left, top, right, bottom
    std::function<m::MarkCategoryListPtr(Populated&)> list;
    std::function<v::CategoryPtr(Populated&)> category;
    bool hidden;
    QStringList expected; // sorted
};
} // namespace

// :911-982, spec 2.9.9
TEST(MarkDbImplORM, MarkSubsetsInARectangle) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    const g::DoubleRect world{0, 50, 50, 0};
    const g::DoubleRect firstTwo{5, 25, 25, 5};
    const g::DoubleRect nowhere{60, 70, 70, 60};
    const auto catA = [](Populated& p) -> v::CategoryPtr { return p.a; };
    const auto catB = [](Populated& p) -> v::CategoryPtr { return p.b; };
    const auto none = [](Populated&) -> v::CategoryPtr { return nullptr; };
    const auto plain = [](Populated&) -> v::CategoryPtr {
        return std::make_shared<const v::Category>(kTown);
    };
    const QStringList all{kAlpha, kBeta, kTown, kStreet}; // sorted by QString order: Latin before Cyrillic
    const RectRow rows[] = {
        {"rect, no category", firstTwo, {}, none, true, {kTown, kStreet}},
        {"rect, no category, no hidden", firstTwo, {}, none, false, {kTown}},
        {"world, category", world, {}, catA, true, {kTown, kStreet}},
        {"world, other category", world, {}, catB, true, {kAlpha}},
        {"nowhere", nowhere, {}, none, true, {}},
        // :926-932, assert off: the id list stays empty, and an empty list means every category
        {"foreign category: every mark in the rect", world, {}, plain, true, all},
        {"foreign category, no hidden", firstTwo, {}, plain, false, {kTown}},
        {"null list: every mark", world, [](Populated&) { return m::MarkCategoryListPtr{}; }, {}, true, all},
        {"list a, c",
         world,
         [](Populated& p) { return categories({p.a, p.c}); },
         {},
         true,
         {kBeta, kTown, kStreet}},
        {"list a, c, no hidden",
         world,
         [](Populated& p) { return categories({p.a, p.c}); },
         {},
         false,
         {kBeta, kTown}},
        {"list a, c in a rect",
         firstTwo,
         [](Populated& p) { return categories({p.c, p.a}); },
         {},
         true,
         {kTown, kStreet}},
        // :965-974: the invalid entry is skipped, what is left is passed
        {"list with a foreign entry",
         world,
         [](Populated& p) { return categories({tu::smlCategory(5, kDbId), p.b}); },
         {},
         true,
         {kAlpha}},
        // spec 2.9.9: nothing left -> empty id list -> every mark
        {"list of foreign entries only: every mark",
         world,
         [](Populated&) { return categories({tu::smlCategory(5, kDbId), tu::smlCategory(6, kDbId)}); },
         {},
         true,
         all},
        {"foreign entries only, no hidden",
         world,
         [](Populated&) { return categories({tu::smlCategory(5, kDbId)}); },
         {},
         false,
         {kAlpha, kBeta, kTown}},
        {"nowhere, list", nowhere, [](Populated& p) { return categories({p.a}); }, {}, true, {}},
    };
    for (const RectRow& row : rows) {
        SCOPED_TRACE(row.label);
        Populated p;
        const g::DoublePoint size{0.0, 0.0};
        const auto subset =
            row.list ? p.db->markSubsetByCategoryListInRect(row.rect, row.list(p), row.hidden, size)
                     : p.db->markSubsetByCategoryInRect(row.rect, row.category(p), row.hidden, size);
        QStringList names = namesOf(subset);
        names.sort();
        EXPECT_EQ(names, row.expected);
        EXPECT_EQ(subset == nullptr, row.expected.isEmpty());
        EXPECT_EQ(p.notifications, 0);
    }
}

// :988-1010
TEST(MarkDbImplORM, VisibilityGetters) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Populated p;
    EXPECT_TRUE(p.db->markVisible(p.m1));
    EXPECT_FALSE(p.db->markVisible(p.m2));
    EXPECT_TRUE(p.db->markVisible(nullptr));
    EXPECT_TRUE(p.db->markVisible(tu::dataItem(kTown, tu::makePoint()))); // main info without the interface
    EXPECT_TRUE(p.db->markVisibleById(p.idObject(p.m3)));
    EXPECT_FALSE(p.db->markVisibleById(p.idObject(p.m2)));
    EXPECT_TRUE(p.db->markVisibleById(nullptr));
    // an id of the other backend
    const m::MarkIdPtr sml =
        std::make_shared<const m::MarkId>(tu::converter(), 0, m::MarkIdType::Point, kTown, kDesc, 3, kDbId, 1,
                                          tu::smlCategory(1, kDbId), false);
    EXPECT_TRUE(p.db->markVisibleById(sml));
    // the id objects of allMarkIdList carry the state read from the database
    const auto ids = p.db->allMarkIdList();
    ASSERT_EQ(ids->size(), 4U);
    EXPECT_TRUE(p.db->markVisibleById((*ids)[0]));
    EXPECT_FALSE(p.db->markVisibleById((*ids)[1]));
}

// §2.9.12: the two getters take no lock (:988-1010). Latches only; the overlap is asserted: the getters
// return while another thread holds the write lock, and that thread is still inside it when they return.
TEST(MarkDbImplORM, VisibilityGettersNeedNoLock) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Populated p;
    const m::MarkIdPtr hiddenId = p.idObject(p.m2);
    std::latch held(1);
    std::latch release(1);
    std::atomic<bool> holding{false};
    std::thread holder([&] {
        const auto guard = p.db->writeLock();
        holding = true;
        held.count_down();
        release.wait();
        holding = false;
    });
    held.wait();
    auto getters = std::async(std::launch::async, [&] {
        const bool byMark = p.db->markVisible(p.m2);
        const bool byId = p.db->markVisibleById(hiddenId);
        return std::make_tuple(byMark, byId, holding.load());
    });
    // a getter that waited for the lock would not return before `release`; the timeout only bounds the
    // failure
    const bool returned = getters.wait_for(std::chrono::seconds(30)) == std::future_status::ready;
    release.count_down();
    holder.join();
    ASSERT_TRUE(returned);
    const auto [byMark, byId, stillHeld] = getters.get();
    EXPECT_TRUE(stillHeld); // the overlap really happened
    EXPECT_FALSE(byMark);
    EXPECT_FALSE(byId);
}

// :1012-1030
TEST(MarkDbImplORM, SetAllMarksInCategoryVisible) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Populated p;
    EXPECT_EQ(p.views(), (QStringList{"2|1|0"}));
    p.db->setAllMarksInCategoryVisible(p.a, false); // hides the visible mark 1 too
    EXPECT_EQ(p.views(), (QStringList{"1|1|0", "2|1|0"}));
    EXPECT_EQ(p.notifications, 1);
    p.db->setAllMarksInCategoryVisible(p.a, true);
    EXPECT_EQ(p.views(), (QStringList{"1|1|1", "2|1|1"})); // the rows survive (LC-19)
    EXPECT_EQ(p.notifications, 2);
    // a category that is not of this backend, without an id, absent: nothing
    p.db->setAllMarksInCategoryVisible(std::make_shared<const v::Category>(kTown), false);
    p.db->setAllMarksInCategoryVisible(tu::ormCategory(0), false);
    p.db->setAllMarksInCategoryVisible(nullptr, false);
    EXPECT_EQ(p.views(), (QStringList{"1|1|1", "2|1|1"}));
    EXPECT_EQ(p.notifications, 2);
    // another category is untouched
    p.db->setAllMarksInCategoryVisible(p.c, false);
    EXPECT_EQ(p.views(), (QStringList{"1|1|1", "2|1|1", "4|3|0"}));
    EXPECT_EQ(p.notifications, 3);
}

// :1032-1096, both setters
TEST(MarkDbImplORM, SetMarkVisibleAndSetMarkVisibleById) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    for (const bool byId : {false, true}) {
        SCOPED_TRACE(byId ? "by id" : "by mark");
        Populated p;
        const auto set = [&](const v::VectorDataItemPtr& mark, bool visible) {
            byId ? p.db->setMarkVisibleById(p.idObject(mark), visible) : p.db->setMarkVisible(mark, visible);
        };
        const auto visible = [&](const v::VectorDataItemPtr& mark) {
            return byId ? p.db->markVisibleById(p.idObject(mark)) : p.db->markVisible(mark);
        };
        // hide: the flag of the object flips, the row is written in its own transaction, one notification
        set(p.m3, false);
        EXPECT_FALSE(visible(p.m3));
        EXPECT_EQ(p.views(), (QStringList{"2|1|0", "3|2|0"}));
        EXPECT_EQ(p.notifications, 1);
        EXPECT_EQ(p.spy().events, (QStringList{"start:rw", "commit"}));
        // the same value again: leaves at once, no write, no notification
        p.spy().reset();
        set(p.m3, false);
        EXPECT_EQ(p.notifications, 1);
        EXPECT_TRUE(p.spy().events.isEmpty());
        // show again: the row stays with visible = 1
        set(p.m3, true);
        EXPECT_TRUE(visible(p.m3));
        EXPECT_EQ(p.views(), (QStringList{"2|1|0", "3|2|1"}));
        EXPECT_EQ(p.notifications, 2);
        // nil: nothing
        byId ? p.db->setMarkVisibleById(nullptr, false) : p.db->setMarkVisible(nullptr, false);
        EXPECT_EQ(p.notifications, 2);

        // id 0 / category 0: the flag of the object flips, no row is written, no notification
        const auto noId = internalItem(0, kDbId, p.a, tu::makePoint());
        const auto noCategory = internalItem(9, kDbId, tu::ormCategory(0), tu::makePoint());
        for (const auto& mark : {noId, noCategory}) {
            set(mark, false);
            EXPECT_FALSE(visible(mark));
        }
        EXPECT_EQ(p.views(), (QStringList{"2|1|0", "3|2|1"}));
        EXPECT_EQ(p.notifications, 2);
        // the early exit looks at the FLAG OF THE OBJECT, not at the row: this object says "visible" while
        // the row of mark 2 says hidden, so asking for "visible" writes nothing
        const auto stale = internalItem(2, kDbId, p.a, tu::makePoint(), nullptr, true);
        set(stale, true);
        EXPECT_EQ(p.views(), (QStringList{"2|1|0", "3|2|1"}));
        EXPECT_EQ(p.notifications, 2);
        // an object without the internal interface (a fresh mark / an id of the other backend): nothing
        const auto fresh = p.newPoint(kTown, p.a);
        const m::MarkIdPtr sml =
            std::make_shared<const m::MarkId>(tu::converter(), 0, m::MarkIdType::Point, kTown, kDesc, 3,
                                              kDbId, 1, tu::smlCategory(1, kDbId), true);
        byId ? p.db->setMarkVisibleById(sml, false) : p.db->setMarkVisible(fresh, false);
        EXPECT_TRUE(byId ? p.db->markVisibleById(sml) : p.db->markVisible(fresh));
        EXPECT_EQ(p.views(), (QStringList{"2|1|0", "3|2|1"}));
        EXPECT_EQ(p.notifications, 2);
    }
}

namespace {
m::MarkIdList idList(std::vector<m::MarkIdPtr> ids) {
    return std::make_shared<const std::vector<m::MarkIdPtr>>(std::move(ids));
}
} // namespace

// :1098-1146
TEST(MarkDbImplORM, SetMarkVisibleByIdListUsesOneTransaction) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Populated p;
    const auto ids = idList({p.idObject(p.m1), p.idObject(p.m2), p.idObject(p.m3), p.idObject(p.m4)});
    // mark 2 is hidden already and is skipped; 1, 3, 4 change; one BEGIN, one notification
    p.db->setMarkVisibleByIdList(ids, false);
    EXPECT_EQ(p.spy().begins, 1);
    EXPECT_EQ(p.spy().events, (QStringList{"start:rw", "commit"}));
    EXPECT_EQ(p.notifications, 1);
    EXPECT_EQ(p.views(), (QStringList{"1|1|0", "2|1|0", "3|2|0", "4|3|0"}));
    EXPECT_FALSE(p.db->markVisibleById((*ids)[0])); // the id objects flipped
    EXPECT_FALSE(p.db->markVisibleById((*ids)[3]));
    // nothing differs: the transaction is still opened and committed, no notification
    p.spy().reset();
    p.db->setMarkVisibleByIdList(ids, false);
    EXPECT_EQ(p.spy().begins, 1);
    EXPECT_EQ(p.notifications, 1);
    // show only some
    p.db->setMarkVisibleByIdList(idList({(*ids)[2], (*ids)[3]}), true);
    EXPECT_EQ(p.views(), (QStringList{"1|1|0", "2|1|0", "3|2|1", "4|3|1"}));
    EXPECT_EQ(p.notifications, 2);
    // nil and empty lists: no transaction
    p.spy().reset();
    p.db->setMarkVisibleByIdList(nullptr, true);
    p.db->setMarkVisibleByIdList(idList({}), true);
    EXPECT_TRUE(p.spy().events.isEmpty());
    // marks (not ids) are skipped by the probe (spec C3 / LC-20): nothing changes, but the transaction runs
    p.db->setMarkVisibleByIdList(list({p.m1, p.m2}), true);
    EXPECT_EQ(p.spy().begins, 1);
    EXPECT_EQ(p.views(), (QStringList{"1|1|0", "2|1|0", "3|2|1", "4|3|1"}));
    EXPECT_EQ(p.notifications, 2);
    // the skip looks at the flag of the object: this id says "visible" while the row of mark 2 says hidden
    const auto stale = internalItem(2, kDbId, p.a, tu::makePoint(), nullptr, true);
    p.db->setMarkVisibleByIdList(idList({std::dynamic_pointer_cast<const m::IMarkId>(stale->mainInfo())}),
                                 true);
    EXPECT_EQ(p.views(), (QStringList{"1|1|0", "2|1|0", "3|2|1", "4|3|1"}));
    EXPECT_EQ(p.notifications, 2);
    // an id without a row id (no write) next to one that changes: the flag of the id without a row id still
    // flips (a visible id asked to hide)
    const auto noId = internalItem(0, kDbId, p.a, tu::makePoint());
    p.db->setMarkVisibleByIdList(
        idList({std::dynamic_pointer_cast<const m::IMarkId>(noId->mainInfo()), (*ids)[2]}), false);
    EXPECT_EQ(p.views(), (QStringList{"1|1|0", "2|1|0", "3|2|0", "4|3|1"}));
    EXPECT_FALSE(p.db->markVisible(noId));
    EXPECT_EQ(p.notifications, 3);
}

// :1113: the transaction is started with the read-only flag of the helper
TEST(MarkDbImplORM, SetMarkVisibleByIdListWhileReadOnly) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Populated p;
    p.state->setWriteAccess(false);
    p.db->setMarkVisibleByIdList(idList({p.idObject(p.m1)}), false);
    EXPECT_EQ(p.spy().events, (QStringList{"start:ro", "commit"}));
    EXPECT_EQ(p.spy().begins, 0);
    EXPECT_EQ(p.views(), (QStringList{"2|1|0"})); // nothing reaches the file
    p.state->setWriteAccess(true);
}

// :1148-1173
TEST(MarkDbImplORM, ToggleMarkVisibleByIdList) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // initially: 1, 3, 4 visible, 2 hidden (and 2 the only MarkView row)
    // visible 2, hidden 1: 2 < 1 is false -> hide (:1170)
    {
        Populated p;
        p.db->toggleMarkVisibleByIdList(idList({p.idObject(p.m1), p.idObject(p.m2), p.idObject(p.m3)}));
        EXPECT_EQ(p.views(), (QStringList{"1|1|0", "2|1|0", "3|2|0"}));
        EXPECT_EQ(p.spy().begins, 1);
        EXPECT_EQ(p.notifications, 1);
    }
    // visible 1, hidden 2: 1 < 2 -> show
    {
        Populated p;
        p.db->setMarkVisible(p.m3, false);
        p.notifications = 0;
        p.db->toggleMarkVisibleByIdList(idList({p.idObject(p.m1), p.idObject(p.m2), p.idObject(p.m3)}));
        EXPECT_EQ(p.views(), (QStringList{"2|1|1", "3|2|1"}));
        EXPECT_TRUE(p.db->markVisible(p.m2));
        EXPECT_EQ(p.notifications, 1);
    }
    // a tie, 1 visible and 1 hidden: 1 < 1 is false -> hide
    {
        Populated p;
        p.db->toggleMarkVisibleByIdList(idList({p.idObject(p.m1), p.idObject(p.m2)}));
        EXPECT_EQ(p.views(), (QStringList{"1|1|0", "2|1|0"}));
    }
    // an element without the internal interface counts for neither side: 1 visible, 1 hidden, 5 foreign
    // ids -> a tie -> hide
    {
        Populated p;
        const m::MarkIdPtr sml =
            std::make_shared<const m::MarkId>(tu::converter(), 0, m::MarkIdType::Point, kTown, kDesc, 3,
                                              kDbId, 1, tu::smlCategory(1, kDbId), false);
        p.db->toggleMarkVisibleByIdList(
            idList({sml, sml, sml, sml, sml, p.idObject(p.m1), p.idObject(p.m2)}));
        EXPECT_EQ(p.views(), (QStringList{"1|1|0", "2|1|0"}));
    }
    // marks only: both counts 0, 0 < 0 is false -> "hide", which skips every element: nothing happens
    {
        Populated p;
        p.db->toggleMarkVisibleByIdList(list({p.m1, p.m3}));
        EXPECT_EQ(p.views(), (QStringList{"2|1|0"}));
        EXPECT_EQ(p.notifications, 0);
        EXPECT_EQ(p.spy().begins, 1);
    }
    // nil / empty: no transaction
    {
        Populated p;
        p.db->toggleMarkVisibleByIdList(nullptr);
        p.db->toggleMarkVisibleByIdList(idList({}));
        EXPECT_TRUE(p.spy().events.isEmpty());
    }
}

namespace {
struct FindRow {
    const char* label;
    QString search;
    int maxCount;
    bool hidden;
    bool inDescription;
    QStringList expected; // in the order returned
};
} // namespace

// :1179-1212
TEST(MarkDbImplORM, FindMarksByNameDescriptionAndMaxCount) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    const FindRow rows[] = {
        {"name", QString::fromUtf8("город"), 0, true, false, {kTown}},
        // the text is lower-cased with AnsiLowerCase (D-97) before MATCH
        {"upper-case Cyrillic", QString::fromUtf8("ГОРОД"), 0, true, false, {kTown}},
        {"hidden mark found", QString::fromUtf8("улица"), 0, true, false, {kStreet}},
        {"hidden mark dropped", QString::fromUtf8("улица"), 0, false, false, {}},
        {"name only: the description is not searched", QStringLiteral("alpha"), 0, true, false, {kAlpha}},
        {"name and description: union by mark id", QStringLiteral("alpha"), 0, true, true, {kAlpha, kBeta}},
        // fewer name hits than maxCount: the description is searched too
        {"maxCount 5", QStringLiteral("alpha"), 5, true, true, {kAlpha, kBeta}},
        // name hits >= maxCount: the description is not searched
        {"maxCount 1", QStringLiteral("alpha"), 1, true, true, {kAlpha}},
        {"description only", QString::fromUtf8("описание"), 0, true, true, {kTown}},
        {"description not asked", QString::fromUtf8("описание"), 0, true, false, {}},
        {"prefix query", QStringLiteral("alp*"), 0, true, false, {kAlpha}},
        {"no hit", QStringLiteral("zzz"), 0, true, true, {}},
    };
    for (const FindRow& row : rows) {
        SCOPED_TRACE(row.label);
        Populated p;
        const auto found = p.db->findMarks(row.search, row.maxCount, row.hidden, row.inDescription);
        EXPECT_EQ(namesOf(found), row.expected);
        EXPECT_EQ(found == nullptr, row.expected.isEmpty());
        EXPECT_EQ(p.notifications, 0);
    }
}
