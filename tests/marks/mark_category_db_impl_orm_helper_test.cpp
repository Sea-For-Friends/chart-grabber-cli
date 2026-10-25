// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-033, FEAT-GEO-034 (both held open). Ports no legacy test (there is none): the SQL half
// of u_MarkCategoryDbImplORMHelper.pas against a copy of tests/fixtures/marks/Marks.db3 and an empty
// database (docs/specs/marks.md §2.9.6, statements S-10 ... S-1C of §2.9.5). Expected values are literals
// read back with plain SQL on a second connection, never through the helper.
#include "cg/marks/mormot/categories/mark_category_db_impl_orm_helper.h"
#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"
#include "cg/marks/mormot/mark_system_orm_log.h"
#include "cg/marks/mormot/mark_system_orm_types.h"
#include "cg/marks/mormot/system_nocase.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "../corpus_skip.h"
#include "fixture_path.h"

using cg::marks::MarkCategoryDbImplORMHelper;
using cg::marks::MarkSystemImplConfigORM;
using cg::marks::MarkSystemImplORMClientProvider;
using cg::marks::MarkSystemORMError;
using cg::marks::OrmCategoryRec;
using cg::storage::sqlite::SqliteDb;
using cg::storage::sqlite::SqliteStmt;

namespace {

// A database file (a copy of the fixture, or a missing file the provider creates), a provider on it and a
// second plain connection for seeding and reading back.
class Env {
public:
    explicit Env(bool fromFixture) {
        EXPECT_TRUE(dir_.isValid());
        const QString path = QDir(dir_.path()).filePath(QStringLiteral("Marks.db3"));
        if (fromFixture) {
            EXPECT_TRUE(QFile::copy(cgMarksFixture(QStringLiteral("marks/Marks.db3")), path));
            EXPECT_TRUE(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
        }
        provider_ = std::make_unique<MarkSystemImplORMClientProvider>(
            dir_.path(), MarkSystemImplConfigORM({}, false, {}, {}, {}));
        auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
        if (!db) {
            ADD_FAILURE() << db.error().what().toStdString();
            std::abort();
        }
        plain_ = std::make_unique<SqliteDb>(std::move(*db));
        std::ignore = plain_->createCollation("SYSTEMNOCASE", &cg::marks::systemNoCaseCompare);
    }

    MarkSystemImplORMClientProvider& provider() { return *provider_; }

    MarkCategoryDbImplORMHelper helper(bool readOnly = false) { return {readOnly, *provider_}; }

    void exec(const char* sql) {
        const auto done = plain_->exec(sql);
        ASSERT_TRUE(done.has_value()) << sql;
    }

    QStringList rows(const char* sql) {
        QStringList out;
        auto stmt = plain_->prepare(sql);
        if (!stmt) {
            ADD_FAILURE() << stmt.error().what().toStdString();
            return out;
        }
        for (auto step = stmt->step(); step && *step == SqliteStmt::StepResult::Row; step = stmt->step()) {
            QStringList columns;
            for (int i = 0; i < stmt->columnCount(); ++i) {
                columns.append(stmt->columnText(i));
            }
            out.append(columns.join(QLatin1Char('|')));
        }
        return out;
    }

    QStringList categories() { return rows("SELECT ID,cName FROM Category ORDER BY ID"); }
    QStringList views() {
        return rows(
            "SELECT ID,cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom FROM CategoryView ORDER BY ID");
    }

private:
    QTemporaryDir dir_;
    std::unique_ptr<MarkSystemImplORMClientProvider> provider_;
    std::unique_ptr<SqliteDb> plain_;
};

OrmCategoryRec rec(const QString& name, bool visible = true, int minZoom = 3, int maxZoom = 23) {
    OrmCategoryRec r;
    r.name = name;
    r.visible = visible;
    r.minZoom = static_cast<quint8>(minZoom);
    r.maxZoom = static_cast<quint8>(maxZoom);
    return r;
}

QString message(const std::function<void()>& call) {
    try {
        call();
    } catch (const MarkSystemORMError& e) {
        return QString::fromStdString(e.what());
    }
    return {};
}

const QString kTown = QString::fromUtf8("Город");
const QString kTownLower = QString::fromUtf8("город");
const QString kStreet = QString::fromUtf8("Улица");
const QString kVillage = QString::fromUtf8("Село");

// The fixture has no categories and one user (1, sasgis); user 2 is seeded where "all users" matters.
void seedTwoUsers(Env& env) {
    env.exec("INSERT INTO User (ID,uName) VALUES (2,'other')");
}

} // namespace

// ---- insert (S-14, S-19), LC-16 ---------------------------------------------------------------------

namespace {
struct InsertCase {
    const char* label;
    QString name;
    bool visible;
    int minZoom;
    int maxZoom;
    const char* category; // expected Category rows
    const char* view;     // expected CategoryView rows: ID|user|category|visible|min|max
};
} // namespace

TEST(MarkCategoryDbImplORMHelper, InsertTable) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    const std::vector<InsertCase> cases = {
        // LC-16: a visible category gets no CategoryView row, whatever its zooms
        {"visible default zooms", QStringLiteral("a"), true, 3, 23, "1|a", ""},
        {"visible custom zooms", QStringLiteral("a"), true, 5, 10, "1|a", ""},
        {"hidden custom zooms", QStringLiteral("a"), false, 4, 9, "1|a", "1|1|1|0|4|9"},
        {"hidden, non-ASCII name", kTown + QStringLiteral("\\") + kStreet, false, 7, 8,
         "1|\xD0\x93\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB4\\\xD0\xA3\xD0\xBB\xD0\xB8\xD1\x86\xD0\xB0",
         "1|1|1|0|7|8"},
    };
    for (const bool fromFixture : {true, false}) {
        for (const InsertCase& c : cases) {
            Env env(fromFixture);
            auto helper = env.helper();
            OrmCategoryRec r = rec(c.name, c.visible, c.minZoom, c.maxZoom);
            ASSERT_TRUE(helper.insertCategorySql(r)) << c.label;
            EXPECT_EQ(r.categoryId, 1) << c.label;
            EXPECT_EQ(r.viewId, c.visible ? 0 : 1) << c.label;
            EXPECT_EQ(env.categories(), QStringList{QString::fromUtf8(c.category)}) << c.label;
            EXPECT_EQ(env.views(), c.view[0] ? QStringList{QString::fromUtf8(c.view)} : QStringList{})
                << c.label;
        }
    }
}

TEST(MarkCategoryDbImplORMHelper, VisibleCategoryKeepsItsZoomsInTheCacheOnly) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    auto helper = env.helper();
    OrmCategoryRec r = rec(QStringLiteral("a"), true, 5, 10);
    ASSERT_TRUE(helper.insertCategorySql(r));

    OrmCategoryRec fromCache;
    ASSERT_TRUE(helper.readCategorySql(fromCache, 1, {}));
    EXPECT_EQ(fromCache.minZoom, 5);
    EXPECT_EQ(fromCache.maxZoom, 10);

    auto afterRestart = env.helper(); // a new helper = a new, empty cache
    OrmCategoryRec fromDb;
    ASSERT_TRUE(afterRestart.readCategorySql(fromDb, 1, {}));
    EXPECT_EQ(fromDb.minZoom, 3);
    EXPECT_EQ(fromDb.maxZoom, 23);
    EXPECT_TRUE(fromDb.visible);
}

TEST(MarkCategoryDbImplORMHelper, InsertReadOnlyDoesNothing) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    auto helper = env.helper(true);
    OrmCategoryRec r = rec(QStringLiteral("a"), false);
    EXPECT_FALSE(helper.insertCategorySql(r));
    EXPECT_EQ(r.categoryId, 0);
    EXPECT_EQ(env.categories(), QStringList{});
    EXPECT_EQ(env.views(), QStringList{});
}

// ---- delete (S-1C, S-16) ----------------------------------------------------------------------------

TEST(MarkCategoryDbImplORMHelper, DeleteRemovesTheViewsOfAllUsersAndTheCaches) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    seedTwoUsers(env);
    env.exec("INSERT INTO Category (ID,cName) VALUES (1,'a'),(2,'b')");
    env.exec("INSERT INTO CategoryView (ID,cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) VALUES "
             "(1,1,1,0,4,9),(2,2,1,1,3,23),(3,1,2,0,5,6)");
    auto helper = env.helper();
    OrmCategoryRec warm;
    ASSERT_TRUE(helper.readCategorySql(warm, 1, {})); // fills both caches for category 1

    EXPECT_TRUE(helper.deleteCategorySql(1));
    EXPECT_EQ(env.categories(), QStringList{QStringLiteral("2|b")});
    EXPECT_EQ(env.views(), QStringList{QStringLiteral("3|1|2|0|5|6")});
    OrmCategoryRec gone;
    EXPECT_FALSE(helper.readCategorySql(gone, 1, {})); // a stale cache row would answer true
}

TEST(MarkCategoryDbImplORMHelper, DeleteReadOnlyAndEmptyId) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    env.exec("INSERT INTO Category (ID,cName) VALUES (1,'a')");
    auto readOnly = env.helper(true);
    EXPECT_FALSE(readOnly.deleteCategorySql(1));
    EXPECT_EQ(env.categories(), QStringList{QStringLiteral("1|a")});

    auto helper = env.helper();
    EXPECT_EQ(message([&] { helper.deleteCategorySql(0); }), QStringLiteral("MarkSystemORM: ID is empty!"));
    EXPECT_EQ(env.categories(), QStringList{QStringLiteral("1|a")});
}

// ---- update, view part (S-17, S-19, S-1A) ------------------------------------------------------------

namespace {
struct ViewUpdateCase {
    const char* label;
    bool newVisible;
    int newMin;
    int newMax;
    const char*
        view; // the row after the update (the cache holds visible, 7, 8; the database 1|1|1|1|9|8 before)
};
} // namespace

TEST(MarkCategoryDbImplORMHelper, UpdateViewWritesOnlyTheChangedColumns) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // old = (visible, 7, 8). The cache says (visible, 7, 8) too, but the database row is changed behind the
    // cache to min 9: a column that is not in the changed list keeps the database value.
    const std::vector<ViewUpdateCase> cases = {
        {"visible only", false, 7, 8, "1|1|1|0|9|8"},
        {"min only", true, 6, 8, "1|1|1|1|6|8"},
        {"max only", true, 7, 20, "1|1|1|1|9|20"},
        {"all three", false, 5, 21, "1|1|1|0|5|21"},
    };
    for (const ViewUpdateCase& c : cases) {
        Env env(true);
        env.exec("INSERT INTO Category (ID,cName) VALUES (1,'a')");
        env.exec("INSERT INTO CategoryView (ID,cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) VALUES "
                 "(1,1,1,1,7,8)");
        auto helper = env.helper();
        OrmCategoryRec warm;
        ASSERT_TRUE(helper.readCategorySql(warm, 1, {})); // the view row is now in the cache
        env.exec("UPDATE CategoryView SET cvMinZoom=9 WHERE ID=1");

        OrmCategoryRec oldRec = warm;
        OrmCategoryRec newRec = rec(QStringLiteral("a"), c.newVisible, c.newMin, c.newMax);
        EXPECT_TRUE(helper.updateCategorySql(oldRec, newRec)) << c.label;
        EXPECT_EQ(newRec.categoryId, 1) << c.label;
        EXPECT_EQ(newRec.viewId, 1) << c.label;
        EXPECT_EQ(env.views(), QStringList{QString::fromUtf8(c.view)}) << c.label;
    }
}

TEST(MarkCategoryDbImplORMHelper, UpdateViewWithoutRowInsertsAllFiveValues) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    env.exec("INSERT INTO Category (ID,cName) VALUES (1,'a')");
    auto helper = env.helper();
    OrmCategoryRec oldRec;
    ASSERT_TRUE(helper.readCategorySql(oldRec, 1, {})); // no view row: the cache gets the defaults, viewId 0
    EXPECT_EQ(oldRec.viewId, 0);

    OrmCategoryRec newRec = rec(QStringLiteral("a"), true, 5, 10); // only the zooms change
    EXPECT_TRUE(helper.updateCategorySql(oldRec, newRec));
    EXPECT_EQ(env.views(), QStringList{QStringLiteral("1|1|1|1|5|10")});
    EXPECT_EQ(newRec.viewId, 1);
}

TEST(MarkCategoryDbImplORMHelper, UpdateWithoutChangeReturnsFalse) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    env.exec("INSERT INTO Category (ID,cName) VALUES (1,'a')");
    auto helper = env.helper();
    OrmCategoryRec oldRec = rec(QStringLiteral("a"));
    oldRec.categoryId = 1;
    OrmCategoryRec newRec = oldRec;
    newRec.categoryId = 99; // overwritten by the old id
    EXPECT_FALSE(helper.updateCategorySql(oldRec, newRec));
    EXPECT_EQ(newRec.categoryId, 1);
    OrmCategoryRec noId = rec(QStringLiteral("a"));
    EXPECT_EQ(message([&] { helper.updateCategorySql(noId, newRec); }),
              QStringLiteral("MarkSystemORM: ID is empty!"));
}

TEST(MarkCategoryDbImplORMHelper, UpdateReadOnlyChangesTheCacheOnly) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    env.exec("INSERT INTO Category (ID,cName) VALUES (1,'a')");
    env.exec("INSERT INTO CategoryView (ID,cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) VALUES "
             "(1,1,1,1,3,23)");
    auto helper = env.helper(true);
    OrmCategoryRec oldRec;
    ASSERT_TRUE(helper.readCategorySql(oldRec, 1, {}));

    OrmCategoryRec hide = oldRec;
    hide.visible = false;
    EXPECT_TRUE(helper.updateCategorySql(oldRec, hide));
    EXPECT_EQ(hide.viewId, 1);
    OrmCategoryRec again;
    ASSERT_TRUE(helper.readCategorySql(again, 1, {}));
    EXPECT_FALSE(again.visible);
    EXPECT_EQ(env.views(), QStringList{QStringLiteral("1|1|1|1|3|23")});

    // a name change alone returns at the read-only check with the result still false
    OrmCategoryRec rename = again;
    rename.name = QStringLiteral("b");
    EXPECT_FALSE(helper.updateCategorySql(again, rename));
    EXPECT_EQ(env.categories(), QStringList{QStringLiteral("1|a")});
}

TEST(MarkCategoryDbImplORMHelper, UpdateViewColdCacheReadsTheViewRowFirst) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // S-17 in the update path: the view cache is empty and not prepared. An existing row must be updated
    // (S-1A); an INSERT would hit the unique index (cvCategory, cvUser) and throw.
    {
        Env env(true);
        env.exec("INSERT INTO Category (ID,cName) VALUES (1,'a')");
        env.exec("INSERT INTO CategoryView (ID,cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) VALUES "
                 "(4,1,1,1,7,8)");
        auto helper = env.helper();
        OrmCategoryRec oldRec = rec(QStringLiteral("a"), true, 7, 8);
        oldRec.categoryId = 1;
        OrmCategoryRec newRec = rec(QStringLiteral("a"), false, 7, 8);
        EXPECT_TRUE(helper.updateCategorySql(oldRec, newRec));
        EXPECT_EQ(newRec.viewId, 4);
        EXPECT_EQ(env.views(), QStringList{QStringLiteral("4|1|1|0|7|8")});
    }
    {
        Env env(true); // no row: S-19 with the five values of the new record
        env.exec("INSERT INTO Category (ID,cName) VALUES (1,'a')");
        auto helper = env.helper();
        OrmCategoryRec oldRec = rec(QStringLiteral("a"));
        oldRec.categoryId = 1;
        OrmCategoryRec newRec = rec(QStringLiteral("a"), false, 5, 10);
        EXPECT_TRUE(helper.updateCategorySql(oldRec, newRec));
        EXPECT_EQ(newRec.viewId, 1);
        EXPECT_EQ(env.views(), QStringList{QStringLiteral("1|1|1|0|5|10")});
    }
}

// ---- update, name part (S-15, sub categories) -------------------------------------------------------

TEST(MarkCategoryDbImplORMHelper, RenameRewritesTheSubCategories) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    struct Case {
        const char* label;
        QString from;
        QString to;
        QStringList before;
        QStringList after;
    };
    const QString bs = QStringLiteral("\\");
    const std::vector<Case> cases = {
        {"children, case-insensitive prefix, only the first occurrence",
         kTown,
         kVillage,
         {kTown, kTownLower + bs + kStreet, kTown + bs + kTown + bs + QStringLiteral("x"),
          QStringLiteral("x") + bs + kTown + bs + QStringLiteral("y"), kTown + QStringLiteral("2")},
         {kVillage, kVillage + bs + kStreet, kVillage + bs + kTown + bs + QStringLiteral("x"),
          QStringLiteral("x") + bs + kTown + bs + QStringLiteral("y"), kTown + QStringLiteral("2")}},
        {"ASCII",
         QStringLiteral("Ab"),
         QStringLiteral("c"),
         {QStringLiteral("Ab"), QStringLiteral("aB\\z")},
         {QStringLiteral("c"), QStringLiteral("c\\z")}},
        {"case-only change is a change",
         kTownLower,
         kTown,
         {kTownLower, kTownLower + bs + QStringLiteral("q")},
         {kTown, kTown + bs + QStringLiteral("q")}},
    };
    for (const bool warm : {false, true}) { // the category cache is prepared by the rename itself
        for (const Case& c : cases) {
            Env env(true);
            int id = 1;
            for (const QString& name : c.before) {
                env.exec(QStringLiteral("INSERT INTO Category (ID,cName) VALUES (%1,'%2')")
                             .arg(id++)
                             .arg(name)
                             .toUtf8()
                             .constData());
            }
            auto helper = env.helper();
            OrmCategoryRec oldRec;
            ASSERT_TRUE(helper.readCategorySql(oldRec, 1, {})) << c.label;
            if (warm) {
                std::vector<OrmCategoryRec> all;
                helper.getCategoriesRecArray(all);
            }
            OrmCategoryRec newRec = oldRec;
            newRec.name = c.to;
            ASSERT_TRUE(helper.updateCategorySql(oldRec, newRec)) << c.label;

            QStringList expectedDb;
            id = 1;
            for (const QString& name : c.after) {
                expectedDb.append(QStringLiteral("%1|%2").arg(id++).arg(name));
            }
            EXPECT_EQ(env.categories(), expectedDb) << c.label;
            std::vector<OrmCategoryRec> all;
            EXPECT_EQ(helper.getCategoriesRecArray(all), static_cast<int>(c.after.size())) << c.label;
            for (size_t i = 0; i < all.size(); ++i) {
                EXPECT_EQ(all[i].name, c.after[static_cast<qsizetype>(i)]) << c.label; // the cache follows
            }
        }
    }
}

// ---- read (S-10, S-11, S-17) and the two name look-ups, L-187 ----------------------------------------

TEST(MarkCategoryDbImplORMHelper, RenamingIntoItsOwnSubtreeRewritesTheRenamedRowAgain) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // Legacy :320 puts the new name in the cache, then the sub category loop (:326-336) sees "A\B" starting
    // with "A\" and rewrites the renamed row itself: database and cache hold "A\B\B", the caller's record
    // "A\B".
    Env env(true);
    env.exec("INSERT INTO Category (ID,cName) VALUES (1,'A'),(2,'A\\C')");
    auto helper = env.helper();
    OrmCategoryRec oldRec;
    ASSERT_TRUE(helper.readCategorySql(oldRec, 1, {}));
    OrmCategoryRec newRec = oldRec;
    newRec.name = QStringLiteral("A\\B");
    ASSERT_TRUE(helper.updateCategorySql(oldRec, newRec));
    EXPECT_EQ(newRec.name, QStringLiteral("A\\B"));
    EXPECT_EQ(env.categories(), (QStringList{QStringLiteral("1|A\\B\\B"), QStringLiteral("2|A\\B\\C")}));
    OrmCategoryRec fromCache;
    ASSERT_TRUE(helper.readCategorySql(fromCache, 1, {}));
    EXPECT_EQ(fromCache.name, QStringLiteral("A\\B\\B"));
}

TEST(MarkCategoryDbImplORMHelper, ReadByNameWithDuplicatesTakesTheLowestRowId) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    env.exec("INSERT INTO Category (ID,cName) VALUES (4,'dup'),(2,'DUP'),(3,'dup')");
    auto helper = env.helper();
    OrmCategoryRec r;
    ASSERT_TRUE(helper.readCategorySql(r, 0, QStringLiteral("dup")));
    EXPECT_EQ(r.categoryId, 2);
    EXPECT_EQ(r.name, QStringLiteral("DUP"));
}

TEST(MarkCategoryDbImplORMHelper, ReadByIdAndByNameFromTheDatabase) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    env.exec("INSERT INTO Category (ID,cName) VALUES (3,'Abc'),(5,'x')");
    env.exec("INSERT INTO CategoryView (ID,cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) VALUES "
             "(8,1,3,0,4,9)");
    auto helper = env.helper();
    OrmCategoryRec r;
    ASSERT_TRUE(helper.readCategorySql(r, 3, {}));
    EXPECT_EQ(r.name, QStringLiteral("Abc"));
    EXPECT_EQ(r.viewId, 8);
    EXPECT_FALSE(r.visible);
    EXPECT_EQ(r.minZoom, 4);
    EXPECT_EQ(r.maxZoom, 9);

    // the id wins when both are given
    auto byName = env.helper();
    ASSERT_TRUE(byName.readCategorySql(r, 5, QStringLiteral("Abc")));
    EXPECT_EQ(r.categoryId, 5);
    EXPECT_EQ(r.name, QStringLiteral("x"));
    // by name, SYSTEMNOCASE equality finds "ABC" (ASCII)
    auto bySqlName = env.helper(); // a cold cache
    ASSERT_TRUE(bySqlName.readCategorySql(r, 0, QStringLiteral("ABC")));
    EXPECT_EQ(r.categoryId, 3);
    EXPECT_EQ(r.name, QStringLiteral("Abc"));

    // no view row: the defaults of the empty record, viewId 0
    ASSERT_TRUE(byName.readCategorySql(r, 5, {}));
    EXPECT_EQ(r.viewId, 0);
    EXPECT_TRUE(r.visible);
    EXPECT_EQ(r.minZoom, 3);
    EXPECT_EQ(r.maxZoom, 23);

    // the defaults were cached: a view row added behind the cache is not seen
    env.exec("INSERT INTO CategoryView (ID,cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) VALUES "
             "(9,1,5,0,1,2)");
    ASSERT_TRUE(byName.readCategorySql(r, 5, {}));
    EXPECT_EQ(r.viewId, 0);
    EXPECT_TRUE(r.visible);

    EXPECT_FALSE(byName.readCategorySql(r, 77, {}));
    EXPECT_FALSE(byName.readCategorySql(r, 0, QStringLiteral("nothing")));
    EXPECT_FALSE(byName.readCategorySql(r, 0, {}));
    EXPECT_EQ(r.categoryId, 0); // reset to the empty record
}

TEST(MarkCategoryDbImplORMHelper, NameLookupIsSystemNoCaseInSqlAndAnsiSameTextInTheCache) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // L-187 / LC-14: Cyrillic names differing in case are the same name for the cache, different for SQL
    Env env(true);
    env.exec(
        QStringLiteral("INSERT INTO Category (ID,cName) VALUES (1,'%1')").arg(kTown).toUtf8().constData());
    auto helper = env.helper();
    OrmCategoryRec r;
    EXPECT_FALSE(helper.readCategorySql(r, 0, kTownLower)); // cache empty and not prepared -> SQL -> no row
    EXPECT_TRUE(helper.readCategorySql(r, 0, kTown));       // exact bytes
    EXPECT_EQ(helper.countCategorySql(kTownLower), 0);      // S-12 is SQL, too
    EXPECT_EQ(helper.countCategorySql(kTown), 1);

    std::vector<OrmCategoryRec> all;
    ASSERT_EQ(helper.getCategoriesRecArray(all), 1);       // prepares the category cache
    EXPECT_TRUE(helper.readCategorySql(r, 0, kTownLower)); // now the cache answers
    EXPECT_EQ(r.categoryId, 1);
    EXPECT_EQ(r.name, kTown);                          // the stored spelling
    EXPECT_EQ(helper.countCategorySql(kTownLower), 0); // still SQL
}

TEST(MarkCategoryDbImplORMHelper, CountCategoryCountsEveryRowOfTheName) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    env.exec("INSERT INTO Category (ID,cName) VALUES (1,'x'),(2,'X'),(3,'y')");
    auto helper = env.helper();
    EXPECT_EQ(helper.countCategorySql(QStringLiteral("x")), 2);
    EXPECT_EQ(helper.countCategorySql(QStringLiteral("y")), 1);
    EXPECT_EQ(helper.countCategorySql(QStringLiteral("z")), 0);
}

// ---- list (S-13, S-18) -------------------------------------------------------------------------------

TEST(MarkCategoryDbImplORMHelper, ListIsInIdOrderWithViewValuesOrDefaults) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    seedTwoUsers(env);
    env.exec("INSERT INTO Category (ID,cName) VALUES (9,'c'),(2,'a'),(5,'b')");
    env.exec("INSERT INTO CategoryView (ID,cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) VALUES "
             "(1,1,5,0,6,7),(2,2,9,0,1,2)");
    auto helper = env.helper();
    std::vector<OrmCategoryRec> all;
    ASSERT_EQ(helper.getCategoriesRecArray(all), 3);
    ASSERT_EQ(all.size(), 3U);
    EXPECT_EQ(all[0].categoryId, 2);
    EXPECT_EQ(all[1].categoryId, 5);
    EXPECT_EQ(all[2].categoryId, 9);
    EXPECT_EQ(all[1].name, QStringLiteral("b"));
    EXPECT_EQ(all[1].viewId, 1);
    EXPECT_FALSE(all[1].visible);
    EXPECT_EQ(all[1].minZoom, 6);
    EXPECT_EQ(all[1].maxZoom, 7);
    // the other user's row is not ours: the defaults
    EXPECT_EQ(all[2].viewId, 0);
    EXPECT_TRUE(all[2].visible);
    EXPECT_EQ(all[2].minZoom, 3);
    EXPECT_EQ(all[2].maxZoom, 23);
}

TEST(MarkCategoryDbImplORMHelper, ListOfAnEmptyDatabase) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    for (const bool fromFixture : {true, false}) {
        Env env(fromFixture);
        auto helper = env.helper();
        std::vector<OrmCategoryRec> all(4);
        EXPECT_EQ(helper.getCategoriesRecArray(all), 0);
        EXPECT_TRUE(all.empty());
    }
}

// ---- set all visible (S-1B, S-19) and read-only toggles ----------------------------------------------

TEST(MarkCategoryDbImplORMHelper, SetAllVisibleFalseAddsMissingRowsWithDefaultZooms) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    seedTwoUsers(env);
    env.exec("INSERT INTO Category (ID,cName) VALUES (1,'a'),(2,'b'),(3,'c')");
    env.exec("INSERT INTO CategoryView (ID,cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) VALUES "
             "(1,1,1,1,5,10),(2,2,1,1,3,23)");
    auto helper = env.helper();
    helper.setAllCategoriesVisibleSql(false);
    EXPECT_EQ(env.views(), (QStringList{QStringLiteral("1|1|1|0|5|10"), QStringLiteral("2|2|1|1|3|23"),
                                        QStringLiteral("3|1|2|0|3|23"), QStringLiteral("4|1|3|0|3|23")}));

    // showing all updates our rows and never deletes or adds
    helper.setAllCategoriesVisibleSql(true);
    EXPECT_EQ(env.views(), (QStringList{QStringLiteral("1|1|1|1|5|10"), QStringLiteral("2|2|1|1|3|23"),
                                        QStringLiteral("3|1|2|1|3|23"), QStringLiteral("4|1|3|1|3|23")}));
}

TEST(MarkCategoryDbImplORMHelper, SetAllVisibleOverwritesCacheOnlyZoomsWithTheDefaults) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // LC-16: a visible category with zooms 5-10 has no row; "hide all" gives it 3-23
    Env env(true);
    auto helper = env.helper();
    OrmCategoryRec r = rec(QStringLiteral("a"), true, 5, 10);
    ASSERT_TRUE(helper.insertCategorySql(r));
    helper.setAllCategoriesVisibleSql(false);
    EXPECT_EQ(env.views(), QStringList{QStringLiteral("1|1|1|0|3|23")});
    OrmCategoryRec back;
    ASSERT_TRUE(helper.readCategorySql(back, 1, {}));
    EXPECT_EQ(back.minZoom, 3);
    EXPECT_EQ(back.maxZoom, 23);
}

TEST(MarkCategoryDbImplORMHelper, ReadOnlyToggleChangesTheCacheAndSetReadOnlyResetsIt) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    env.exec("INSERT INTO Category (ID,cName) VALUES (1,'a'),(2,'b')");
    env.exec("INSERT INTO CategoryView (ID,cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) VALUES "
             "(1,1,1,1,5,10)");
    auto helper = env.helper(true);
    helper.setAllCategoriesVisibleSql(false);
    EXPECT_EQ(env.views(), QStringList{QStringLiteral("1|1|1|1|5|10")}); // read-only: no SQL

    std::vector<OrmCategoryRec> all;
    ASSERT_EQ(helper.getCategoriesRecArray(all), 2);
    EXPECT_FALSE(all[0].visible);
    EXPECT_EQ(all[0].viewId, 1);
    EXPECT_FALSE(all[1].visible);
    EXPECT_EQ(all[1].viewId, 0); // fake id: the category had no row

    helper.setReadOnly(true); // no change: the cache stays
    ASSERT_EQ(helper.getCategoriesRecArray(all), 2);
    EXPECT_FALSE(all[0].visible);

    helper.setReadOnly(false); // away from read-only: the view cache is dropped, the database speaks again
    EXPECT_FALSE(helper.isReadOnly());
    ASSERT_EQ(helper.getCategoriesRecArray(all), 2);
    EXPECT_TRUE(all[0].visible);
    EXPECT_EQ(all[0].minZoom, 5);
    EXPECT_TRUE(all[1].visible);
    EXPECT_EQ(all[1].viewId, 0);

    helper.setReadOnly(true); // towards read-only: nothing is dropped
    EXPECT_TRUE(helper.isReadOnly());
    ASSERT_EQ(helper.getCategoriesRecArray(all), 2);
    EXPECT_TRUE(all[0].visible);
}

// ---- the connection mutex: one lock per statement ----------------------------------------------------

TEST(MarkCategoryDbImplORMHelper, FreeRunningHelpersOnOneConnectionGetTheirOwnIds) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // Two threads, two helpers (two category databases), one connection, behind a start gate. Each records
    // its own (id, name) pairs; afterwards every pair is compared with the database by plain SQL, so an id
    // taken from lastInsertRowId outside the statement's mutex hold (another thread's insert in between)
    // fails. A third thread runs countCategorySql, the one method legacy calls under a read lock, at the same
    // time.
    //
    // T-0305: the first log line of a process builds Qt's message pattern inside libQt6Core (Qt 6.8.2: the ":
    // " literal of the default pattern, a qstrdup block). Qt's own once-only set-up is not visible to TSan,
    // which reported it as a race between the two writers' first ormLogEnter (insertCategorySql ->
    // addOrUpdate, 78 of 200 fresh processes on Debian 13). The test is about ids, not about Qt's logger: log
    // once here, before the threads exist.
    ormLogDebug("T-0305 warm-up of the Qt message pattern before the threads start");
    constexpr int kEach = 60;
    Env env(true);
    env.exec("INSERT INTO Category (ID,cName) VALUES (1000,'seed'),(1001,'seed')");
    auto txn = env.provider().transaction().start(false); // one commit instead of 120 fsyncs
    std::atomic<bool> go{false};
    std::atomic<bool> writersDone{false};
    std::atomic<int> wrongCounts{0};
    std::vector<std::pair<qint64, QString>> mine[2];
    const auto writer = [&](int self, const QString& prefix) {
        MarkCategoryDbImplORMHelper helper(false, env.provider());
        while (!go.load()) {
            std::this_thread::yield();
        }
        for (int i = 0; i < kEach; ++i) {
            OrmCategoryRec r = rec(prefix + QString::number(i));
            ASSERT_TRUE(helper.insertCategorySql(r));
            mine[self].emplace_back(r.categoryId, r.name);
        }
    };
    const auto counter = [&] {
        MarkCategoryDbImplORMHelper helper(false, env.provider());
        while (!go.load()) {
            std::this_thread::yield();
        }
        while (!writersDone.load()) {
            if (helper.countCategorySql(QStringLiteral("seed")) != 2) {
                ++wrongCounts;
            }
        }
    };
    {
        std::thread a(writer, 0, QStringLiteral("a"));
        std::thread b(writer, 1, QStringLiteral("b"));
        std::thread c(counter);
        go = true;
        a.join();
        b.join();
        writersDone = true;
        c.join();
    }
    env.provider().transaction().commit(txn);
    EXPECT_EQ(wrongCounts.load(), 0);
    ASSERT_EQ(mine[0].size(), static_cast<size_t>(kEach));
    ASSERT_EQ(mine[1].size(), static_cast<size_t>(kEach));
    QStringList expected{QStringLiteral("1000|seed"), QStringLiteral("1001|seed")};
    std::vector<std::pair<qint64, QString>> all = mine[0];
    all.insert(all.end(), mine[1].begin(), mine[1].end());
    for (const auto& [id, name] : all) {
        EXPECT_GT(id, 1001);
        expected.append(QStringLiteral("%1|%2").arg(id).arg(name));
    }
    expected.sort();
    QStringList actual = env.rows("SELECT ID,cName FROM Category");
    actual.sort();
    EXPECT_EQ(actual, expected);
}
