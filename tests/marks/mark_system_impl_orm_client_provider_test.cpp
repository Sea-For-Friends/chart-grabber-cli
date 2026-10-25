// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-034 (held open). Ports no legacy test (there is none): the connection bootstrap
// and the transaction object against the real Marks.db3 written by Windows SAS.Planet
// (tests/fixtures/marks/Marks.db3), docs/specs/marks.md §2.9.3 B-1...B-14 and §2.9.5.
#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"
#include "cg/marks/mormot/mark_system_orm_transaction.h"
#include "cg/marks/mormot/mark_system_orm_types.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <functional>
#include <mutex>
#include <semaphore>
#include <thread>

#include "../corpus_skip.h"
#include "fixture_path.h"

using cg::marks::IMarkSystemImplConfigORM;
using cg::marks::MarkSystemImplORMClientProvider;
using cg::marks::MarkSystemORMError;
using cg::marks::TransactionRec;
using cg::storage::sqlite::SqliteDb;
using cg::storage::sqlite::SqliteStmt;

namespace {

class TestConfig final : public IMarkSystemImplConfigORM {
public:
    TestConfig(QString fileName, bool readOnly, QString userName)
        : fileName_(std::move(fileName)), readOnly_(readOnly), userName_(std::move(userName)) {}
    QString fileName() const override { return fileName_; }
    bool isReadOnly() const override { return readOnly_; }
    QString userName() const override { return userName_; }
    QString password() const override { return {}; }
    QString passwordPlain() const override { return {}; }
    quint32 cacheSizeMb() const override { return 100; }
    QString forcedSchemaName() const override { return {}; }

private:
    QString fileName_;
    bool readOnly_;
    QString userName_;
};

QStringList rows(SqliteDb& db, QByteArrayView sql) {
    QStringList out;
    auto stmt = db.prepare(sql);
    if (!stmt.has_value()) {
        ADD_FAILURE() << stmt.error().what().toStdString();
        return out;
    }
    for (auto step = stmt->step(); step.has_value() && *step == SqliteStmt::StepResult::Row;
         step = stmt->step()) {
        QStringList columns;
        for (int i = 0; i < stmt->columnCount(); ++i) {
            columns.append(stmt->columnText(i));
        }
        out.append(columns.join(QLatin1Char('|')));
    }
    return out;
}

SqliteDb openPlain(const QString& path) {
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
    if (!db.has_value()) {
        ADD_FAILURE() << db.error().what().toStdString();
        std::abort();
    }
    return std::move(*db);
}

QString message(const std::function<void()>& call) {
    try {
        call();
    } catch (const MarkSystemORMError& e) {
        return QString::fromStdString(e.what());
    }
    return {};
}

const QByteArrayView kMaster = "SELECT type,name,tbl_name,rootpage,sql FROM sqlite_master ORDER BY rowid";

} // namespace

TEST(MarkSystemImplORMClientProvider, FixtureReopensUnchanged) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString copy = QDir(dir.path()).filePath(QStringLiteral("Marks.db3"));
    ASSERT_TRUE(QFile::copy(cgMarksFixture(QStringLiteral("marks/Marks.db3")), copy));
    ASSERT_TRUE(QFile::setPermissions(copy, QFile::ReadOwner | QFile::WriteOwner));
    QStringList masterBefore;
    QStringList usersBefore;
    QStringList versionBefore;
    QStringList pagesBefore;
    {
        SqliteDb db = openPlain(copy);
        masterBefore = rows(db, kMaster);
        usersBefore = rows(db, "SELECT ID,uName FROM User");
        versionBefore = rows(db, "PRAGMA schema_version");
        pagesBefore = rows(db, "PRAGMA page_count");
    }
    {
        MarkSystemImplORMClientProvider provider(dir.path(), TestConfig({}, false, {}));
        EXPECT_EQ(provider.userId(), 1);
    }
    SqliteDb db = openPlain(copy);
    EXPECT_EQ(rows(db, kMaster), masterBefore);
    EXPECT_EQ(rows(db, "SELECT ID,uName FROM User"), usersBefore);
    EXPECT_EQ(rows(db, "PRAGMA schema_version"), versionBefore);
    EXPECT_EQ(rows(db, "PRAGMA page_count"), pagesBefore);
}

TEST(MarkSystemImplORMClientProvider, FreshFileGetsSchemaAndDefaultUser) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("Marks.db3"));
    {
        MarkSystemImplORMClientProvider provider(dir.path(), TestConfig({}, false, {}));
        EXPECT_EQ(provider.userId(), 1);
    }
    SqliteDb fresh = openPlain(path);
    const QString fixture = QDir(dir.path()).filePath(QStringLiteral("fixture.db3"));
    ASSERT_TRUE(QFile::copy(cgMarksFixture(QStringLiteral("marks/Marks.db3")), fixture));
    ASSERT_TRUE(QFile::setPermissions(fixture, QFile::ReadOwner | QFile::WriteOwner));
    SqliteDb legacy = openPlain(fixture);
    const QStringList expected = rows(legacy, kMaster);
    ASSERT_EQ(expected.size(), 32);
    EXPECT_EQ(rows(fresh, kMaster), expected);
    for (const char* pragma : {"page_size", "journal_mode", "encoding"}) {
        const QByteArray sql = QByteArray("PRAGMA ") + pragma;
        EXPECT_EQ(rows(fresh, sql), rows(legacy, sql)) << pragma;
    }
    EXPECT_EQ(rows(fresh, "SELECT ID,uName FROM User"), QStringList{QStringLiteral("1|sasgis")});
}

TEST(MarkSystemImplORMClientProvider, SecondUserNameAddsRowTwo) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("Marks.db3"));
    {
        MarkSystemImplORMClientProvider provider(dir.path(), TestConfig({}, false, {}));
        EXPECT_EQ(provider.userId(), 1);
    }
    {
        MarkSystemImplORMClientProvider provider(dir.path(), TestConfig({}, false, QStringLiteral("Second")));
        EXPECT_EQ(provider.userId(), 2);
    }
    {
        // SYSTEMNOCASE: the same name in another case is the same user; a read-only open finds it.
        MarkSystemImplORMClientProvider provider(dir.path(), TestConfig({}, true, QStringLiteral("SECOND")));
        EXPECT_EQ(provider.userId(), 2);
    }
    SqliteDb db = openPlain(path);
    EXPECT_EQ(rows(db, "SELECT ID,uName FROM User ORDER BY ID"),
              (QStringList{QStringLiteral("1|sasgis"), QStringLiteral("2|Second")}));
}

TEST(MarkSystemImplORMClientProvider, ThreeErrorMessages) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString missingDir = QDir(dir.path()).filePath(QStringLiteral("nope"));
    EXPECT_EQ(message([&] {
                  MarkSystemImplORMClientProvider p(
                      dir.path(), TestConfig(missingDir + QStringLiteral("/m.db3"), false, {}));
              }),
              QStringLiteral("Directory does not exists: ") + missingDir);
    const QString file = QDir(dir.path()).filePath(QStringLiteral("Marks.db3"));
    EXPECT_EQ(message([&] { MarkSystemImplORMClientProvider p(dir.path(), TestConfig({}, true, {})); }),
              QStringLiteral("File does not exists: ") + file);
    {
        MarkSystemImplORMClientProvider p(dir.path(), TestConfig({}, false, {}));
    }
    EXPECT_EQ(message([&] {
                  MarkSystemImplORMClientProvider p(dir.path(),
                                                    TestConfig({}, true, QStringLiteral("other")));
              }),
              QStringLiteral("MarkSystemORM: Can't init User in read-only mode!"));
}

TEST(MarkSystemImplORMClientProvider, FileNameResolution) { // B-1
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path(); // T
    ASSERT_TRUE(QDir(root).mkpath(QStringLiteral("base/sub/inner")));
    ASSERT_TRUE(QDir(root).mkpath(QStringLiteral("sub")));
    const auto make = [&](const QString& basePath, const QString& fileName) {
        MarkSystemImplORMClientProvider p(basePath, TestConfig(fileName, false, {}));
    };
    const auto exists = [&](const char* relativeToRoot) {
        return QFile::exists(root + QLatin1Char('/') + QString::fromLatin1(relativeToRoot));
    };
    // Legacy's default FullPath has NO trailing delimiter (Test/u_PathConfig_Test.pas:115,149-157:
    // PathCombine(base\, '.') drops it); the cases with a delimiter are the explicit-delimiter corner
    // (an absolute configured path is kept verbatim, u_PathConfig.pas:262). The default case follows.
    const QString base = root + QStringLiteral("/base/");
    make(base, {});
    EXPECT_TRUE(exists("base/Marks.db3"));
    make(base, QStringLiteral("plain.db3"));
    EXPECT_TRUE(exists("base/plain.db3"));
    make(base, QStringLiteral("sub/Marks.db3"));
    EXPECT_TRUE(exists("base/sub/Marks.db3"));
    make(base, QStringLiteral("../x.db3"));
    EXPECT_TRUE(exists("x.db3"));
    make(base, QStringLiteral("sub/inner/"));
    EXPECT_TRUE(exists("base/sub/inner/Marks.db3"));
    make(base, root + QStringLiteral("/abs.db3"));
    EXPECT_TRUE(exists("abs.db3"));
    // Legacy defect: a base path without the trailing delimiter loses its last component
    // (RelativeToAbsolutePath applies ExtractFilePath to it): "<T>/base" + "sub/n.db3" = "<T>/sub/n.db3".
    make(root + QStringLiteral("/base"), QStringLiteral("sub/n.db3"));
    EXPECT_TRUE(exists("sub/n.db3"));
    EXPECT_FALSE(exists("base/sub/n.db3"));
    // ... while a name without a directory part is used against the base path itself.
    make(root + QStringLiteral("/base"), QStringLiteral("q.db3"));
    EXPECT_TRUE(exists("base/q.db3"));
}

TEST(MarkSystemImplORMClientProvider, ZeroByteReadOnlyStillOpensReadWriteAndSetsWal) { // B-3, B-8, LC-18
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("Marks.db3"));
    {
        QFile empty(path);
        ASSERT_TRUE(empty.open(QIODevice::WriteOnly));
    }
    EXPECT_EQ(message([&] { MarkSystemImplORMClientProvider p(dir.path(), TestConfig({}, true, {})); }),
              QStringLiteral("MarkSystemORM: Can't init User in read-only mode!"));
    SqliteDb db = openPlain(path);
    EXPECT_TRUE(rows(db, "SELECT name FROM sqlite_master").isEmpty());              // read-only: no schema
    EXPECT_EQ(rows(db, "PRAGMA journal_mode"), QStringList{QStringLiteral("wal")}); // converted anyway
}

TEST(MarkSystemImplORMClientProvider, FailedCommitIsSwallowed) { // L-184
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    MarkSystemImplORMClientProvider provider(dir.path(), TestConfig({}, false, {}));
    auto& db = provider.db();
    for (const char* sql : {"PRAGMA foreign_keys=ON", "CREATE TABLE p (id INTEGER PRIMARY KEY)",
                            "CREATE TABLE c (pid REFERENCES p(id) DEFERRABLE INITIALLY DEFERRED)"}) {
        ASSERT_TRUE(db.exec(sql).has_value()) << sql;
    }
    auto& tx = provider.transaction();
    TransactionRec rec = tx.start(false);
    ASSERT_TRUE(db.exec("INSERT INTO c VALUES (7)").has_value());
    EXPECT_NO_THROW(tx.commit(rec)); // the deferred constraint makes COMMIT fail; legacy hides it
    EXPECT_EQ(rec.sessionId, 0U);
    // SQLite still holds the transaction open, the object believes it is closed.
    EXPECT_TRUE(db.exec("ROLLBACK TRANSACTION;").has_value());
}

TEST(MarkSystemImplORMClientProvider, RefusedBeginDoesNotWedgeTheObject) { // D-79
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    MarkSystemImplORMClientProvider provider(dir.path(), TestConfig({}, false, {}));
    auto& tx = provider.transaction();
    ASSERT_TRUE(provider.db().exec("CREATE TABLE t (x)").has_value());
    ASSERT_TRUE(provider.db().exec("BEGIN TRANSACTION;").has_value());
    EXPECT_EQ(message([&] { (void)tx.start(false); }),
              QStringLiteral("MarkSystemORM: Start transaction is failed!"));
    ASSERT_TRUE(provider.db().exec("ROLLBACK TRANSACTION;").has_value());
    // legacy would now join forever and never commit; the port begins and commits a real transaction
    TransactionRec rec = tx.start(false);
    EXPECT_TRUE(rec.isInternal);
    ASSERT_TRUE(provider.db().exec("INSERT INTO t VALUES (1)").has_value());
    tx.commit(rec);
    SqliteDb other = openPlain(QDir(dir.path()).filePath(QStringLiteral("Marks.db3")));
    EXPECT_EQ(rows(other, "SELECT x FROM t").size(), 1);
}

TEST(MarkSystemImplORMClientProvider, JoinerRowVanishesWithOwnersRollbackAcrossThreads) { // LC-15
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    MarkSystemImplORMClientProvider provider(dir.path(), TestConfig({}, false, {}));
    ASSERT_TRUE(provider.db().exec("CREATE TABLE t (x)").has_value());
    auto& tx = provider.transaction();
    const auto locked = [&](QByteArrayView sql) {
        const std::lock_guard lock(provider.dbMutex());
        EXPECT_TRUE(provider.db().exec(sql).has_value());
    };
    for (int round = 0; round < 1000; ++round) {
        std::binary_semaphore ownerOpen(0);
        std::binary_semaphore joinerDone(0);
        TransactionRec joined;
        std::thread joiner([&] {
            ownerOpen.acquire();
            joined = tx.start(false); // while thread A's transaction is open
            locked("INSERT INTO t VALUES (2)");
            tx.commit(joined); // no-op for a joiner
            joinerDone.release();
        });
        TransactionRec owner = tx.start(false);
        locked("INSERT INTO t VALUES (1)");
        ownerOpen.release();
        joinerDone.acquire();
        {
            const std::lock_guard lock(provider.dbMutex());
            EXPECT_EQ(rows(provider.db(), "SELECT x FROM t").size(), 2); // both rows inside the transaction
        }
        tx.rollBack(owner);
        joiner.join();
        // the overlap happened: the joiner saw the owner's transaction and committed nothing
        ASSERT_TRUE(owner.isInternal);
        ASSERT_FALSE(joined.isInternal);
        ASSERT_NE(joined.sessionId, 0U);
        const std::lock_guard lock(provider.dbMutex());
        ASSERT_TRUE(rows(provider.db(), "SELECT x FROM t").isEmpty());
    }
}

TEST(MarkSystemImplORMClientProvider, TransactionMatrix) { // §2.9.5
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("Marks.db3"));
    MarkSystemImplORMClientProvider provider(dir.path(), TestConfig({}, false, {}));
    auto& tx = provider.transaction();
    ASSERT_TRUE(provider.db().exec("CREATE TABLE t (x)").has_value());
    SqliteDb other = openPlain(path); // a second connection sees committed data only
    const auto visible = [&] { return rows(other, "SELECT x FROM t").size(); };
    const auto insert = [&](int v) {
        ASSERT_TRUE(provider.db().exec("INSERT INTO t VALUES (" + QByteArray::number(v) + ")").has_value());
    };

    // read-only: does nothing, commit and rollback of it do nothing
    TransactionRec ro = tx.start(true);
    EXPECT_TRUE(ro.isReadOnly);
    EXPECT_FALSE(ro.isInternal);
    tx.commit(ro);
    tx.rollBack(ro);

    // owner commit
    TransactionRec a = tx.start(false);
    EXPECT_TRUE(a.isInternal);
    EXPECT_NE(a.sessionId, 0U);
    insert(1);
    EXPECT_EQ(visible(), 0U);
    tx.commit(a);
    EXPECT_EQ(a.sessionId, 0U);
    EXPECT_EQ(visible(), 1U);

    // owner rollback
    a = tx.start(false);
    insert(2);
    tx.rollBack(a);
    EXPECT_EQ(a.sessionId, 0U);
    EXPECT_EQ(visible(), 1U);

    // a nested start joins; the joiner's commit / rollback do nothing; the owner decides
    a = tx.start(false);
    TransactionRec b = tx.start(false);
    EXPECT_FALSE(b.isInternal);
    EXPECT_NE(b.sessionId, 0U);
    insert(3);
    tx.commit(b);
    EXPECT_EQ(visible(), 1U);
    tx.rollBack(b);
    EXPECT_EQ(visible(), 1U);
    tx.rollBack(a); // the joiner's row is lost with the owner's rollback (LC-15)
    EXPECT_EQ(rows(provider.db(), "SELECT x FROM t").size(), 1U);

    // the object is reusable after the owner finished
    a = tx.start(false);
    EXPECT_TRUE(a.isInternal);
    tx.commit(a);

    // BEGIN refused by SQLite (a transaction opened behind its back)
    ASSERT_TRUE(provider.db().exec("BEGIN TRANSACTION;").has_value());
    EXPECT_EQ(message([&] { (void)tx.start(false); }),
              QStringLiteral("MarkSystemORM: Start transaction is failed!"));
    ASSERT_TRUE(provider.db().exec("ROLLBACK TRANSACTION;").has_value());
}
