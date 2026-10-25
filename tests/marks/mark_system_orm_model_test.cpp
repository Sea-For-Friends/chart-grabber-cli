// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-034 (held open). Ports no legacy test (there is none): the schema a fresh
// bootstrap writes against the real Marks.db3 written by Windows SAS.Planet
// (tests/fixtures/marks/Marks.db3), docs/specs/marks.md §2.9.1 and §2.9.3 steps B-9...B-12.
#include "cg/marks/mormot/mark_system_orm_model.h"
#include "cg/marks/mormot/system_nocase.h"
#include "cg/storage/sqlite/sqlite_db.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "../corpus_skip.h"
#include "fixture_path.h"

using cg::marks::createMissingIndexesSqlite3;
using cg::marks::createMissingTablesSqlite3;
using cg::storage::sqlite::SqliteDb;
using cg::storage::sqlite::SqliteStmt;

namespace {

// Every row of a query, columns joined with '|'.
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

const QByteArrayView kMaster = "SELECT type,name,tbl_name,rootpage,sql FROM sqlite_master ORDER BY rowid";

// What the connection provider does around the model calls (steps B-3...B-8): page size for a
// new file, the collation, WAL.
SqliteDb openAsLegacy(const QString& path, bool isNew) {
    auto db =
        SqliteDb::open(path, isNew ? SqliteDb::OpenMode::ReadWriteCreate : SqliteDb::OpenMode::ReadWrite);
    if (!db.has_value()) {
        ADD_FAILURE() << db.error().what().toStdString();
        std::abort();
    }
    if (isNew) {
        EXPECT_TRUE(db->exec("PRAGMA page_size=4096").has_value());
    }
    EXPECT_TRUE(db->createCollation("SYSTEMNOCASE", &cg::marks::systemNoCaseCompare).has_value());
    EXPECT_TRUE(db->exec("PRAGMA journal_mode=WAL;").has_value());
    return std::move(*db);
}

// The fixture is never opened in place: SQLite would write -wal/-shm next to it.
QString copyOfFixture(const QTemporaryDir& dir) {
    const QString copy = QDir(dir.path()).filePath(QStringLiteral("Marks.db3"));
    EXPECT_TRUE(QFile::copy(cgMarksFixture(QStringLiteral("marks/Marks.db3")), copy));
    EXPECT_TRUE(QFile::setPermissions(copy, QFile::ReadOwner | QFile::WriteOwner));
    return copy;
}

} // namespace

TEST(MarkSystemOrmModel, EmptyDatabaseGetsTheSchemaOfTheWindowsFile) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    SqliteDb fixture = openAsLegacy(copyOfFixture(dir), /*isNew=*/false);
    SqliteDb fresh = openAsLegacy(QDir(dir.path()).filePath(QStringLiteral("fresh.db3")), /*isNew=*/true);

    const auto statements = createMissingTablesSqlite3(fresh);
    ASSERT_TRUE(statements.has_value()) << statements.error().what().toStdString();
    EXPECT_EQ(*statements, 13); // BEGIN, 10 x CREATE [VIRTUAL] TABLE, IndexMarkMetamMark, COMMIT
    createMissingIndexesSqlite3(fresh);
    // The fixture holds the default user (step B-14, statement S-02) and nothing else.
    ASSERT_TRUE(fresh.exec("INSERT INTO User (uName) VALUES ('sasgis')").has_value());

    const QStringList expected = rows(fixture, kMaster);
    ASSERT_EQ(expected.size(), 32);
    const QStringList actual = rows(fresh, kMaster);
    ASSERT_EQ(actual.size(), expected.size());
    for (qsizetype i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i], expected[i]) << "sqlite_master row " << i;
    }
    for (const char* pragma : {"page_size", "encoding", "journal_mode", "user_version", "application_id",
                               "auto_vacuum", "schema_version", "page_count", "freelist_count"}) {
        const QByteArray sql = QByteArray("PRAGMA ") + pragma;
        EXPECT_EQ(rows(fresh, sql), rows(fixture, sql)) << pragma;
    }
    EXPECT_EQ(rows(fresh, "PRAGMA schema_version"), QStringList{QStringLiteral("29")});
    EXPECT_EQ(rows(fresh, "PRAGMA page_count"), QStringList{QStringLiteral("31")});
    EXPECT_EQ(rows(fresh, "SELECT name,seq FROM sqlite_sequence"),
              rows(fixture, "SELECT name,seq FROM sqlite_sequence"));
    EXPECT_EQ(rows(fresh, "SELECT ID,uName FROM User"), rows(fixture, "SELECT ID,uName FROM User"));
}

TEST(MarkSystemOrmModel, CompleteDatabaseIsLeftUnchanged) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    SqliteDb db = openAsLegacy(copyOfFixture(dir), /*isNew=*/false);
    const QStringList masterBefore = rows(db, kMaster);
    const QStringList cookieBefore = rows(db, "PRAGMA schema_version");

    const auto statements = createMissingTablesSqlite3(db);
    ASSERT_TRUE(statements.has_value()) << statements.error().what().toStdString();
    EXPECT_EQ(*statements, 0);       // B-9, B-10: nothing missing -> no transaction, no B-11
    createMissingIndexesSqlite3(db); // B-12: ten IF NOT EXISTS requests that find their index

    EXPECT_EQ(rows(db, kMaster), masterBefore);
    EXPECT_EQ(rows(db, "PRAGMA schema_version"), cookieBefore);
    EXPECT_EQ(rows(db, "PRAGMA integrity_check"), QStringList{QStringLiteral("ok")});
}

// B-10: an existing table that lacks a model column gets it by ALTER TABLE, inside the transaction
// that creates the nine missing tables.
TEST(MarkSystemOrmModel, ExistingTableGetsItsMissingColumn) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    SqliteDb db = openAsLegacy(QDir(dir.path()).filePath(QStringLiteral("old.db3")), /*isNew=*/true);
    ASSERT_TRUE(db.exec("CREATE TABLE Category(ID INTEGER PRIMARY KEY AUTOINCREMENT)").has_value());

    const auto statements = createMissingTablesSqlite3(db);
    ASSERT_TRUE(statements.has_value()) << statements.error().what().toStdString();
    EXPECT_EQ(*statements, 13); // BEGIN, 9 x CREATE, ALTER, IndexMarkMetamMark, COMMIT
    EXPECT_EQ(
        rows(db, "SELECT sql FROM sqlite_master WHERE name='Category'"),
        QStringList{QStringLiteral(
            "CREATE TABLE Category(ID INTEGER PRIMARY KEY AUTOINCREMENT, cName TEXT COLLATE SYSTEMNOCASE)")});
    EXPECT_EQ(rows(db, "SELECT count(*) FROM sqlite_master WHERE name='IndexMarkMetamMark'"),
              QStringList{QStringLiteral("1")});
}

// A failing statement rolls the whole call back: SQLite refuses to add the UNIQUE column mMark.
TEST(MarkSystemOrmModel, FailingStatementRollsTheTransactionBack) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    SqliteDb db = openAsLegacy(QDir(dir.path()).filePath(QStringLiteral("broken.db3")), /*isNew=*/true);
    ASSERT_TRUE(
        db.exec("CREATE TABLE MarkMeta(ID INTEGER PRIMARY KEY AUTOINCREMENT, mMeta BLOB)").has_value());

    const auto statements = createMissingTablesSqlite3(db);
    ASSERT_FALSE(statements.has_value());
    EXPECT_TRUE(statements.error().what().contains(QStringLiteral("UNIQUE")))
        << statements.error().what().toStdString();
    EXPECT_EQ(rows(db, "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"),
              QStringList{QStringLiteral("MarkMeta")});
    EXPECT_TRUE(db.exec("BEGIN TRANSACTION;").has_value()); // no transaction was left open
}
