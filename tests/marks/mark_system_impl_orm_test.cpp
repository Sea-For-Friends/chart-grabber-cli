// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports no legacy test (there is none): u_MarkSystemImplORM.pas, u_MarkSystemImplFactoryORM.pas and the
// SQLite3 part of u_MarkSystemImplFactoryChangeable.pas (docs/specs/marks.md §2.9.10, §2.6 Ruling A.1, ticket
// T-0133). This ticket implements no feature (Features: — in its header).
//
// Everything runs on files created in a QTemporaryDir. Expectations are literals and plain SQL on the file
// (a connection opened after the system is destroyed), never the unit's own caches. The end-to-end tests go
// through the real list, MarkSystemImplChangeable, MarkDbByImpl and MarkCategoryDbByImpl; the others build
// the system through the factory of the list. Synchronisation is by semaphores only: no sleeps.
#include "cg/core/notifier.h"
#include "cg/core/path_config.h"
#include "cg/marks/c_mark_system.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_db_by_impl.h"
#include "cg/marks/mark_db_by_impl.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mark_system_config.h"
#include "cg/marks/mark_system_impl_changeable.h"
#include "cg/marks/mark_system_impl_factory_changeable.h"
#include "cg/marks/mormot/mark_system_impl_config_orm.h"
#include "cg/marks/mormot/mark_system_impl_factory_orm.h"
#include "cg/marks/mormot/mark_system_impl_orm.h"
#include "cg/marks/mormot/marks/mark_db_internal_orm.h"
#include "cg/marks/mormot/system_nocase.h"
#include "cg/marks/sml/mark_id.h"
#include "cg/marks/sml/mark_system_impl_config_sml.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/appearance_of_vector_item.h"
#include "cg/vector/vector_item_subset_builder.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <semaphore>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../corpus_skip.h"
#include "mark_db_impl_orm_helper_test_util.h"
#include "mark_system_rig.h"
#include "mark_test_util.h"

namespace m = cg::marks;
namespace v = cg::vector;
namespace g = cg::geo;
namespace tu = cg::marks::testutil;
namespace sq = cg::storage::sqlite;

namespace {

const QString kTown = QString::fromUtf8("Город");
const QString kStreet = QString::fromUtf8("Улица");
const QString kPointName = QString::fromUtf8("Точка");
const QString kLineName = QString::fromUtf8("Линия");
const QString kPolyName = QString::fromUtf8("Полигон");
const QString kHiddenName = QString::fromUtf8("Скрытая");
const QString kDesc = QString::fromUtf8("Описание");

// The collaborators, the list and the config: hoisted to mark_system_rig.h (shared with
// mark_system_test.cpp).
using tu::makeList;
using tu::ormConfig;
using tu::Parts;

// The factory the list registers for the SQLite3 GUID.
std::shared_ptr<m::IMarkSystemImplFactory> sqliteFactory(const Parts& p) {
    return makeList(p)->get(m::cORMSQLiteMarksDbGUID)->factory();
}

// Plain SQL on a closed file, and the file bytes: hoisted to mark_system_rig.h.
using tu::fileBytes;
using tu::sqlRows;

// The real chain of the application around one database file: list -> MarkSystemImplChangeable (builds on
// its task's thread) -> the two facades. Members are declared in construction order and destroyed in reverse:
// facades first, then the changeable (which drops the system), then the rest.
class Chain {
public:
    Chain(const QString& dir, bool readOnly, const Parts& p) : markFactory(p.markFactory) {
        config = std::make_shared<m::MarkSystemConfig>();
        config->add(m::cORMSQLiteMarksDbGUID, QStringLiteral("My Marks"), ormConfig(readOnly), true);
        basePath = cg::core::PathConfig::create(QString(), dir, nullptr);
        errorSubscription = errors.subscribe([this] { ++errorCount; });
        impl = std::make_shared<m::MarkSystemImplChangeable>(basePath, config, makeList(p), errors,
                                                             appStarted.get_token(), appClosing.get_token());
        auto changeSubscription = impl->changed().subscribe([this] { changes.release(); });
        appStarted.request_stop();
        changes.acquire(); // the old backend dropped
        changes.acquire(); // the new one published, or the open failed
        db = std::make_shared<m::MarkDbByImpl>(impl, markFactory, errors);
        cats = std::make_shared<m::MarkCategoryDbByImpl>(impl, nullptr, errors);
    }
    ~Chain() {
        cats.reset();
        db.reset();
        impl.reset();
    }
    [[nodiscard]] bool opened() const { return impl->getStatic() != nullptr; }

    m::MarkCategoryPtr addCategory(const QString& name, bool visible, int after, int before) {
        return cats->updateCategory(nullptr,
                                    std::make_shared<const m::MarkCategory>(name, visible, after, before));
    }
    v::VectorDataItemPtr addMark(g::GeometryLonLatPtr geometry, const QString& name,
                                 const m::MarkCategoryPtr& category, v::AppearancePtr appearance) {
        return db->updateMark(nullptr, markFactory->createMark(std::move(geometry), name, kDesc, category,
                                                               std::move(appearance)));
    }

    std::shared_ptr<m::IMarkFactory> markFactory;
    cg::core::Notifier errors;
    std::atomic<int> errorCount{0}; // incremented on whichever thread notifies; read by the test
    cg::core::Subscription errorSubscription;
    std::shared_ptr<m::MarkSystemConfig> config;
    cg::core::PathConfigPtr basePath;
    std::stop_source appStarted;
    std::stop_source appClosing;
    std::counting_semaphore<> changes{0};
    std::shared_ptr<m::MarkSystemImplChangeable> impl;
    std::shared_ptr<m::MarkDbByImpl> db;
    std::shared_ptr<m::MarkCategoryDbByImpl> cats;
};

std::vector<g::LonLat> pointsOf(const v::VectorDataItemPtr& mark) {
    const g::GeometryLonLatPtr geometry = mark->geometry();
    if (const auto* line = dynamic_cast<const g::IGeometryLonLatSingleLine*>(geometry.get())) {
        const auto points = line->points();
        return {points.begin(), points.end()};
    }
    if (const auto* poly = dynamic_cast<const g::IGeometryLonLatSinglePolygon*>(geometry.get())) {
        const auto points = poly->outerBorder()->points();
        return {points.begin(), points.end()};
    }
    if (const auto* point = dynamic_cast<const g::IGeometryLonLatPoint*>(geometry.get())) {
        return {point->point()};
    }
    return {};
}

using tu::namesOf;
using tu::sorted;

qint64 markIdOf(const v::VectorDataItemPtr& mark) {
    const auto info = mark->mainInfo();
    const auto* internal = dynamic_cast<const m::IMarkInternalORM*>(info.get());
    return internal != nullptr ? internal->id() : -1;
}

} // namespace

// (a), (b) The persistence proof: add through the facades, destroy everything, build again on the same file.
// The second run sees the same categories, marks, geometry, appearance and visibility, and writes.
TEST(MarkSystemImplORM, PersistenceThroughTheFacadesAcrossARestart) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = QDir(dir.path()).filePath(QStringLiteral("Marks.db3"));
    ASSERT_FALSE(QFile::exists(file));
    const Parts parts;

    {
        Chain chain(dir.path(), false, parts);
        ASSERT_TRUE(chain.opened());
        ASSERT_TRUE(chain.impl->getStatic()->state()->getStatic()->writeAccess);
        const m::MarkCategoryPtr town = chain.addCategory(kTown, true, 5, 10);
        const m::MarkCategoryPtr street = chain.addCategory(kStreet, false, 4, 18);
        ASSERT_TRUE(town);
        ASSERT_TRUE(street);
        ASSERT_TRUE(chain.addMark(tu::makePoint(), kPointName, town,
                                  parts.appearances->createPointAppearance(
                                      0xFF112233U, 0xFF445566U, 13, QStringLiteral("pin"), nullptr, 17)));
        ASSERT_TRUE(chain.addMark(tu::makeLine(), kLineName, town,
                                  parts.appearances->createLineAppearance(0xFF778899U, 5)));
        ASSERT_TRUE(chain.addMark(tu::makePolygon(), kPolyName, town,
                                  parts.appearances->createPolygonAppearance(0xFFAABBCCU, 7, 0xFFDDEEFFU)));
        const v::VectorDataItemPtr hidden = chain.addMark(
            tu::makePoint(), kHiddenName, town,
            parts.appearances->createPointAppearance(0xFF010203U, 0xFF445566U, 9, QString(), nullptr, 4));
        ASSERT_TRUE(hidden);
        chain.db->setMarkVisible(hidden, false);
        EXPECT_EQ(chain.errorCount.load(), 0);
    }

    // The file as the first run left it: plain SQL.
    EXPECT_FALSE(QFile::exists(file + QStringLiteral("-wal"))); // the last connection checkpointed and closed
    EXPECT_EQ(sqlRows(file, "SELECT ID,cName FROM Category ORDER BY ID"),
              (QStringList{QStringLiteral("1|") + kTown, QStringLiteral("2|") + kStreet}));
    EXPECT_EQ(
        sqlRows(file,
                "SELECT cvCategory,cvVisible,cvMinZoom,cvMaxZoom FROM CategoryView ORDER BY cvCategory"),
        (QStringList{QStringLiteral("2|0|4|18")}));
    EXPECT_EQ(sqlRows(file, "SELECT ID,mCategory,mName FROM Mark ORDER BY ID"),
              (QStringList{QStringLiteral("1|1|") + kPointName, QStringLiteral("2|1|") + kLineName,
                           QStringLiteral("3|1|") + kPolyName, QStringLiteral("4|1|") + kHiddenName}));
    // a view row exists only for a hidden mark / category (LC-16); the visible category's zooms 5/10 are not
    // stored (L-209, shown after the restart below as 3/23), the hidden one's 4/18 are
    EXPECT_EQ(sqlRows(file, "SELECT mvMark,mvVisible FROM MarkView ORDER BY mvMark"),
              (QStringList{QStringLiteral("4|0")}));
    EXPECT_EQ(sqlRows(file, "SELECT maColor1,maColor2,maScale1,maScale2 FROM MarkAppearance ORDER BY ID"),
              (QStringList{QStringLiteral("4279312947|4282668390|13|17"), QStringLiteral("4286023833|0|5|0"),
                           QStringLiteral("4289379276|4292734719|7|0"),
                           QStringLiteral("4278256131|4282668390|9|4")}));

    {
        Chain chain(dir.path(), false, parts);
        ASSERT_TRUE(chain.opened());
        EXPECT_TRUE(chain.impl->getStatic()->state()->getStatic()->writeAccess);

        // categories
        const m::MarkCategoryListPtr categories = chain.cats->categoriesList();
        ASSERT_TRUE(categories);
        EXPECT_EQ(categories->count(), 2);
        const m::MarkCategoryPtr town = chain.cats->firstCategoryByName(kTown);
        const m::MarkCategoryPtr street = chain.cats->firstCategoryByName(kStreet);
        ASSERT_TRUE(town);
        ASSERT_TRUE(street);
        EXPECT_TRUE(town->visible());
        EXPECT_EQ(town->afterScale(), 3);   // L-209: 5 was set, the visible category's zooms are lost
        EXPECT_EQ(town->beforeScale(), 23); // (10 was set)
        EXPECT_FALSE(street->visible());
        EXPECT_EQ(street->afterScale(), 4);
        EXPECT_EQ(street->beforeScale(), 18);

        // marks: hidden included / excluded
        EXPECT_EQ(namesOf(chain.db->markSubsetByCategory(town, true)),
                  sorted(QStringList{kLineName, kPointName, kPolyName, kHiddenName}));
        EXPECT_EQ(namesOf(chain.db->markSubsetByCategory(town, false)),
                  sorted(QStringList{kLineName, kPointName, kPolyName}));
        EXPECT_FALSE(chain.db->markSubsetByCategory(street, true));

        const v::VectorDataItemPtr point = chain.db->firstMarkByName(kPointName, town);
        const v::VectorDataItemPtr line = chain.db->firstMarkByName(kLineName, town);
        const v::VectorDataItemPtr poly = chain.db->firstMarkByName(kPolyName, town);
        const v::VectorDataItemPtr hidden = chain.db->firstMarkByName(kHiddenName, town);
        ASSERT_TRUE(point);
        ASSERT_TRUE(line);
        ASSERT_TRUE(poly);
        ASSERT_TRUE(hidden);
        EXPECT_EQ(point->desc(), kDesc);

        // geometry
        EXPECT_EQ(pointsOf(point), (std::vector<g::LonLat>{{10.5, 20.25}}));
        EXPECT_EQ(pointsOf(line), (std::vector<g::LonLat>{{10.5, 20.25}, {12.5, 22.75}}));
        EXPECT_EQ(pointsOf(poly), (std::vector<g::LonLat>{{1.5, 2.5}, {3.5, 2.5}, {3.5, 4.5}}));

        // appearance
        const auto pointAppearance = point->appearance();
        const auto* caption = dynamic_cast<const v::IAppearancePointCaption*>(pointAppearance.get());
        const auto* icon = dynamic_cast<const v::IAppearancePointIcon*>(pointAppearance.get());
        ASSERT_TRUE(caption != nullptr && icon != nullptr);
        EXPECT_EQ(caption->textColor(), 0xFF112233U);
        EXPECT_EQ(caption->textBgColor(), 0xFF445566U);
        EXPECT_EQ(caption->fontSize(), 13);
        EXPECT_EQ(icon->markerSize(), 17);
        EXPECT_EQ(icon->picName(), QStringLiteral("pin"));
        const auto lineAppearance = line->appearance();
        const auto* lineFacet = dynamic_cast<const v::IAppearanceLine*>(lineAppearance.get());
        ASSERT_TRUE(lineFacet != nullptr);
        EXPECT_EQ(lineFacet->lineColor(), 0xFF778899U);
        EXPECT_EQ(lineFacet->lineWidth(), 5);
        const auto polyAppearance = poly->appearance();
        const auto* border = dynamic_cast<const v::IAppearancePolygonBorder*>(polyAppearance.get());
        const auto* fill = dynamic_cast<const v::IAppearancePolygonFill*>(polyAppearance.get());
        ASSERT_TRUE(border != nullptr && fill != nullptr);
        EXPECT_EQ(border->lineColor(), 0xFFAABBCCU);
        EXPECT_EQ(border->lineWidth(), 7);
        EXPECT_EQ(fill->fillColor(), 0xFFDDEEFFU);

        // visibility
        EXPECT_TRUE(chain.db->markVisible(point));
        EXPECT_TRUE(chain.db->markVisible(line));
        EXPECT_TRUE(chain.db->markVisible(poly));
        EXPECT_FALSE(chain.db->markVisible(hidden));

        // (b) the reopened file is writable: a further add succeeds, a further hide sticks
        const v::VectorDataItemPtr added =
            chain.addMark(tu::makePoint(), kTown + QStringLiteral("2"), town,
                          parts.appearances->createLineAppearance(0xFF778899U, 5));
        EXPECT_EQ(chain.errorCount.load(), 0);
        ASSERT_TRUE(added);
        EXPECT_EQ(markIdOf(added), 5);
        chain.db->setMarkVisible(line, false);
    }
    EXPECT_EQ(sqlRows(file, "SELECT ID,mName FROM Mark ORDER BY ID").size(), 5);
    EXPECT_EQ(sqlRows(file, "SELECT mvMark,mvVisible FROM MarkView ORDER BY mvMark"),
              (QStringList{QStringLiteral("2|0"), QStringLiteral("4|0")}));
}

// (c) A read-only impl config: no write access, every write refused, the bytes of the file unchanged.
TEST(MarkSystemImplORM, ReadOnlyConfigRefusesEveryWriteAndLeavesTheFileUnchanged) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = QDir(dir.path()).filePath(QStringLiteral("Marks.db3"));
    const Parts parts;
    {
        Chain chain(dir.path(), false, parts);
        ASSERT_TRUE(chain.opened());
        const m::MarkCategoryPtr town = chain.addCategory(kTown, true, 3, 23);
        ASSERT_TRUE(chain.addMark(tu::makePoint(), kPointName, town,
                                  parts.appearances->createLineAppearance(0xFF778899U, 5)));
    }
    const QByteArray before = fileBytes(file);
    ASSERT_FALSE(before.isEmpty());

    {
        Chain chain(dir.path(), true, parts);
        ASSERT_TRUE(chain.opened());
        const auto state = chain.impl->getStatic()->state()->getStatic();
        EXPECT_TRUE(state->readAccess);
        EXPECT_FALSE(state->writeAccess);

        const m::MarkCategoryPtr town = chain.cats->firstCategoryByName(kTown);
        ASSERT_TRUE(town); // reads work
        const v::VectorDataItemPtr point = chain.db->firstMarkByName(kPointName, town);
        ASSERT_TRUE(point);

        // writes: a new mark, a new category, a visibility change, a category change
        const v::VectorDataItemPtr newMark = chain.markFactory->createMark(
            tu::makeLine(), kLineName, kDesc, town, parts.appearances->createLineAppearance(0xFF778899U, 5));
        EXPECT_FALSE(chain.db->updateMark(nullptr, newMark));
        EXPECT_FALSE(chain.addCategory(kStreet, true, 3, 23));
        chain.db->setMarkVisible(point, false);
        chain.cats->setAllCategoriesVisible(false);
        EXPECT_EQ(chain.errorCount.load(), 0);
    }
    EXPECT_EQ(fileBytes(file), before);
    EXPECT_FALSE(QFile::exists(file + QStringLiteral("-wal")));
    EXPECT_EQ(sqlRows(file, "SELECT ID,cName FROM Category ORDER BY ID"),
              (QStringList{QStringLiteral("1|") + kTown}));
    EXPECT_TRUE(sqlRows(file, "SELECT mvMark,mvVisible FROM MarkView ORDER BY mvMark").isEmpty());
}

// A read-only config on a file that does not exist: the provider's exception leaves the constructor and so
// build (:148-153); no file is created.
TEST(MarkSystemImplORM, ReadOnlyConfigOnAMissingFileThrowsOutOfBuild) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const Parts parts;
    const auto factory = sqliteFactory(parts);
    try {
        std::ignore = factory->build(std::stop_token{}, dir.path(), ormConfig(true));
        FAIL() << "build did not throw";
    } catch (const m::MarkSystemORMError& e) {
        EXPECT_NE(std::string(e.what()).find("File does not exists"), std::string::npos) << e.what();
    }
    EXPECT_FALSE(QFile::exists(QDir(dir.path()).filePath(QStringLiteral("Marks.db3"))));
}

// (d) The string ids, and the identity (dbId) the marks carry.
TEST(MarkSystemImplORM, StringIds) {
    QTemporaryDir dirA;
    QTemporaryDir dirB;
    ASSERT_TRUE(dirA.isValid() && dirB.isValid());
    const Parts parts;
    const auto factory = sqliteFactory(parts);
    const auto systemA = factory->build(std::stop_token{}, dirA.path(), ormConfig(false));
    const auto systemB = factory->build(std::stop_token{}, dirB.path(), ormConfig(false));
    ASSERT_TRUE(systemA && systemB);

    const auto addMark = [&](m::IMarkSystemImpl& system, const QString& name) {
        m::MarkCategoryPtr cat = system.categoryDB()->firstCategoryByName(kTown);
        if (!cat) {
            cat = system.categoryDB()->updateCategory(
                nullptr, std::make_shared<const m::MarkCategory>(kTown, true, 3, 23));
        }
        return system.markDb()->updateMark(
            nullptr, parts.markFactory->createMark(tu::makePoint(), name, kDesc, cat,
                                                   parts.appearances->createLineAppearance(0xFF778899U, 5)));
    };
    const v::VectorDataItemPtr first = addMark(*systemA, kPointName);
    const v::VectorDataItemPtr second = addMark(*systemA, kLineName);
    const v::VectorDataItemPtr other = addMark(*systemB, kPolyName);
    ASSERT_TRUE(first && second && other);

    // stringIdByMark: the decimal id (:256-264)
    EXPECT_EQ(systemA->stringIdByMark(first), QStringLiteral("1"));
    EXPECT_EQ(systemA->stringIdByMark(second), QStringLiteral("2"));
    EXPECT_EQ(systemA->stringIdByMark(nullptr), QString());
    // a mark whose main info is not an IMarkInternalORM: a new mark, and a mark of the SML backend
    const m::MarkCategoryPtr cat = systemA->categoryDB()->firstCategoryByName(kTown);
    EXPECT_EQ(systemA->stringIdByMark(parts.markFactory->createMark(
                  tu::makePoint(), kPointName, kDesc, cat, parts.appearances->createLineAppearance(1, 1))),
              QString());
    const auto smlInfo = std::make_shared<const m::MarkId>(
        tu::converter(), 0, m::MarkIdType::Point, kPointName, kDesc, 41, 1, 1, tu::smlCategory(1), true);
    EXPECT_EQ(systemA->stringIdByMark(tu::markWithInfo(smlInfo)), QString());
    // legacy tests only Supports(MainInfo, IMarkInternalORM): a mark of ANOTHER SQLite database gives its id
    EXPECT_EQ(systemA->stringIdByMark(other), QStringLiteral("1"));

    // the dbId is the address of the system (:138), a different one per instance
    const auto dbIdOf = [](const v::VectorDataItemPtr& mark) {
        const auto info = mark->mainInfo();
        return dynamic_cast<const m::IMarkInternalORM*>(info.get())->dbId();
    };
    EXPECT_EQ(dbIdOf(first), reinterpret_cast<quintptr>(dynamic_cast<const void*>(systemA.get())));
    EXPECT_EQ(dbIdOf(other), reinterpret_cast<quintptr>(dynamic_cast<const void*>(systemB.get())));
    EXPECT_NE(dbIdOf(first), dbIdOf(other));

    // markByStringId: empty id, not a number, unknown id -> null; a number -> the internal getById
    const v::VectorDataItemPtr found = systemA->markByStringId(QStringLiteral("2"));
    ASSERT_TRUE(found);
    EXPECT_EQ(found->name(), kLineName);
    EXPECT_EQ(markIdOf(found), 2);
    EXPECT_FALSE(systemA->markByStringId(QString()));
    EXPECT_FALSE(systemA->markByStringId(QStringLiteral("abc")));
    EXPECT_FALSE(systemA->markByStringId(QStringLiteral("2x")));
    EXPECT_FALSE(systemA->markByStringId(QStringLiteral("2 ")));
    EXPECT_FALSE(systemA->markByStringId(QStringLiteral("999")));
    EXPECT_FALSE(systemA->markByStringId(QStringLiteral("9223372036854775808"))); // does not fit Int64
    EXPECT_FALSE(systemA->markByStringId(QStringLiteral("-1")));
    // blank and hex spellings: unverified, by analogy (delphiTryStrToInt64, RTL not in the legacy tree)
    ASSERT_TRUE(systemA->markByStringId(QStringLiteral(" 2")));
    EXPECT_EQ(systemA->markByStringId(QStringLiteral("$2"))->name(), kLineName);
    EXPECT_EQ(systemA->markByStringId(QStringLiteral("0x1"))->name(), kPointName);

    // markCategoryByStringId: the same rules over the category database
    const m::MarkCategoryPtr byId = systemA->markCategoryByStringId(QStringLiteral("1"));
    ASSERT_TRUE(byId);
    EXPECT_EQ(byId->name(), kTown);
    EXPECT_FALSE(systemA->markCategoryByStringId(QString()));
    EXPECT_FALSE(systemA->markCategoryByStringId(QStringLiteral("abc")));
    EXPECT_FALSE(systemA->markCategoryByStringId(QStringLiteral("5")));
    // a category id is not a mark id: mark 2 exists, category 2 does not
    EXPECT_FALSE(systemA->markCategoryByStringId(QStringLiteral("2")));
}

// (e) Wrong impl-config class: the legacy text, thrown before the provider touches any file (:140-142).
TEST(MarkSystemImplORM, WrongImplConfigThrowsTheLegacyTextAndCreatesNoFile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const Parts parts;
    const auto factory = sqliteFactory(parts);
    const m::MarkSystemImplConfigStaticPtr configs[] = {
        std::make_shared<m::MarkSystemImplConfigSML>(QStringLiteral("Marks.sml"), false), nullptr};
    for (const auto& config : configs) {
        try {
            std::ignore = factory->build(std::stop_token{}, dir.path(), config);
            FAIL() << "build did not throw";
        } catch (const m::MarkSystemORMError& e) {
            EXPECT_STREQ(e.what(), "MarkSystemORM: Unknown Impl config interface!");
        }
    }
    EXPECT_TRUE(QDir(dir.path()).isEmpty());
}

// (g) The list: the SQLite3 GUID alone; its element has the caption and a factory that needs initialisation.
TEST(MarkSystemImplFactoryListStatic, HoldsTheSqliteBackendAlone) {
    const Parts parts;
    const auto list = makeList(parts);
    EXPECT_EQ(list->guidEnum(), (std::vector<QUuid>{m::cORMSQLiteMarksDbGUID}));
    EXPECT_EQ(list->guidEnum().front().toString(QUuid::WithoutBraces),
              QStringLiteral("776bc34b-7a4b-4237-be9a-d905228fda1a"));

    const auto element = list->get(m::cORMSQLiteMarksDbGUID);
    ASSERT_TRUE(element);
    EXPECT_EQ(element->guid(), m::cORMSQLiteMarksDbGUID);
    EXPECT_EQ(element->caption(), QStringLiteral("SQLite3"));
    ASSERT_TRUE(element->factory());
    EXPECT_TRUE(element->factory()->isInitializationRequired());

    EXPECT_FALSE(list->get(m::cSMLMarksDbGUID)); // until S6 registers it
    EXPECT_FALSE(list->get(m::cORMMongoDbMarksDbGUID));
    EXPECT_FALSE(list->get(m::cORMODBCMarksDbGUID));
    EXPECT_FALSE(list->get(QUuid()));
}

// (h) A copy of the fixture written by Windows SAS.Planet opens writable, lists nothing, accepts an add; the
// file stays sound.
TEST(MarkSystemImplORM, FixtureCopyOpensWritableAndAcceptsAnAdd) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = QDir(dir.path()).filePath(QStringLiteral("Marks.db3"));
    ASSERT_TRUE(QFile::copy(cgMarksFixture(QStringLiteral("marks/Marks.db3")), file));
    ASSERT_TRUE(QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner));
    const Parts parts;
    {
        const auto system = sqliteFactory(parts)->build(std::stop_token{}, dir.path(), ormConfig(false));
        ASSERT_TRUE(system);
        EXPECT_TRUE(system->state()->getStatic()->readAccess);
        EXPECT_TRUE(system->state()->getStatic()->writeAccess);
        EXPECT_FALSE(system->categoryDB()->categoriesList());
        const m::MarkIdList ids = system->markDb()->allMarkIdList();
        EXPECT_TRUE(!ids || ids->empty());
        const m::MarkCategoryPtr town = system->categoryDB()->updateCategory(
            nullptr, std::make_shared<const m::MarkCategory>(kTown, true, 3, 23));
        ASSERT_TRUE(town);
        const v::VectorDataItemPtr mark = system->markDb()->updateMark(
            nullptr, parts.markFactory->createMark(tu::makePoint(), kPointName, kDesc, town,
                                                   parts.appearances->createLineAppearance(0xFF778899U, 5)));
        ASSERT_TRUE(mark);
        EXPECT_EQ(system->stringIdByMark(mark), QStringLiteral("1"));
    }
    EXPECT_EQ(sqlRows(file, "PRAGMA integrity_check"), (QStringList{QStringLiteral("ok")}));
    EXPECT_EQ(sqlRows(file, "SELECT ID,cName FROM Category"), (QStringList{QStringLiteral("1|") + kTown}));
    EXPECT_EQ(sqlRows(file, "SELECT ID,mName FROM Mark"), (QStringList{QStringLiteral("1|") + kPointName}));
    EXPECT_EQ(sqlRows(file, "SELECT ID,uName FROM User"), (QStringList{QStringLiteral("1|sasgis")}));
}
