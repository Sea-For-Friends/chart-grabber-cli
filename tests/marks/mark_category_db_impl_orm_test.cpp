// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-033, FEAT-GEO-034 (both held open). Ports no legacy test (there is none):
// u_MarkCategoryDbImplORM.pas (docs/specs/marks.md §2.9.6, §2.9.12) against a copy of
// tests/fixtures/marks/Marks.db3 and an empty database. Expected values are literals read back with plain SQL
// on a second connection, never through the unit's own caches. The provider is wrapped in a spy that records
// the transaction calls and counts the accesses to the connection mutex (the proof of the lock modes).
#include "cg/core/read_write_state_internal.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_list.h"
#include "cg/marks/mormot/categories/mark_category_db_impl_orm.h"
#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"
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
#include <semaphore>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

#include "../corpus_skip.h"
#include "fixture_path.h"
#include "mark_test_util.h"

using cg::core::ReadWriteStateInternal;
using cg::marks::IMarkCategory;
using cg::marks::IMarkSystemImplORMClientProvider;
using cg::marks::IMarkSystemORMTransaction;
using cg::marks::MarkCategory;
using cg::marks::MarkCategoryDbImplORM;
using cg::marks::MarkCategoryList;
using cg::marks::MarkCategoryListPtr;
using cg::marks::MarkCategoryPtr;
using cg::marks::MarkSystemImplConfigORM;
using cg::marks::MarkSystemImplORMClientProvider;
using cg::marks::MarkSystemORMError;
using cg::marks::TransactionRec;
using cg::storage::sqlite::SqliteDb;
using cg::storage::sqlite::SqliteStmt;

namespace {

constexpr quintptr kDbId = 7;
constexpr quintptr kOtherDbId = 8;

using cg::marks::testutil::SpyTransaction;

// Counts how often a statement asks for the connection mutex (before it waits for it).
class SpyProvider final : public IMarkSystemImplORMClientProvider {
public:
    explicit SpyProvider(IMarkSystemImplORMClientProvider& real) : real_(real), tx_(real.transaction()) {}
    [[nodiscard]] qint64 userId() const override { return real_.userId(); }
    [[nodiscard]] SqliteDb& db() override { return real_.db(); }
    [[nodiscard]] std::recursive_mutex& dbMutex() override {
        ++mutexRequests;
        requested.release();
        return real_.dbMutex();
    }
    [[nodiscard]] IMarkSystemORMTransaction& transaction() override { return tx_; }
    std::atomic<int> mutexRequests{0};
    std::counting_semaphore<> requested{0}; // one release per request, acquired by the test
    SpyTransaction& spy() { return tx_; }

private:
    IMarkSystemImplORMClientProvider& real_;
    SpyTransaction tx_;
};

class Env {
public:
    explicit Env(bool fromFixture, bool writeAccess = true) {
        EXPECT_TRUE(dir_.isValid());
        const QString path = QDir(dir_.path()).filePath(QStringLiteral("Marks.db3"));
        if (fromFixture) {
            EXPECT_TRUE(QFile::copy(cgMarksFixture(QStringLiteral("marks/Marks.db3")), path));
            EXPECT_TRUE(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
        }
        real_ = std::make_unique<MarkSystemImplORMClientProvider>(
            dir_.path(), MarkSystemImplConfigORM({}, false, {}, {}, {}));
        spy_ = std::make_unique<SpyProvider>(*real_);
        auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
        if (!db) {
            ADD_FAILURE() << db.error().what().toStdString();
            std::abort();
        }
        plain_ = std::make_unique<SqliteDb>(std::move(*db));
        std::ignore = plain_->createCollation("SYSTEMNOCASE", &cg::marks::systemNoCaseCompare);
        state_ = std::make_shared<ReadWriteStateInternal>(true, writeAccess);
    }

    // Created on demand so that a test can seed the file with plain SQL first (cold caches).
    MarkCategoryDbImplORM& db() {
        if (!db_) {
            db_ = std::make_unique<MarkCategoryDbImplORM>(kDbId, state_, *spy_);
            notifySub_ = db_->changeNotifier().subscribe([this] { ++notifications; });
        }
        return *db_;
    }
    ReadWriteStateInternal& state() { return *state_; }
    SpyProvider& spy() { return *spy_; }
    MarkSystemImplORMClientProvider& real() { return *real_; }

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

    int notifications = 0;

private:
    QTemporaryDir dir_;
    std::unique_ptr<MarkSystemImplORMClientProvider> real_;
    std::unique_ptr<SpyProvider> spy_;
    std::unique_ptr<SqliteDb> plain_;
    std::shared_ptr<ReadWriteStateInternal> state_;
    std::unique_ptr<MarkCategoryDbImplORM> db_;
    cg::core::Subscription notifySub_;
};

const QString kTown = QString::fromUtf8("Город");
const QString kTownLower = QString::fromUtf8("город");
const QString kStreet = QString::fromUtf8("Улица");

MarkCategoryPtr cat(const QString& name, bool visible = true, int after = 3, int before = 23) {
    return std::make_shared<const MarkCategory>(name, visible, after, before);
}

MarkCategoryListPtr list(std::vector<MarkCategoryPtr> items) {
    return std::make_shared<const MarkCategoryList>(std::move(items));
}

// "-" for a null entry, else the name
QString show(const MarkCategoryPtr& c) {
    return c ? c->name() : QStringLiteral("-");
}

QStringList show(const MarkCategoryListPtr& l) {
    QStringList out;
    for (int i = 0; l && i < l->count(); ++i) {
        out.append(show(l->item(i)));
    }
    return out;
}

// Seeds `names` as visible categories through the unit (ids 1, 2, ...) and returns the objects.
std::vector<MarkCategoryPtr> seed(Env& env, const QStringList& names) {
    std::vector<MarkCategoryPtr> out;
    for (const QString& n : names) {
        out.push_back(env.db().updateCategory(nullptr, cat(n)));
    }
    env.spy().spy().events.clear();
    env.notifications = 0;
    return out;
}

// A write that really fails: the CategoryView table is gone, so the view insert of a hidden category throws
// after the Category row was inserted and cached.
void breakViewTable(Env& env) {
    env.exec("DROP TABLE CategoryView");
}

} // namespace

// ---- update list: pairing (spec §2.9.6, :291-375) ----------------------------------------------------

namespace {
struct PairCase {
    const char* label;
    QStringList seeded;
    bool oldNull;
    std::vector<int> oldIdx; // index into `seeded`, -1 = null entry, -2 = other database, -3 = vanished id
    bool newNull;
    std::vector<QString> newNames; // empty name = null entry
    QStringList result;            // "-" = null entry; empty with !resultNull = null list
    QStringList rows;              // Category rows "ID|name" afterwards
    int notifications;
};
} // namespace

TEST(MarkCategoryDbImplORM, UpdateCategoryListPairingTable) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    const std::vector<PairCase> cases = {
        {"old shorter: pairs, then inserts against null",
         {"A", "B"},
         false,
         {0},
         false,
         {"A2", "C", "D"},
         {"A2", "C", "D"},
         {"1|A2", "2|B", "3|C", "4|D"},
         1},
        {"old longer: pairs, then deletes against null, results only for the new length",
         {"A", "B", "C"},
         false,
         {0, 1, 2},
         false,
         {"A2"},
         {"A2"},
         {"1|A2"},
         1},
        {"equal length with null entries: delete, insert",
         {"A", "B"},
         false,
         {0, -1},
         false,
         {"", "X"},
         {"-", "X"},
         {"2|B", "3|X"},
         1},
        {"old list null: all inserted",
         {"A"},
         true,
         {},
         false,
         {"P", "Q"},
         {"P", "Q"},
         {"1|A", "2|P", "3|Q"},
         1},
        {"new list null: all old deleted, no result",
         {"A", "B", "C"},
         false,
         {0, 1},
         true,
         {},
         {},
         {"3|C"},
         1},
        {"new list empty (not null): all old deleted, no result",
         {"A", "B"},
         false,
         {0, 1},
         false,
         {},
         {},
         {},
         1},
        {"nothing changes: the re-read objects come back, no notification",
         {"A", "B"},
         false,
         {0, 1},
         false,
         {"A", "B"},
         {"A", "B"},
         {"1|A", "2|B"},
         0},
        {"one of two changes: one notification",
         {"A", "B"},
         false,
         {0, 1},
         false,
         {"A", "B2"},
         {"A", "B2"},
         {"1|A", "2|B2"},
         1},
        {"old entry of another database: nothing happens, the result keeps a nil entry",
         {"A"},
         false,
         {-2},
         false,
         {"X"},
         {"-"},
         {"1|A"},
         0},
        {"old entry vanished: nil entry in the result, the other pair still changes",
         {"A"},
         false,
         {0, -3},
         false,
         {"A2", "Y"},
         {"A2", "-"},
         {"1|A2"},
         1},
    };
    for (const bool fromFixture : {true, false}) {
        for (const PairCase& c : cases) {
            Env env(fromFixture);
            const std::vector<MarkCategoryPtr> objects = seed(env, c.seeded);

            MarkCategoryListPtr oldList;
            if (!c.oldNull) {
                std::vector<MarkCategoryPtr> items;
                for (const int i : c.oldIdx) {
                    if (i == -2) { // an object of another database
                        items.push_back(std::make_shared<const cg::marks::MarkCategoryDbInternalORM>(
                            1, kOtherDbId, QStringLiteral("A"), true, 3, 23));
                    } else if (i == -3) { // an id of this database that does not exist
                        items.push_back(std::make_shared<const cg::marks::MarkCategoryDbInternalORM>(
                            99, kDbId, QStringLiteral("A"), true, 3, 23));
                    } else {
                        items.push_back(i < 0 ? nullptr : objects[static_cast<size_t>(i)]);
                    }
                }
                oldList = list(items);
            }
            MarkCategoryListPtr newList;
            if (!c.newNull) {
                std::vector<MarkCategoryPtr> items;
                for (const QString& n : c.newNames) {
                    items.push_back(n.isEmpty() ? nullptr : cat(n));
                }
                newList = list(items);
            }
            const MarkCategoryListPtr result = env.db().updateCategoryList(oldList, newList);
            EXPECT_EQ(show(result), c.result) << c.label;
            EXPECT_EQ(env.categories(), c.rows) << c.label;
            EXPECT_EQ(env.notifications, c.notifications) << c.label;
            // one transaction around the whole list
            EXPECT_EQ(env.spy().spy().events, (QStringList{"start:rw", "commit"})) << c.label;
        }
    }
}

TEST(MarkCategoryDbImplORM, UpdateCategoryListReturnsObjectsOfThisDbWithTheStoredIds) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    const auto objects = seed(env, {"A", "B"});
    const MarkCategoryListPtr result = env.db().updateCategoryList(
        list({objects[0], nullptr}), list({cat(QStringLiteral("A3")), cat(QStringLiteral("N"))}));
    ASSERT_EQ(result->count(), 2);
    EXPECT_TRUE(env.db().isCategoryFromThisDb(result->item(0)));
    EXPECT_EQ(env.db().getCategoryById(1)->name(), QStringLiteral("A3"));
    EXPECT_EQ(env.db().getCategoryById(3)->name(), QStringLiteral("N"));
    EXPECT_EQ(env.categories(), (QStringList{"1|A3", "2|B", "3|N"}));
}

// ---- update list failures ----------------------------------------------------------------------------

TEST(MarkCategoryDbImplORM, ListPairOfTwoNullsThrowsInsideTheTransactionAndRollsBack) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    seed(env, {});
    // item 0 is inserted, item 1 pairs two nulls: "Old and New is nil!" (:202-204)
    try {
        std::ignore =
            env.db().updateCategoryList(list({nullptr, nullptr}), list({cat(QStringLiteral("P")), nullptr}));
        FAIL() << "no exception";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: Old and New is nil!");
    }
    EXPECT_EQ(env.spy().spy().events, (QStringList{"start:rw", "rollback"}));
    EXPECT_EQ(env.categories(), QStringList{});
    EXPECT_EQ(env.notifications, 0); // the insert of item 0 was real, its notification is lost (:369)
    // the transaction is closed: a new one starts as the owner
    TransactionRec rec = env.real().transaction().start(false);
    EXPECT_TRUE(rec.isInternal);
    env.real().transaction().rollBack(rec);
}

TEST(MarkCategoryDbImplORM, BothListsNullThrowsInsideTheTransactionAndRollsBack) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    seed(env, {"A"});
    EXPECT_THROW(std::ignore = env.db().updateCategoryList(nullptr, nullptr), std::runtime_error);
    EXPECT_EQ(env.spy().spy().events, (QStringList{"start:rw", "rollback"}));
    EXPECT_EQ(env.categories(), QStringList{"1|A"});
    EXPECT_EQ(env.notifications, 0);
    TransactionRec rec = env.real().transaction().start(false);
    EXPECT_TRUE(rec.isInternal);
    env.real().transaction().rollBack(rec);
}

// LC-17 (spec §5, L-213 family): the caches are written before the commit and not undone on a rollback.
TEST(MarkCategoryDbImplORM, FailedInsertRollsBackButTheCategoryCacheKeepsTheRow) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    seed(env, {});
    breakViewTable(env);
    // hidden: Category row, then the CategoryView insert fails (:143-189 of the helper)
    EXPECT_THROW(std::ignore = env.db().updateCategory(nullptr, cat(QStringLiteral("X"), false)),
                 MarkSystemORMError);
    EXPECT_EQ(env.spy().spy().events, (QStringList{"start:rw", "rollback"}));
    EXPECT_EQ(env.categories(), QStringList{}); // rolled back
    EXPECT_EQ(env.notifications, 0);
    // the phantom, from the cache
    const MarkCategoryPtr phantom = env.db().getFirstCategoryByName(QStringLiteral("X"));
    ASSERT_TRUE(phantom);
    EXPECT_EQ(phantom->name(), QStringLiteral("X"));
    EXPECT_EQ(env.db().getCategoryById(1)->name(), QStringLiteral("X"));
    // the list reads the table (cache not prepared) and replaces the cache: the phantom is gone
    EXPECT_FALSE(env.db().categoriesList());
    EXPECT_FALSE(env.db().getFirstCategoryByName(QStringLiteral("X")));
}

TEST(MarkCategoryDbImplORM, FailedListRollsBackEveryItemWithoutNotificationButTheCacheKeepsThem) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    seed(env, {});
    breakViewTable(env);
    // item 0 (visible: no view row needed) succeeds, item 1 (hidden) fails
    EXPECT_THROW(std::ignore = env.db().updateCategoryList(
                     nullptr, list({cat(QStringLiteral("P")), cat(QStringLiteral("Q"), false)})),
                 MarkSystemORMError);
    EXPECT_EQ(env.spy().spy().events, (QStringList{"start:rw", "rollback"}));
    EXPECT_EQ(env.categories(), QStringList{});
    EXPECT_EQ(env.notifications, 0); // item 0 changed the database for a while, VDoNotify is lost
    ASSERT_TRUE(env.db().getFirstCategoryByName(QStringLiteral("P")));
    EXPECT_EQ(env.db().getFirstCategoryByName(QStringLiteral("P"))->name(), QStringLiteral("P"));
}

// ---- update category ---------------------------------------------------------------------------------

namespace {
struct SingleCase {
    const char* label;
    int oldKind; // 0 null, 1 seeded A (id 1), 2 object of another db, 3 id 99 of this db, 4 plain
                 // MarkCategory
    bool hasNew;
    QString newName;
    bool newVisible;
    int newAfter;
    QString result;    // "-" = null
    QStringList rows;  // Category afterwards
    QStringList views; // CategoryView afterwards
    int notifications;
};
} // namespace

TEST(MarkCategoryDbImplORM, UpdateCategoryTable) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    const std::vector<SingleCase> cases = {
        {"insert visible", 0, true, "N", true, 3, "N", {"1|A", "2|N"}, {}, 1},
        {"insert hidden", 0, true, "N", false, 5, "N", {"1|A", "2|N"}, {"1|1|2|0|5|23"}, 1},
        {"update rename", 1, true, "A2", true, 3, "A2", {"1|A2"}, {}, 1},
        {"update hide with zoom", 1, true, "A", false, 6, "A", {"1|A"}, {"1|1|1|0|6|23"}, 1},
        {"update equal: the re-read object, nothing changed", 1, true, "A", true, 3, "A", {"1|A"}, {}, 0},
        {"delete", 1, false, "", true, 3, "-", {}, {}, 1},
        {"delete of an object of another db: nothing", 2, false, "", true, 3, "-", {"1|A"}, {}, 0},
        {"delete of an unknown id of this db: the helper reports success (zero rows is a success)",
         3,
         false,
         "",
         true,
         3,
         "-",
         {"1|A"},
         {},
         1},
        {"delete of a plain category: nothing", 4, false, "", true, 3, "-", {"1|A"}, {}, 0},
        {"update of an object of another db: nothing, no result", 2, true, "Z", true, 3, "-", {"1|A"}, {}, 0},
        {"update of a plain category: nothing, no result", 4, true, "Z", true, 3, "-", {"1|A"}, {}, 0},
        {"update of a vanished id: nothing, no result", 3, true, "Z", true, 3, "-", {"1|A"}, {}, 0},
        {"non-ASCII insert",
         0,
         true,
         "\xD0\x93\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB4",
         true,
         3,
         "\xD0\x93\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB4",
         {"1|A", "2|\xD0\x93\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB4"},
         {},
         1},
    };
    for (const bool fromFixture : {true, false}) {
        for (const SingleCase& c : cases) {
            Env env(fromFixture);
            const auto seeded = seed(env, {"A"});
            MarkCategoryPtr oldCat;
            switch (c.oldKind) {
            case 1:
                oldCat = seeded[0];
                break;
            case 2:
                oldCat = std::make_shared<const cg::marks::MarkCategoryDbInternalORM>(
                    1, kOtherDbId, QStringLiteral("A"), true, 3, 23);
                break;
            case 3:
                oldCat = std::make_shared<const cg::marks::MarkCategoryDbInternalORM>(
                    99, kDbId, QStringLiteral("A"), true, 3, 23);
                break;
            case 4:
                oldCat = cat(QStringLiteral("A"));
                break;
            default:
                break;
            }
            const MarkCategoryPtr newCat =
                c.hasNew ? cat(QString::fromUtf8(c.newName.toUtf8()), c.newVisible, c.newAfter) : nullptr;
            const MarkCategoryPtr result = env.db().updateCategory(oldCat, newCat);
            EXPECT_EQ(show(result), QString::fromUtf8(c.result.toUtf8())) << c.label;
            QStringList rows;
            for (const QString& r : c.rows) {
                rows.append(QString::fromUtf8(r.toUtf8()));
            }
            EXPECT_EQ(env.categories(), rows) << c.label;
            EXPECT_EQ(env.views(), c.views) << c.label;
            EXPECT_EQ(env.notifications, c.notifications) << c.label;
            // always one transaction, committed
            EXPECT_EQ(env.spy().spy().events, (QStringList{"start:rw", "commit"})) << c.label;
        }
    }
}

TEST(MarkCategoryDbImplORM, UpdateCategoryResultCarriesTheStoredIdAndThisDb) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    const auto seeded = seed(env, {"A"});
    const MarkCategoryPtr renamed =
        env.db().updateCategory(seeded[0], cat(QStringLiteral("A2"), false, 4, 9));
    const auto* internal = dynamic_cast<const cg::marks::IMarkCategoryInternalORM*>(renamed.get());
    ASSERT_NE(internal, nullptr);
    EXPECT_EQ(internal->id(), 1);
    EXPECT_EQ(internal->dbId(), kDbId);
    EXPECT_FALSE(renamed->visible());
    EXPECT_EQ(renamed->afterScale(), 4);
    EXPECT_EQ(renamed->beforeScale(), 9);
}

TEST(MarkCategoryDbImplORM, UpdateCategoryOfTwoNullsThrowsBeforeTheTransaction) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    seed(env, {});
    try {
        std::ignore = env.db().updateCategory(nullptr, nullptr);
        FAIL() << "no exception";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: Old and New is nil!");
    }
    EXPECT_TRUE(env.spy().spy().events.isEmpty()); // thrown at :202, the transaction starts at :212
    EXPECT_EQ(env.notifications, 0);
}

TEST(MarkCategoryDbImplORM, AfterScaleAndBeforeScaleAreStoredAsBytes) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    seed(env, {});
    std::ignore = env.db().updateCategory(nullptr, cat(QStringLiteral("Z"), false, 5, 300));
    EXPECT_EQ(env.views(), QStringList{"1|1|1|0|5|44"}); // 300 mod 256
}

// ---- set all visible ---------------------------------------------------------------------------------

TEST(MarkCategoryDbImplORM, SetAllCategoriesVisibleOneTransactionOneNotification) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    for (const bool fromFixture : {true, false}) {
        {
            Env env(fromFixture);
            seed(env, {"A", "B"});
            env.db().setAllCategoriesVisible(false);
            EXPECT_EQ(env.views(), (QStringList{"1|1|1|0|3|23", "2|1|2|0|3|23"}));
            EXPECT_EQ(env.notifications, 1);
            EXPECT_EQ(env.spy().spy().events, (QStringList{"start:rw", "commit"}));
        }
        { // no categories: still a notification (:447 is unconditional)
            Env env(fromFixture);
            seed(env, {});
            env.db().setAllCategoriesVisible(true);
            EXPECT_EQ(env.views(), QStringList{});
            EXPECT_EQ(env.notifications, 1);
            EXPECT_EQ(env.spy().spy().events, (QStringList{"start:rw", "commit"}));
        }
    }
}

TEST(MarkCategoryDbImplORM, SetAllCategoriesVisibleFailureRollsBackWithoutNotification) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    seed(env, {"A"});
    breakViewTable(env);
    EXPECT_THROW(env.db().setAllCategoriesVisible(false), MarkSystemORMError);
    EXPECT_EQ(env.spy().spy().events, (QStringList{"start:rw", "rollback"}));
    EXPECT_EQ(env.notifications, 0);
}

// ---- read-only mode per method -----------------------------------------------------------------------

TEST(MarkCategoryDbImplORM, ReadOnlyModePerMethod) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true, false);
    env.exec("INSERT INTO Category (cName) VALUES ('A')");

    // insert: the helper refuses, the transaction is the read-only one (no BEGIN)
    EXPECT_FALSE(env.db().updateCategory(nullptr, cat(QStringLiteral("N"))));
    EXPECT_EQ(env.spy().spy().events, (QStringList{"start:ro", "commit"}));
    EXPECT_EQ(env.notifications, 0);
    EXPECT_EQ(env.categories(), QStringList{"1|A"});

    // the list: one read-only transaction
    env.spy().spy().events.clear();
    EXPECT_FALSE(env.db().updateCategoryList(nullptr, list({cat(QStringLiteral("N"))}))->item(0));
    EXPECT_EQ(env.spy().spy().events, (QStringList{"start:ro", "commit"}));
    EXPECT_EQ(env.categories(), QStringList{"1|A"});

    // delete: refused
    env.spy().spy().events.clear();
    const MarkCategoryPtr a = env.db().getCategoryById(1);
    ASSERT_TRUE(a);
    EXPECT_FALSE(env.db().updateCategory(a, nullptr));
    EXPECT_EQ(env.categories(), QStringList{"1|A"});
    EXPECT_EQ(env.notifications, 0);

    // a visibility toggle works in memory only; the call still notifies once
    env.spy().spy().events.clear();
    env.db().setAllCategoriesVisible(false);
    EXPECT_EQ(env.spy().spy().events, (QStringList{"start:ro", "commit"}));
    EXPECT_EQ(env.notifications, 1);
    EXPECT_EQ(env.views(), QStringList{});

    // reads need no transaction at all
    env.spy().spy().events.clear();
    EXPECT_EQ(env.db().getFirstCategoryByName(QStringLiteral("A"))->name(), QStringLiteral("A"));
    EXPECT_EQ(env.db().getCategoryByNameCount(QStringLiteral("A")), 1);
    EXPECT_EQ(show(env.db().categoriesList()), QStringList{"A"});
    EXPECT_TRUE(env.spy().spy().events.isEmpty());
}

TEST(MarkCategoryDbImplORM, WriteAccessToggleSwitchesTheHelperAndTheTransactionMode) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true, false);
    EXPECT_FALSE(env.db().updateCategory(nullptr, cat(QStringLiteral("N"))));
    env.state().setWriteAccess(true); // state change -> onStateChange (:157-165)
    env.spy().spy().events.clear();
    EXPECT_EQ(show(env.db().updateCategory(nullptr, cat(QStringLiteral("N")))), QStringLiteral("N"));
    EXPECT_EQ(env.spy().spy().events, (QStringList{"start:rw", "commit"}));
    EXPECT_EQ(env.categories(), QStringList{"1|N"});
    EXPECT_EQ(env.notifications, 1); // the state change itself does not notify
    env.state().setWriteAccess(false);
    env.spy().spy().events.clear();
    EXPECT_FALSE(env.db().updateCategory(nullptr, cat(QStringLiteral("M"))));
    EXPECT_EQ(env.spy().spy().events, (QStringList{"start:ro", "commit"}));
    EXPECT_EQ(env.categories(), QStringList{"1|N"});
}

TEST(MarkCategoryDbImplORM, NullStateIsRefused) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    EXPECT_THROW(MarkCategoryDbImplORM(kDbId, nullptr, env.spy()), std::invalid_argument);
}

// ---- readers (cold caches: the file is seeded with plain SQL before the unit exists) -----------------

TEST(MarkCategoryDbImplORM, ReadersOnColdCachesAndNoTransactions) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    env.exec("INSERT INTO Category (cName) VALUES ('Zeta'),('Город'),('Улица')");
    env.exec("INSERT INTO CategoryView (cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) VALUES (1,3,0,6,7)");
    {
        // by id
        const MarkCategoryPtr c = env.db().getCategoryById(3);
        ASSERT_TRUE(c);
        EXPECT_EQ(c->name(), kStreet);
        EXPECT_FALSE(c->visible());
        EXPECT_EQ(c->afterScale(), 6);
        EXPECT_EQ(c->beforeScale(), 7);
        EXPECT_TRUE(env.db().isCategoryFromThisDb(c));
    }
    EXPECT_FALSE(env.db().getCategoryById(77));
    // by name, both spellings of the interface: cache not prepared -> SQL, where SYSTEMNOCASE folds ASCII
    // only (LC-12 / L-187: Cyrillic differing in case is a different name for SQL)
    EXPECT_FALSE(env.db().firstCategoryByName(kTownLower));
    EXPECT_EQ(env.db().firstCategoryByName(kTown)->name(), kTown);
    EXPECT_EQ(env.db().getFirstCategoryByName(QStringLiteral("zETA"))->name(), QStringLiteral("Zeta"));
    EXPECT_FALSE(env.db().firstCategoryByName(QStringLiteral("nope")));
    // the count always asks SQL
    EXPECT_EQ(env.db().categoryByNameCount(kTownLower), 0);
    EXPECT_EQ(env.db().categoryByNameCount(kTown), 1);
    EXPECT_EQ(env.db().getCategoryByNameCount(QStringLiteral("ZETA")), 1);
    EXPECT_EQ(env.db().getCategoryByNameCount(QStringLiteral("nope")), 0);
    // once the list prepared the cache, the cache answers by AnsiSameText (the stored spelling comes back)
    EXPECT_EQ(show(env.db().categoriesList()), (QStringList{"Zeta", kTown, kStreet}));
    EXPECT_EQ(env.db().firstCategoryByName(kTownLower)->name(), kTown);
    EXPECT_EQ(env.db().categoryByNameCount(kTownLower), 0); // still SQL
    EXPECT_TRUE(env.spy().spy().events.isEmpty());
    EXPECT_EQ(env.notifications, 0);
}

TEST(MarkCategoryDbImplORM, CategoriesListIsInIdOrderWithViewValuesOrDefaults) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    for (const bool empty : {false, true}) {
        Env env(true);
        if (!empty) {
            env.exec("INSERT INTO Category (ID,cName) VALUES (9,'c'),(2,'a'),(5,'b')");
            env.exec("INSERT INTO CategoryView (cvUser,cvCategory,cvVisible,cvMinZoom,cvMaxZoom) VALUES "
                     "(1,5,0,6,7)");
        }
        const MarkCategoryListPtr l = env.db().categoriesList();
        if (empty) {
            EXPECT_FALSE(l);
            continue;
        }
        ASSERT_TRUE(l);
        EXPECT_EQ(show(l), (QStringList{"a", "b", "c"}));
        EXPECT_TRUE(l->item(0)->visible());
        EXPECT_FALSE(l->item(1)->visible());
        EXPECT_EQ(l->item(1)->afterScale(), 6);
        EXPECT_EQ(l->item(2)->beforeScale(), 23);
        EXPECT_TRUE(env.spy().spy().events.isEmpty());
    }
}

TEST(MarkCategoryDbImplORM, IsCategoryFromThisDbTable) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    const auto own = std::make_shared<const cg::marks::MarkCategoryDbInternalORM>(
        1, kDbId, QStringLiteral("a"), true, 3, 23);
    const auto foreign = std::make_shared<const cg::marks::MarkCategoryDbInternalORM>(
        1, kOtherDbId, QStringLiteral("a"), true, 3, 23);
    EXPECT_TRUE(env.db().isCategoryFromThisDb(own));
    EXPECT_FALSE(env.db().isCategoryFromThisDb(foreign));
    EXPECT_FALSE(env.db().isCategoryFromThisDb(cat(QStringLiteral("a"))));
    EXPECT_FALSE(env.db().isCategoryFromThisDb(nullptr));
}

// ---- lock modes (spec §2.9.12): observed through the accesses to the connection mutex ----------------

namespace {

constexpr std::chrono::seconds kLimit{10};       // a failure bound, never reached by a passing run
constexpr std::chrono::milliseconds kStill{100}; // how long "still blocked" is observed

} // namespace

// Two readers of the count (read lock, :425) both get past the database lock and meet at the held connection
// mutex. Repeated 1000 times on fresh threads.
TEST(MarkCategoryDbImplORM, CountByNameRunsUnderTheReadLockSoReadersOverlap) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    env.db(); // construct
    for (int iteration = 0; iteration < 1000; ++iteration) {
        std::unique_lock<std::recursive_mutex> hold(env.real().dbMutex());
        std::thread a([&] { EXPECT_EQ(env.db().getCategoryByNameCount(QStringLiteral("x")), 0); });
        std::thread b([&] { EXPECT_EQ(env.db().getCategoryByNameCount(QStringLiteral("y")), 0); });
        const bool first = env.spy().requested.try_acquire_for(kLimit);
        const bool second = env.spy().requested.try_acquire_for(kLimit);
        hold.unlock();
        a.join();
        b.join();
        ASSERT_TRUE(first && second) << "iteration " << iteration;
    }
}

// Two readers by id (write lock, :404) never overlap: the second waits for the database lock, so only one
// reaches the connection mutex while the first is held there.
TEST(MarkCategoryDbImplORM, GettersRunUnderTheWriteLockSoTheyExcludeEachOther) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    env.db();
    std::unique_lock<std::recursive_mutex> hold(env.real().dbMutex());
    std::thread a([&] { std::ignore = env.db().getCategoryById(5); });
    ASSERT_TRUE(env.spy().requested.try_acquire_for(kLimit));
    std::thread b([&] { std::ignore = env.db().getFirstCategoryByName(QStringLiteral("y")); });
    EXPECT_FALSE(env.spy().requested.try_acquire_for(kStill));
    hold.unlock();
    a.join();
    b.join();
}

// A count (read lock) waits for a writer that holds the database lock.
TEST(MarkCategoryDbImplORM, ACountWaitsForAWriter) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    env.db();
    std::unique_lock<std::recursive_mutex> hold(env.real().dbMutex());
    std::thread a([&] { std::ignore = env.db().getCategoryById(5); });
    ASSERT_TRUE(env.spy().requested.try_acquire_for(kLimit));
    std::thread b([&] { std::ignore = env.db().getCategoryByNameCount(QStringLiteral("y")); });
    EXPECT_FALSE(env.spy().requested.try_acquire_for(kStill));
    hold.unlock();
    a.join();
    b.join();
}

// The write lock of the four other public methods: a reader (the count, read lock) is parked at the held
// connection mutex; the method called on a second thread must wait for it, so it neither starts a
// transaction nor asks for the mutex. (With the read lock it would do both at once.)
TEST(MarkCategoryDbImplORM, UpdatersAndTheListTakeTheWriteLock) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    struct Method {
        const char* label;
        std::function<void(Env&)> call;
    };
    const std::vector<Method> methods = {
        {"updateCategory",
         [](Env& e) { std::ignore = e.db().updateCategory(nullptr, cat(QStringLiteral("N"))); }},
        {"updateCategoryList",
         [](Env& e) { std::ignore = e.db().updateCategoryList(nullptr, list({cat(QStringLiteral("N"))})); }},
        {"setAllCategoriesVisible", [](Env& e) { e.db().setAllCategoriesVisible(false); }},
        {"categoriesList", [](Env& e) { std::ignore = e.db().categoriesList(); }},
    };
    for (const Method& m : methods) {
        Env env(true);
        env.db();
        std::unique_lock<std::recursive_mutex> hold(env.real().dbMutex());
        std::thread reader([&] { std::ignore = env.db().getCategoryByNameCount(QStringLiteral("p")); });
        ASSERT_TRUE(env.spy().requested.try_acquire_for(kLimit)) << m.label;
        std::thread writer([&] { m.call(env); });
        EXPECT_FALSE(env.spy().requested.try_acquire_for(kStill)) << m.label;
        EXPECT_EQ(env.spy().spy().starts.load(), 0) << m.label;
        hold.unlock();
        reader.join();
        writer.join();
        EXPECT_GE(env.spy().mutexRequests.load(), 2) << m.label;
    }
}

// A subscriber of the change notification may call back into the object: the notification fires after the
// write lock is released.
TEST(MarkCategoryDbImplORM, ASubscriberMayCallBackFromTheNotification) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env(true);
    MarkCategoryPtr seen;
    const cg::core::Subscription sub =
        env.db().changeNotifier().subscribe([&] { seen = env.db().getCategoryById(1); });
    std::ignore = env.db().updateCategory(nullptr, cat(QStringLiteral("N")));
    ASSERT_TRUE(seen);
    EXPECT_EQ(seen->name(), QStringLiteral("N"));
    seen.reset();
    env.db().setAllCategoriesVisible(false);
    ASSERT_TRUE(seen);
}
