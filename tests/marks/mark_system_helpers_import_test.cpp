// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkSystemHelpers.pas:213-301 (ImportItemsTree with PrepareCategoriesFromTreeForImport and
// PrepareFromTreeForImport; CategoryTreeToMarkTreeHelper). No FEAT id: ticket T-0106 has Features: —.
// The fakes record every database and factory call in order; each expected log is what the Pascal makes,
// derived from the line numbers in the comments. Names with Cyrillic letters are compared as well.
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_tree.h"
#include "cg/marks/mark_system_helpers.h"
#include "cg/vector/category.h"
#include "cg/vector/import_config.h"
#include "cg/vector/vector_item_subset.h"
#include "cg/vector/vector_item_tree.h"

#include <QString>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mark_system_fakes.h"
#include "mark_test_util.h"

namespace m = cg::marks;
namespace v = cg::vector;
namespace t = cg::marks::testutil;

namespace {

// The databases and factories of one import, with one call log for all of them (a rig is never copied).
struct Rig {
    t::CallLog log;
    std::shared_ptr<t::FakeCategoryDbImpl> categoryDb = std::make_shared<t::FakeCategoryDbImpl>();
    std::shared_ptr<t::FakeDb> markDb = std::make_shared<t::FakeDb>();
    t::FakeMarkFactory markFactory;
    t::FakeCategoryFactory categoryFactory;

    Rig() {
        categoryDb->log = &log;
        categoryDb->storesLists = true; // the real backend stores the list it is given
        markDb->log = &log;
        markDb->byName = true; // firstMarkByName and markSubsetByCategory answer by name
        markFactory.log = &log;
    }

    m::MarkList importTree(const v::VectorItemTreePtr& tree, const v::ImportConfigPtr& config) {
        return m::importItemsTree(tree, config, *markDb, markFactory, *categoryDb, categoryFactory);
    }
};

// What makeConfig sets or leaves out.
struct ConfigParts {
    QString rootName =
        QStringLiteral("Root");  // RootCategory name; "" is the exporter's (u_VectorItemTreeMarksDb.pas:120)
    bool ignoreSameName = false; // IsIgnoreMarkIfExistsWithSameNameInCategory
    bool point = true;           // PointParams given
    bool line = true;            // LineParams given
    bool poly = true;            // PolyParams given
    bool nilRoot = false;        // RootCategory is nil
    bool nilCategoryParams = false; // CategoryParams is nil
};

// The import config of the tests. Params have a null appearance (null facets, no throw).
v::ImportConfigPtr makeConfig(const ConfigParts& parts = {}) {
    v::CategoryPtr root;
    if (!parts.nilRoot) {
        root = std::make_shared<const v::Category>(parts.rootName);
    }
    v::ImportCategoryParamsPtr categoryParams;
    if (!parts.nilCategoryParams) {
        categoryParams =
            std::make_shared<const v::ImportCategoryParams>(false, true, false, parts.ignoreSameName);
    }
    v::ImportPointParamsPtr point;
    if (parts.point) {
        point = std::make_shared<const v::ImportPointParams>(nullptr, false, false, false, false, false);
    }
    v::ImportLineParamsPtr line;
    if (parts.line) {
        line = std::make_shared<const v::ImportLineParams>(nullptr, false, false);
    }
    v::ImportPolyParamsPtr poly;
    if (parts.poly) {
        poly = std::make_shared<const v::ImportPolyParams>(nullptr, false, false, false);
    }
    return std::make_shared<const v::ImportConfig>(root, categoryParams, point, line, poly);
}

// A tree node: its name, its items (no items: a nil subset, as legacy Items = nil) and its children.
v::VectorItemTreePtr node(const QString& name, std::vector<v::VectorDataItemPtr> items = {},
                          std::vector<v::VectorItemTreePtr> children = {}) {
    v::VectorItemSubsetPtr subset;
    if (!items.empty()) {
        subset = std::make_shared<const v::VectorItemSubset>(0, std::move(items));
    }
    return std::make_shared<const v::VectorItemTree>(name, subset, std::move(children));
}

v::VectorDataItemPtr pt(const QString& name) {
    return t::dataItem(name, t::point());
}
v::VectorDataItemPtr ln(const QString& name) {
    return t::dataItem(name, t::lineGeometry());
}
v::VectorDataItemPtr pg(const QString& name) {
    return t::dataItem(name, t::polygonGeometry());
}
v::VectorDataItemPtr mp(const QString& name) {
    return t::dataItem(name, t::multiPointGeometry());
}

m::MarkCategoryPtr category(const QString& name) {
    return std::make_shared<const m::MarkCategory>(name, true, 0, 24);
}

// Names of the marks of a written list, in order.
std::vector<QString> namesOf(const m::MarkList& list) {
    std::vector<QString> names;
    if (list) {
        for (const auto& mark : *list) {
            names.push_back(mark->name());
        }
    }
    return names;
}

// ImportItemsTree: categories and marks.

TEST(ImportItemsTree, ListsMissingCategoriesThenWritesMarksLast) {
    Rig rig;
    const v::VectorItemTreePtr tree = node(QStringLiteral("T"), {pt(QStringLiteral("a"))},
                                           {node(QStringLiteral("C"), {pt(QStringLiteral("b"))}),
                                            node(QStringLiteral("D"), {pt(QStringLiteral("d"))})});

    const m::MarkList result = rig.importTree(tree, makeConfig());

    // Categories phase (:86-106): lookups and creation, children in order; one updateCategoryList (:235-237).
    // Marks phase (:140-199): the node's category, its items, then each child in order; one updateMarkList
    // (:252-253).
    EXPECT_EQ(rig.log, (t::CallLog{"cat.first Root\\T", "cat.first Root\\T\\C", "cat.first Root\\T\\D",
                                   "cat.list Root\\T", "cat.list Root\\T\\C", "cat.list Root\\T\\D",
                                   "cat.first Root\\T", "factory.prepare a point @Root\\T",
                                   "cat.first Root\\T\\C", "factory.prepare b point @Root\\T\\C",
                                   "cat.first Root\\T\\D", "factory.prepare d point @Root\\T\\D",
                                   "mark.write a", "mark.write b", "mark.write d"}));
    EXPECT_EQ(rig.categoryDb->listed,
              (std::vector<QString>{QStringLiteral("Root\\T"), QStringLiteral("Root\\T\\C"),
                                    QStringLiteral("Root\\T\\D")}));
    EXPECT_EQ(namesOf(result),
              (std::vector<QString>{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("d")}));
}

TEST(ImportItemsTree, ExistingCategoryIsNotListedAgain) {
    Rig rig;
    rig.categoryDb->categories.push_back(category(QStringLiteral("Root\\T")));

    const m::MarkList result =
        rig.importTree(node(QStringLiteral("T"), {pt(QStringLiteral("a"))}), makeConfig());

    // The category is found in both phases (:99, :153), so no category is listed or created.
    EXPECT_EQ(rig.log, (t::CallLog{"cat.first Root\\T", "cat.first Root\\T",
                                   "factory.prepare a point @Root\\T", "mark.write a"}));
    EXPECT_EQ(rig.categoryDb->listCalls, 0);
    EXPECT_EQ(namesOf(result), (std::vector<QString>{QStringLiteral("a")}));
}

TEST(ImportItemsTree, MarksPhaseCreatesAStillMissingCategoryAtOnce) {
    // :153-156: when the lookup of the marks phase fails (a backend that did not keep the list), the category
    // is created and written at once by updateCategory, and the item takes the category it returns.
    Rig rig;
    rig.categoryDb->storesLists = false;

    const m::MarkList result =
        rig.importTree(node(QStringLiteral("T"), {pt(QStringLiteral("a"))}), makeConfig());

    EXPECT_EQ(rig.log,
              (t::CallLog{"cat.first Root\\T", "cat.list Root\\T", "cat.first Root\\T", "cat.update Root\\T",
                          "factory.prepare a point @Root\\T", "mark.write a"}));
    EXPECT_EQ(rig.categoryDb->inserted, (std::vector<QString>{QStringLiteral("Root\\T")}));
    EXPECT_EQ(namesOf(result), (std::vector<QString>{QStringLiteral("a")}));
}

TEST(ImportItemsTree, UpdateCategoryAnsweringNilGivesAnUncategorisedMark) {
    // :156-158: VCategory := the value updateCategory returns, not the category that was created. A nil
    // answer is taken as it is: the mark is prepared with a nil category (the log shows <nil>).
    Rig rig;
    rig.categoryDb->storesLists = false;
    rig.categoryDb->returnsNull = true;

    const m::MarkList result =
        rig.importTree(node(QStringLiteral("T"), {pt(QStringLiteral("a"))}), makeConfig());

    EXPECT_EQ(rig.log, (t::CallLog{"cat.first Root\\T", "cat.list Root\\T", "cat.first Root\\T",
                                   "cat.update Root\\T", "factory.prepare a point @<nil>", "mark.write a"}));
    EXPECT_EQ(namesOf(result), (std::vector<QString>{QStringLiteral("a")}));
}

TEST(ImportItemsTree, NoPreparedMarkGivesNullAndNoMarkWrite) {
    Rig rig;
    const v::VectorItemTreePtr tree = node(QStringLiteral("T"), {}, {node(QStringLiteral("C"))});

    const m::MarkList result = rig.importTree(tree, makeConfig());

    // The categories are written even though no mark is (:235-237); updateMarkList is not called (:251-253).
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(rig.markDb->writeCalls, 0);
    EXPECT_EQ(rig.categoryDb->listed,
              (std::vector<QString>{QStringLiteral("Root\\T"), QStringLiteral("Root\\T\\C")}));
    EXPECT_EQ(rig.log, (t::CallLog{"cat.first Root\\T", "cat.first Root\\T\\C", "cat.list Root\\T",
                                   "cat.list Root\\T\\C", "cat.first Root\\T", "cat.first Root\\T\\C"}));
}

TEST(ImportItemsTree, GeneratedMarkNamesFollowTheLegacyRules) {
    // :167-170: an unnamed item of a named node takes the node's name, numbered "-<index + 1>" when the node
    // has more than one item (the index counts every item, named or not). An unnamed item of an unnamed node
    // keeps its empty name.
    struct Case {
        const char* title;
        QString nodeName;
        std::vector<v::VectorDataItemPtr> items;
        std::vector<QString> written;
    };
    const std::vector<Case> cases{
        {"a single unnamed item takes the node name",
         QStringLiteral("Town"),
         {pt({})},
         {QStringLiteral("Town")}},
        {"unnamed items are numbered by position",
         QStringLiteral("Town"),
         {pt({}), pt({})},
         {QStringLiteral("Town-1"), QStringLiteral("Town-2")}},
        {"numbering counts the named items too",
         QStringLiteral("Town"),
         {pt(QStringLiteral("x")), pt({}), pt({})},
         {QStringLiteral("x"), QStringLiteral("Town-2"), QStringLiteral("Town-3")}},
        {"an unnamed item of an unnamed node keeps its empty name", QString(), {pt({})}, {QString()}},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.title);
        Rig rig;
        rig.importTree(node(c.nodeName, c.items), makeConfig());
        EXPECT_EQ(rig.markDb->written, c.written);
    }
}

TEST(ImportItemsTree, IgnoreFlagLooksUpNamedItemsOnly) {
    // :173-176: with the flag, a named item is skipped when the database has a mark of that name in the
    // category. A generated name ("T-5") is not looked up, so the existing mark "T-5" does not skip it. The
    // lookup is exact: "город" is not "Город".
    Rig rig;
    rig.markDb->marks = {t::dataItem(QStringLiteral("dup"), t::point()),
                         t::dataItem(QStringLiteral("Город"), t::point()),
                         t::dataItem(QStringLiteral("T-5"), t::point())};
    const v::VectorItemTreePtr tree =
        node(QStringLiteral("T"), {pt(QStringLiteral("dup")), pt(QStringLiteral("new")),
                                   pt(QStringLiteral("Город")), pt(QStringLiteral("город")), pt({})});

    rig.importTree(tree, makeConfig({.ignoreSameName = true}));

    EXPECT_EQ(rig.log, (t::CallLog{
                           "cat.first Root\\T",
                           "cat.list Root\\T",
                           "cat.first Root\\T",
                           "mark.first dup @Root\\T",
                           "mark.first new @Root\\T",
                           "factory.prepare new point @Root\\T",
                           "mark.first Город @Root\\T",
                           "mark.first город @Root\\T",
                           "factory.prepare город point @Root\\T",
                           "factory.prepare T-5 point @Root\\T",
                           "mark.write new",
                           "mark.write город",
                           "mark.write T-5",
                       }));
}

TEST(ImportItemsTree, IgnoreFlagOffLooksUpNothing) {
    Rig rig;
    rig.markDb->marks = {t::dataItem(QStringLiteral("dup"), t::point())};
    const v::VectorItemTreePtr tree = node(QStringLiteral("T"), {pt(QStringLiteral("dup"))});

    rig.importTree(tree, makeConfig({.ignoreSameName = false}));

    // :173 reads the flag; it is false, so :174 is not reached and the item is kept.
    EXPECT_EQ(rig.markDb->written, (std::vector<QString>{QStringLiteral("dup")}));
    for (const std::string& line : rig.log) {
        EXPECT_NE(line.rfind("mark.first", 0), 0u) << line;
    }
}

TEST(ImportItemsTree, UnnamedItemsOfAnUnnamedNodeAreLookedUpWithTheFlag) {
    // :166 is "(VName = '') and (node name <> '')": with both names empty the first half holds but the second
    // does not, so the else branch runs and the ignore check looks up the empty name in the root category.
    Rig rig;
    rig.importTree(node(QString(), {pt({}), pt({})}), makeConfig({.ignoreSameName = true}));

    EXPECT_EQ(rig.log, (t::CallLog{"mark.first  @Root", "factory.prepare  point @Root", "mark.first  @Root",
                                   "factory.prepare  point @Root", "mark.write ", "mark.write "}));
    EXPECT_EQ(rig.markDb->written, (std::vector<QString>{QString(), QString()}));
}

TEST(ImportItemsTree, MarkParamsAreChosenByGeometryType) {
    // :179-186: the params of the first matching type; a multipoint matches none (nil params), and the
    // factory refuses a null params object (:188-194), so nothing is written.
    struct Case {
        const char* title;
        v::VectorDataItemPtr item;
        ConfigParts parts;
        t::CallLog expected;
    };
    const std::vector<Case> cases{
        {"a point takes the point params",
         pt(QStringLiteral("m")),
         {},
         {"factory.prepare m point @Root", "mark.write m"}},
        {"a line takes the line params",
         ln(QStringLiteral("m")),
         {},
         {"factory.prepare m line @Root", "mark.write m"}},
        {"a polygon takes the poly params",
         pg(QStringLiteral("m")),
         {},
         {"factory.prepare m poly @Root", "mark.write m"}},
        {"a multipoint matches no params", mp(QStringLiteral("m")), {}, {"factory.prepare m nil @Root"}},
        {"a point without point params",
         pt(QStringLiteral("m")),
         {.point = false},
         {"factory.prepare m nil @Root"}},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.title);
        Rig rig;
        rig.importTree(node(QString(), {c.item}), makeConfig(c.parts));
        EXPECT_EQ(rig.log, c.expected);
    }
}

TEST(ImportItemsTree, FactoryRefusalDropsOnlyThatMark) {
    Rig rig;
    rig.markFactory.refused = {QStringLiteral("bad")};

    const m::MarkList result = rig.importTree(
        node(QString(), {pt(QStringLiteral("bad")), pt(QStringLiteral("good"))}), makeConfig());

    EXPECT_EQ(rig.log, (t::CallLog{"factory.prepare bad point @Root", "factory.prepare good point @Root",
                                   "mark.write good"}));
    EXPECT_EQ(namesOf(result), (std::vector<QString>{QStringLiteral("good")}));
}

TEST(ImportItemsTree, UnnamedNodeTakesTheRootCategoryForItsItems) {
    // The category of an item is read from the root in each call (:140, :158): an unnamed node's items go to
    // the root, not to the nearest named ancestor. The path of a named node below it still continues through
    // the unnamed one (:141-152).
    Rig rig;
    const v::VectorItemTreePtr tree =
        node(QString(), {pt(QStringLiteral("top"))},
             {node(QStringLiteral("A"), {}, {node(QString(), {pt(QStringLiteral("x"))})})});

    rig.importTree(tree, makeConfig());

    EXPECT_EQ(rig.log, (t::CallLog{"cat.first Root\\A", "cat.list Root\\A", "factory.prepare top point @Root",
                                   "cat.first Root\\A", "factory.prepare x point @Root", "mark.write top",
                                   "mark.write x"}));
    EXPECT_EQ(rig.categoryDb->listed, (std::vector<QString>{QStringLiteral("Root\\A")}));
}

TEST(ImportItemsTree, SameNameTwiceInOneTreeListsTheCategoryTwice) {
    // :99-102 look only at the database, which does not yet hold the categories of this import, so each of
    // the two nodes is listed (a legacy quirk, reproduced).
    Rig rig;
    const v::VectorItemTreePtr tree =
        node(QString(), {}, {node(QStringLiteral("A")), node(QStringLiteral("A"))});

    const m::MarkList result = rig.importTree(tree, makeConfig());

    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(rig.categoryDb->listed,
              (std::vector<QString>{QStringLiteral("Root\\A"), QStringLiteral("Root\\A")}));
    EXPECT_EQ(rig.log, (t::CallLog{"cat.first Root\\A", "cat.first Root\\A", "cat.list Root\\A",
                                   "cat.list Root\\A", "cat.first Root\\A", "cat.first Root\\A"}));
}

TEST(ImportItemsTree, EmptyRootNameGivesPlainPaths) {
    // The exporter's root category has the name "" (u_VectorItemTreeMarksDb.pas:120). The root's empty name
    // is not joined: the top node's path is its own name (:94-98), and its child's path is "A\B" (:148-152).
    Rig rig;
    const v::VectorItemTreePtr tree = node(QStringLiteral("A"), {pt(QStringLiteral("a"))},
                                           {node(QStringLiteral("B"), {pt(QStringLiteral("b"))})});

    rig.importTree(tree, makeConfig({.rootName = QString()}));

    EXPECT_EQ(rig.log, (t::CallLog{"cat.first A", "cat.first A\\B", "cat.list A", "cat.list A\\B",
                                   "cat.first A", "factory.prepare a point @A", "cat.first A\\B",
                                   "factory.prepare b point @A\\B", "mark.write a", "mark.write b"}));
    EXPECT_EQ(rig.categoryDb->listed, (std::vector<QString>{QStringLiteral("A"), QStringLiteral("A\\B")}));
}

TEST(ImportItemsTree, RefusesNilArgumentsWithoutAnyWrite) {
    // Each row is the Pascal's first dereference of the nil value (:86, :90, :106), so the database is not
    // reached for a write; the lookups read before the refusal are the ones legacy makes first.
    struct Case {
        const char* title;
        v::VectorItemTreePtr tree;
        v::ImportConfigPtr config;
        t::CallLog expected;
    };
    const v::ImportConfigPtr good = makeConfig();
    const std::vector<Case> cases{
        {"nil tree", nullptr, good, {}},
        {"nil config", node(QStringLiteral("T"), {pt(QStringLiteral("a"))}), nullptr, {}},
        {"nil root category",
         node(QStringLiteral("T"), {pt(QStringLiteral("a"))}),
         makeConfig({.nilRoot = true}),
         {}},
        {"nil node at the top", node(QString(), {}, {nullptr}), good, {}},
        {"nil node under a named node",
         node(QStringLiteral("A"), {}, {nullptr}),
         good,
         {"cat.first Root\\A"}},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.title);
        Rig rig;
        EXPECT_THROW(rig.importTree(c.tree, c.config), std::invalid_argument);
        EXPECT_EQ(rig.log, c.expected);
        EXPECT_EQ(rig.categoryDb->listCalls, 0);
        EXPECT_EQ(rig.markDb->writeCalls, 0);
    }
}

TEST(ImportItemsTree, NilItemRefusedAfterTheCategoryListIsWritten) {
    // :164-165: the category list (:235-237) is written before the marks phase reaches the nil item; no mark
    // is written (:252-253 is not reached).
    Rig rig;
    EXPECT_THROW(rig.importTree(node(QStringLiteral("T"), {nullptr}), makeConfig()), std::invalid_argument);

    EXPECT_EQ(rig.log, (t::CallLog{"cat.first Root\\T", "cat.list Root\\T", "cat.first Root\\T"}));
    EXPECT_EQ(rig.markDb->writeCalls, 0);
}

TEST(ImportItemsTree, NilCategoryParamsRefusedOnlyWhenRead) {
    // :173 reads the category params for a named item (refused here, before the lookup).
    Rig named;
    EXPECT_THROW(named.importTree(node(QStringLiteral("T"), {pt(QStringLiteral("a"))}),
                                  makeConfig({.nilCategoryParams = true})),
                 std::invalid_argument);
    EXPECT_EQ(named.log, (t::CallLog{"cat.first Root\\T", "cat.list Root\\T", "cat.first Root\\T"}));

    // A generated name does not read them (:167-171 take the other branch), so the item is prepared.
    Rig generated;
    generated.importTree(node(QStringLiteral("T"), {pt({})}), makeConfig({.nilCategoryParams = true}));
    EXPECT_EQ(generated.markDb->written, (std::vector<QString>{QStringLiteral("T")}));
}

// CategoryTreeToMarkTreeHelper.

m::MarkCategoryTreePtr folder(const QString& name, m::MarkCategoryPtr markCategory,
                              std::vector<m::MarkCategoryTreePtr> children = {}) {
    return std::make_shared<const m::MarkCategoryTree>(std::move(markCategory), name, std::move(children));
}

v::VectorItemSubsetPtr subsetOf(const QString& markName) {
    return std::make_shared<const v::VectorItemSubset>(0, std::vector<v::VectorDataItemPtr>{pt(markName)});
}

TEST(CategoryTreeToMarkTreeHelper, NilNodeGivesNullWithoutDbCalls) {
    Rig rig;
    EXPECT_EQ(m::categoryTreeToMarkTreeHelper(*rig.markDb, nullptr, true), nullptr);
    EXPECT_TRUE(rig.log.empty());
}

TEST(CategoryTreeToMarkTreeHelper, ParentIsAskedBeforeChildrenAndChildrenKeepTheirOrder) {
    Rig rig;
    const v::VectorItemSubsetPtr subsetF = subsetOf(QStringLiteral("f"));
    const v::VectorItemSubsetPtr subsetA = subsetOf(QStringLiteral("a"));
    const v::VectorItemSubsetPtr subsetC = subsetOf(QStringLiteral("c"));
    rig.markDb->subsets = {
        {QStringLiteral("F"), subsetF}, {QStringLiteral("A"), subsetA}, {QStringLiteral("C"), subsetC}};
    const m::MarkCategoryTreePtr tree = folder(
        QStringLiteral("F"), category(QStringLiteral("F")),
        {folder(QStringLiteral("A"), category(QStringLiteral("A"))),
         folder(QStringLiteral("B"), nullptr, {folder(QStringLiteral("C"), category(QStringLiteral("C")))})});

    const v::VectorItemTreePtr result = m::categoryTreeToMarkTreeHelper(*rig.markDb, tree, true);

    EXPECT_EQ(rig.log,
              (t::CallLog{"mark.subset F hidden=1", "mark.subset A hidden=1", "mark.subset C hidden=1"}));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->items(), subsetF);
    ASSERT_EQ(result->subTreeItemCount(), 2);
    EXPECT_EQ(result->subTreeItem(0)->name(), QStringLiteral("A"));
    EXPECT_EQ(result->subTreeItem(0)->items(), subsetA);
    EXPECT_EQ(result->subTreeItem(1)->name(), QStringLiteral("B"));
    EXPECT_EQ(result->subTreeItem(1)->items(), nullptr);
    ASSERT_EQ(result->subTreeItem(1)->subTreeItemCount(), 1);
    EXPECT_EQ(result->subTreeItem(1)->subTreeItem(0)->name(), QStringLiteral("C"));
    EXPECT_EQ(result->subTreeItem(1)->subTreeItem(0)->items(), subsetC);
}

TEST(CategoryTreeToMarkTreeHelper, NilChildIsKeptAsNilSubtree) {
    // :282-291: the result of the recursion is added even when it is nil.
    Rig rig;
    const m::MarkCategoryTreePtr tree = folder(QStringLiteral("F"), category(QStringLiteral("F")), {nullptr});

    const v::VectorItemTreePtr result = m::categoryTreeToMarkTreeHelper(*rig.markDb, tree, false);

    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->subTreeItemCount(), 1);
    EXPECT_EQ(result->subTreeItem(0), nullptr);
    EXPECT_EQ(rig.log, (t::CallLog{"mark.subset F hidden=0"}));
}

} // namespace
