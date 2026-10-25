// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// FEAT-GEO-034
// Ports u_MarkSystem.pas (TMarkSystem, :118-319); ticket T-0137, spec docs/specs/marks.md §2.6 (MarkSystem
// paragraph, Rulings A.1-A.2) and §2.10. No legacy test exists. Features: FEAT-GEO-034.
//
// Every test runs the real chain on a Marks.db3 that the test creates in a QTemporaryDir: the factory list
// with its SQLite3 factory, MarkSystemImplChangeable (built on its task's thread) and the two facades, all
// behind IMarkSystem. Expectations are literals; after a restart they are plain SQL on the closed file. The
// wait for the publish is one semaphore, released by the state change that the publish makes (the state proxy
// goes from nothing to the built backend). No sleeps.
#include "cg/core/language_manager.h"
#include "cg/core/path_config.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/marks/c_mark_system.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_factory.h"
#include "cg/marks/mark_category_factory_config.h"
#include "cg/marks/mark_category_list.h"
#include "cg/marks/mark_category_tree.h"
#include "cg/marks/mark_system.h"
#include "cg/marks/mark_system_config.h"
#include "cg/vector/import_config.h"
#include "cg/vector/vector_item_subset.h"
#include "cg/vector/vector_item_tree.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <semaphore>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

#include "mark_system_rig.h"
#include "mark_test_util.h"

namespace m = cg::marks;
namespace v = cg::vector;
namespace tu = cg::marks::testutil;

namespace {

using tu::namesOf;
using tu::sorted;
using tu::sqlRows;

const QString kTown = QString::fromUtf8("Город");
const QString kStreet = QString::fromUtf8("Улица");
const QString kPointName = QString::fromUtf8("Точка");
const QString kLineName = QString::fromUtf8("Линия");
const QString kPolyName = QString::fromUtf8("Полигон");
const QString kHiddenName = QString::fromUtf8("Скрытая");
const QString kDesc = QString::fromUtf8("Описание");
// A GUID that no backend is registered under: the factory miss of spec Ruling A.3.
const QUuid kUnknownGuid = QUuid::fromString(QStringLiteral("{01234567-89ab-4cde-8f01-23456789abcd}"));

// The composition root around one MarkSystem on `dir`: the config names one item, `guid`, and the two
// one-shot notifiers are stop sources. Nothing is built until start(). Members are declared in the order they
// are needed: the system, then the subscriptions, so the subscriptions are destroyed first and `published_`
// and `errorCount_` outlive the system (whose destructor joins its build and may still release the
// semaphore).
class Harness {
public:
    Harness(const QString& dir, const QUuid& guid, const tu::Parts& parts) {
        auto config = std::make_shared<m::MarkSystemConfig>();
        config->add(guid, QStringLiteral("My Marks"), tu::ormConfig(false), true);
        const auto categoryConfig = std::make_shared<m::MarkCategoryFactoryConfig>(
            std::make_shared<cg::core::LanguageManager>(languages_.path()));
        system_ = std::make_shared<m::MarkSystem>(
            cg::core::PathConfig::create(QString(), dir, nullptr), config, parts.pictures, parts.markFactory,
            std::make_shared<m::MarkCategoryFactory>(categoryConfig), parts.appearances, tu::kFactory,
            parts.subsets, started_.get_token(), closing_.get_token(), parts.converter);
        stateSubscription_ = system_->state()->changed().subscribe([this] { published_.release(); });
        errorSubscription_ = system_->errorNotifier().subscribe([this] { ++errorCount_; });
    }

    // The application starts: the build runs on the task's thread.
    void start() { started_.request_stop(); }
    // Returns once the build has published its backend (one state change). A build that never publishes fails
    // the test after the bound instead of blocking it.
    [[nodiscard]] bool waitForPublish() { return published_.try_acquire_for(std::chrono::seconds(60)); }
    void open() {
        start();
        ASSERT_TRUE(waitForPublish()) << "the backend was not published";
    }

    [[nodiscard]] m::IMarkSystem& system() { return *system_; }
    [[nodiscard]] int errorCount() const { return errorCount_.load(); }

private:
    std::stop_source started_;
    std::stop_source closing_;
    std::counting_semaphore<> published_{0};
    std::atomic<int> errorCount_{0};
    QTemporaryDir languages_; // the category factory's language root: kept out of the database directory
    std::shared_ptr<m::MarkSystem> system_;
    cg::core::Subscription stateSubscription_;
    cg::core::Subscription errorSubscription_;
};

v::VectorDataItemPtr addPoint(m::IMarkDb& db, const tu::Parts& p, const QString& name,
                              const m::MarkCategoryPtr& category) {
    return db.updateMark(nullptr, p.markFactory->createMark(
                                      tu::makePoint(), name, kDesc, category,
                                      p.appearances->createPointAppearance(
                                          0xFF112233U, 0xFF445566U, 13, QStringLiteral("pin"), nullptr, 17)));
}

v::VectorDataItemPtr addLine(m::IMarkDb& db, const tu::Parts& p, const QString& name,
                             const m::MarkCategoryPtr& category) {
    return db.updateMark(nullptr,
                         p.markFactory->createMark(tu::makeLine(), name, kDesc, category,
                                                   p.appearances->createLineAppearance(0xFF778899U, 5)));
}

m::MarkCategoryPtr addCategory(m::IMarkCategoryDB& db, const QString& name, bool visible, int after,
                               int before) {
    return db.updateCategory(nullptr, std::make_shared<const m::MarkCategory>(name, visible, after, before));
}

// The names of a category list, sorted (the list is in id order).
QStringList categoryNames(const m::MarkCategoryListPtr& list) {
    QStringList names;
    for (int i = 0; list && i < list->count(); ++i) {
        names.append(list->item(i)->name());
    }
    names.sort();
    return names;
}

// The names of a written mark list, in order.
QStringList markNames(const m::MarkList& list) {
    QStringList names;
    if (list) {
        for (const auto& mark : *list) {
            names.append(mark->name());
        }
    }
    return names;
}

v::VectorItemTreePtr node(const QString& name, std::vector<v::VectorDataItemPtr> items,
                          std::vector<v::VectorItemTreePtr> children) {
    v::VectorItemSubsetPtr subset;
    if (!items.empty()) {
        subset = std::make_shared<const v::VectorItemSubset>(0, std::move(items));
    }
    return std::make_shared<const v::VectorItemTree>(name, subset, std::move(children));
}

// The import config of the tests: root "" (a top node's path is its own name), no ignore flags. Each geometry
// type's params carry an appearance, because the imported items have none.
v::ImportConfigPtr importConfig(const tu::Parts& p) {
    return std::make_shared<const v::ImportConfig>(
        std::make_shared<const v::Category>(QString()),
        std::make_shared<const v::ImportCategoryParams>(false, false, false, false),
        std::make_shared<const v::ImportPointParams>(
            p.appearances->createPointAppearance(0xFF112233U, 0xFF445566U, 13, QStringLiteral("pin"), nullptr,
                                                 17),
            false, false, false, false, false),
        std::make_shared<const v::ImportLineParams>(p.appearances->createLineAppearance(0xFF778899U, 5),
                                                    false, false),
        std::make_shared<const v::ImportPolyParams>(
            p.appearances->createPolygonAppearance(0xFFAABBCCU, 7, 0xFFDDEEFFU), false, false, false));
}

m::MarkCategoryTreePtr categoryFolder(const m::MarkCategoryPtr& category, const QString& name,
                                      std::vector<m::MarkCategoryTreePtr> children = {}) {
    return std::make_shared<const m::MarkCategoryTree>(category, name, std::move(children));
}

} // namespace

// The round trip through the real backend: a category and marks are written through IMarkSystem, the system
// is destroyed, the file is read in plain SQL, and a second system on the same file answers with the same
// content.
TEST(MarkSystem, OpenWriteRestartAndReadTheSameDatabase) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = QDir(dir.path()).filePath(QStringLiteral("Marks.db3"));
    const tu::Parts parts;
    {
        Harness h(dir.path(), m::cORMSQLiteMarksDbGUID, parts);
        h.open();
        m::IMarkSystem& sys = h.system();
        const auto state = sys.state()->getStatic();
        EXPECT_TRUE(state->readAccess);
        EXPECT_TRUE(state->writeAccess);
        EXPECT_EQ(sys.implFactoryList()->guidEnum(), (std::vector<QUuid>{m::cORMSQLiteMarksDbGUID}));

        const auto categories = sys.categoryDB();
        const auto markDb = sys.markDb();
        const m::MarkCategoryPtr town = addCategory(*categories, kTown, true, 3, 23);
        const m::MarkCategoryPtr street = addCategory(*categories, kStreet, false, 4, 18);
        ASSERT_TRUE(town && street);
        const v::VectorDataItemPtr point = addPoint(*markDb, parts, kPointName, town);
        const v::VectorDataItemPtr hidden = addLine(*markDb, parts, kHiddenName, town);
        ASSERT_TRUE(point && hidden);
        markDb->setMarkVisible(hidden, false);
        EXPECT_EQ(sys.stringIdByMark(point), QStringLiteral("1"));
        EXPECT_EQ(h.errorCount(), 0);
    }

    // The file as the system left it: plain SQL on the closed file (no -wal: the last connection
    // checkpointed).
    EXPECT_FALSE(QFile::exists(file + QStringLiteral("-wal")));
    EXPECT_EQ(sqlRows(file, "SELECT ID,cName FROM Category ORDER BY ID"),
              (QStringList{QStringLiteral("1|") + kTown, QStringLiteral("2|") + kStreet}));
    EXPECT_EQ(
        sqlRows(file,
                "SELECT cvCategory,cvVisible,cvMinZoom,cvMaxZoom FROM CategoryView ORDER BY cvCategory"),
        (QStringList{QStringLiteral("2|0|4|18")}));
    EXPECT_EQ(sqlRows(file, "SELECT ID,mCategory,mName FROM Mark ORDER BY ID"),
              (QStringList{QStringLiteral("1|1|") + kPointName, QStringLiteral("2|1|") + kHiddenName}));
    EXPECT_EQ(sqlRows(file, "SELECT mvMark,mvVisible FROM MarkView ORDER BY mvMark"),
              (QStringList{QStringLiteral("2|0")}));

    {
        Harness h(dir.path(), m::cORMSQLiteMarksDbGUID, parts);
        h.open();
        m::IMarkSystem& sys = h.system();
        EXPECT_TRUE(sys.state()->getStatic()->writeAccess);
        EXPECT_EQ(categoryNames(sys.categoryDB()->categoriesList()), sorted(QStringList{kTown, kStreet}));

        const m::MarkCategoryPtr town = sys.categoryDB()->firstCategoryByName(kTown);
        const m::MarkCategoryPtr street = sys.categoryDB()->firstCategoryByName(kStreet);
        ASSERT_TRUE(town && street);
        EXPECT_TRUE(town->visible());
        EXPECT_EQ(town->afterScale(), 3);
        EXPECT_EQ(town->beforeScale(), 23);
        EXPECT_FALSE(street->visible());
        EXPECT_EQ(street->afterScale(), 4);
        EXPECT_EQ(street->beforeScale(), 18);

        EXPECT_EQ(namesOf(sys.markDb()->markSubsetByCategory(town, true)),
                  sorted(QStringList{kPointName, kHiddenName}));
        EXPECT_EQ(namesOf(sys.markDb()->markSubsetByCategory(town, false)), sorted(QStringList{kPointName}));
        const v::VectorDataItemPtr point = sys.markDb()->firstMarkByName(kPointName, town);
        ASSERT_TRUE(point);
        EXPECT_EQ(point->desc(), kDesc);
        EXPECT_NE(dynamic_cast<const cg::geo::IGeometryLonLatPoint*>(point->geometry().get()), nullptr);
        EXPECT_TRUE(sys.markDb()->markVisible(point));
        EXPECT_FALSE(sys.markDb()->markVisible(sys.markDb()->firstMarkByName(kHiddenName, town)));

        EXPECT_EQ(sys.stringIdByMark(point), QStringLiteral("1"));
        const v::VectorDataItemPtr byId = sys.markByStringId(QStringLiteral("1"));
        ASSERT_TRUE(byId);
        EXPECT_EQ(byId->name(), kPointName);
        const m::MarkCategoryPtr categoryById = sys.markCategoryByStringId(QStringLiteral("2"));
        ASSERT_TRUE(categoryById);
        EXPECT_EQ(categoryById->name(), kStreet);
        EXPECT_EQ(h.errorCount(), 0);
    }
}

// The string ids round trip through the system, and every empty case answers nothing.
TEST(MarkSystem, StringIdsRoundTripAndTheEmptyCasesAnswerNothing) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const tu::Parts parts;
    Harness h(dir.path(), m::cORMSQLiteMarksDbGUID, parts);
    h.open();
    m::IMarkSystem& sys = h.system();
    const m::MarkCategoryPtr town = addCategory(*sys.categoryDB(), kTown, true, 3, 23);
    const v::VectorDataItemPtr point = addPoint(*sys.markDb(), parts, kPointName, town);
    ASSERT_TRUE(town && point);

    EXPECT_EQ(sys.stringIdByMark(point), QStringLiteral("1"));
    const v::VectorDataItemPtr mark = sys.markByStringId(QStringLiteral("1"));
    ASSERT_TRUE(mark);
    EXPECT_EQ(mark->name(), kPointName);
    const m::MarkCategoryPtr category = sys.markCategoryByStringId(QStringLiteral("1"));
    ASSERT_TRUE(category);
    EXPECT_EQ(category->name(), kTown);

    // an empty id, a nil mark, an id that names nothing
    EXPECT_EQ(sys.stringIdByMark(nullptr), QString());
    EXPECT_FALSE(sys.markByStringId(QString()));
    EXPECT_FALSE(sys.markCategoryByStringId(QString()));
    EXPECT_FALSE(sys.markByStringId(QStringLiteral("2")));
    EXPECT_FALSE(sys.markCategoryByStringId(QStringLiteral("2")));
    EXPECT_FALSE(sys.markByStringId(QStringLiteral("abc")));
    EXPECT_EQ(h.errorCount(), 0);
}

// deleteCategoryWithMarks: the category's marks and its row go; the other category stays whole.
TEST(MarkSystem, DeleteCategoryWithMarksRemovesItsMarksAndItsRowOnly) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const tu::Parts parts;
    Harness h(dir.path(), m::cORMSQLiteMarksDbGUID, parts);
    h.open();
    m::IMarkSystem& sys = h.system();
    const m::MarkCategoryPtr town = addCategory(*sys.categoryDB(), kTown, true, 3, 23);
    const m::MarkCategoryPtr street = addCategory(*sys.categoryDB(), kStreet, true, 3, 23);
    ASSERT_TRUE(town && street);
    ASSERT_TRUE(addPoint(*sys.markDb(), parts, kPointName, town));
    ASSERT_TRUE(addLine(*sys.markDb(), parts, kLineName, town));
    ASSERT_TRUE(addPoint(*sys.markDb(), parts, kHiddenName, street));

    sys.deleteCategoryWithMarks(town);

    EXPECT_EQ(categoryNames(sys.categoryDB()->categoriesList()), sorted(QStringList{kStreet}));
    EXPECT_EQ(namesOf(sys.markDb()->markSubsetByCategory(street, true)), sorted(QStringList{kHiddenName}));
    const m::MarkIdList ids = sys.markDb()->allMarkIdList();
    ASSERT_TRUE(ids);
    EXPECT_EQ(ids->size(), 1U);
    EXPECT_EQ(h.errorCount(), 0);
}

// deleteCategoryListWithMarks: every listed category goes with its marks; a category outside the list keeps
// its mark.
TEST(MarkSystem, DeleteCategoryListWithMarksRemovesEveryListedCategoryAndItsMarks) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const tu::Parts parts;
    Harness h(dir.path(), m::cORMSQLiteMarksDbGUID, parts);
    h.open();
    m::IMarkSystem& sys = h.system();
    const m::MarkCategoryPtr town = addCategory(*sys.categoryDB(), kTown, true, 3, 23);
    const m::MarkCategoryPtr street = addCategory(*sys.categoryDB(), kStreet, true, 3, 23);
    const m::MarkCategoryPtr polygon = addCategory(*sys.categoryDB(), kPolyName, true, 3, 23);
    ASSERT_TRUE(town && street && polygon);
    ASSERT_TRUE(addPoint(*sys.markDb(), parts, kPointName, town));
    ASSERT_TRUE(addLine(*sys.markDb(), parts, kLineName, street));
    ASSERT_TRUE(addLine(*sys.markDb(), parts, kHiddenName, street));
    ASSERT_TRUE(addPoint(*sys.markDb(), parts, kPolyName, polygon));

    sys.deleteCategoryListWithMarks(m::MarkCategoryList::build({town, street}));

    EXPECT_EQ(categoryNames(sys.categoryDB()->categoriesList()), sorted(QStringList{kPolyName}));
    EXPECT_EQ(namesOf(sys.markDb()->markSubsetByCategory(polygon, true)), sorted(QStringList{kPolyName}));
    const m::MarkIdList ids = sys.markDb()->allMarkIdList();
    ASSERT_TRUE(ids);
    EXPECT_EQ(ids->size(), 1U);
    EXPECT_EQ(h.errorCount(), 0);
}

// importItemsTree: the categories the tree names are written, then its marks; both come back in tree order.
TEST(MarkSystem, ImportItemsTreeWritesTheCategoriesThenTheMarksAndReturnsThem) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const tu::Parts parts;
    Harness h(dir.path(), m::cORMSQLiteMarksDbGUID, parts);
    h.open();
    m::IMarkSystem& sys = h.system();
    const v::VectorItemTreePtr tree =
        node(QString(), {},
             {node(kTown, {tu::dataItem(kPointName, tu::point())}, {}),
              node(kStreet, {tu::dataItem(kLineName, tu::lineGeometry())}, {})});

    const m::MarkList written = sys.importItemsTree(tree, importConfig(parts));

    ASSERT_TRUE(written);
    EXPECT_EQ(markNames(written), (QStringList{kPointName, kLineName}));
    EXPECT_EQ(categoryNames(sys.categoryDB()->categoriesList()), sorted(QStringList{kTown, kStreet}));
    const m::MarkCategoryPtr town = sys.categoryDB()->firstCategoryByName(kTown);
    const m::MarkCategoryPtr street = sys.categoryDB()->firstCategoryByName(kStreet);
    ASSERT_TRUE(town && street);
    EXPECT_EQ(namesOf(sys.markDb()->markSubsetByCategory(town, true)), sorted(QStringList{kPointName}));
    EXPECT_EQ(namesOf(sys.markDb()->markSubsetByCategory(street, true)), sorted(QStringList{kLineName}));
    EXPECT_EQ(h.errorCount(), 0);
}

// A nil tree is refused by the helper before any write (legacy's first unguarded read of the tree is at
// u_MarkSystemHelpers.pas:106), and the exception leaves MarkSystem: the system has no try around the import
// (u_MarkSystem.pas:266-288). Nothing is written.
TEST(MarkSystem, ImportItemsTreeOfANullTreeThrowsOutOfTheSystem) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const tu::Parts parts;
    Harness h(dir.path(), m::cORMSQLiteMarksDbGUID, parts);
    h.open();
    m::IMarkSystem& sys = h.system();

    EXPECT_THROW(sys.importItemsTree(nullptr, importConfig(parts)), std::invalid_argument);

    EXPECT_FALSE(sys.categoryDB()->categoriesList());
    EXPECT_EQ(h.errorCount(), 0);
}

// categoryTreeToMarkTree: the folders keep their shape; the marks of each folder come from its category;
// hidden marks come only on request; a nil tree gives nothing.
TEST(MarkSystem, CategoryTreeToMarkTreeKeepsTheFoldersAndHidesHiddenMarksOnRequest) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const tu::Parts parts;
    Harness h(dir.path(), m::cORMSQLiteMarksDbGUID, parts);
    h.open();
    m::IMarkSystem& sys = h.system();
    const m::MarkCategoryPtr town = addCategory(*sys.categoryDB(), kTown, true, 3, 23);
    const m::MarkCategoryPtr street = addCategory(*sys.categoryDB(), kStreet, true, 3, 23);
    ASSERT_TRUE(town && street);
    ASSERT_TRUE(addPoint(*sys.markDb(), parts, kPointName, town));
    const v::VectorDataItemPtr hidden = addLine(*sys.markDb(), parts, kHiddenName, town);
    ASSERT_TRUE(hidden);
    sys.markDb()->setMarkVisible(hidden, false);
    ASSERT_TRUE(addLine(*sys.markDb(), parts, kLineName, street));
    const m::MarkCategoryTreePtr tree =
        categoryFolder(nullptr, QString(), {categoryFolder(town, kTown), categoryFolder(street, kStreet)});

    const v::VectorItemTreePtr shown = sys.categoryTreeToMarkTree(tree, false);
    ASSERT_TRUE(shown);
    EXPECT_EQ(shown->name(), QString());
    EXPECT_FALSE(shown->items()); // the top folder has no category, so no marks
    ASSERT_EQ(shown->subTreeItemCount(), 2);
    EXPECT_EQ(shown->subTreeItem(0)->name(), kTown);
    EXPECT_EQ(namesOf(shown->subTreeItem(0)->items()), sorted(QStringList{kPointName}));
    EXPECT_EQ(shown->subTreeItem(1)->name(), kStreet);
    EXPECT_EQ(namesOf(shown->subTreeItem(1)->items()), sorted(QStringList{kLineName}));

    const v::VectorItemTreePtr all = sys.categoryTreeToMarkTree(tree, true);
    ASSERT_TRUE(all);
    ASSERT_EQ(all->subTreeItemCount(), 2);
    EXPECT_EQ(namesOf(all->subTreeItem(0)->items()), sorted(QStringList{kPointName, kHiddenName}));

    EXPECT_FALSE(sys.categoryTreeToMarkTree(nullptr, true));
    EXPECT_EQ(h.errorCount(), 0);
}

// While the static is null (a config naming a GUID nobody registers, a factory miss, spec Ruling A.3), every
// method answers its empty value. The build is started: it throws on the task's thread, the static stays null
// before, during and after it, so no wait is needed; the destructor joins the build.
TEST(MarkSystem, WhileNoBackendIsPublishedEveryMethodAnswersItsEmptyValue) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const tu::Parts parts;
    Harness h(dir.path(), kUnknownGuid, parts);
    h.start();
    m::IMarkSystem& sys = h.system();
    const m::MarkCategoryPtr town = std::make_shared<const m::MarkCategory>(kTown, true, 3, 23);
    const v::VectorDataItemPtr point = tu::dataItem(kPointName, tu::point());
    const v::VectorItemTreePtr tree = node(kTown, {point}, {});
    const m::MarkCategoryTreePtr folder = categoryFolder(town, kTown);
    const v::ImportConfigPtr config = importConfig(parts);

    EXPECT_EQ(sys.stringIdByMark(point), QString());
    EXPECT_FALSE(sys.markByStringId(QStringLiteral("1")));
    EXPECT_FALSE(sys.markCategoryByStringId(QStringLiteral("1")));
    EXPECT_FALSE(sys.importItemsTree(tree, config));
    // the static is tested before the tree: a nil tree gives nothing here and does not throw
    EXPECT_FALSE(sys.importItemsTree(nullptr, config));
    EXPECT_FALSE(sys.categoryTreeToMarkTree(folder, true));
    EXPECT_FALSE(sys.categoryTreeToMarkTree(nullptr, true));
    sys.deleteCategoryWithMarks(town);
    sys.deleteCategoryListWithMarks(m::MarkCategoryList::build({town}));

    EXPECT_FALSE(sys.state()->getStatic()->readAccess);
    EXPECT_EQ(h.errorCount(), 0);
}

// D-57 widened (L-row of T-0137): destroying the system while its build is inside a notification of a facade
// waits for the build. The test's slot on the category facade blocks the task's thread inside the facade's
// notifier; another thread lets it go while this one destroys the system. Without the join the facades are
// freed under the running notification (TSan, ASan).
TEST(MarkSystem, DestroyingTheSystemWhileItsBuildNotifiesAFacadeWaitsForTheBuild) {
    for (int round = 0; round < 50; ++round) {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const tu::Parts parts;
        std::binary_semaphore inSlot{0};
        std::binary_semaphore go{0};
        std::atomic<bool> blocked{false}; // only the first notification blocks
        auto h = std::make_unique<Harness>(dir.path(), m::cORMSQLiteMarksDbGUID, parts);
        cg::core::Subscription slot = h->system().categoryDB()->changeNotifier().subscribe([&] {
            if (blocked.exchange(true)) {
                return;
            }
            inSlot.release();
            (void)go.try_acquire_for(std::chrono::seconds(60));
        });
        h->start();
        ASSERT_TRUE(inSlot.try_acquire_for(std::chrono::seconds(60))) << "the build did not notify";
        std::thread releaser([&] { go.release(); });
        h.reset();
        releaser.join();
        slot.reset();
    }
}

// The same, from the other end: the system is destroyed the moment the publish is observed, which happens
// inside the changeable's write lock, before its own notification (mark_system_impl_changeable.cpp).
TEST(MarkSystem, DestroyingTheSystemRightAfterThePublishWaitsForTheBuild) {
    for (int round = 0; round < 50; ++round) {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const tu::Parts parts;
        auto h = std::make_unique<Harness>(dir.path(), m::cORMSQLiteMarksDbGUID, parts);
        h->start();
        ASSERT_TRUE(h->waitForPublish());
        h.reset();
    }
}
