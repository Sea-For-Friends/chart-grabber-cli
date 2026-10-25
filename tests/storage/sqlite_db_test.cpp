// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-018. Ports the parts of Src/u_SQLite3Handler.pas this module uses; see
// docs/specs/storage.md §2.15 (:1074-1110) and §3.3 (:1597-1608).
#include "cg/storage/sqlite/sqlite_db.h"

#include <QByteArray>
#include <QDir>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <optional>
#include <utility>

using cg::storage::sqlite::compareLogically;
using cg::storage::sqlite::SqliteDb;
using cg::storage::sqlite::SqliteStmt;

namespace {

QString dbPath(const QTemporaryDir& dir, const QString& name = QStringLiteral("test.sqlite")) {
    return QDir(dir.path()).filePath(name);
}

// T-0062: the two comparators of the createCollation cases -- plain byte order and its reverse.
int byteOrder(QByteArrayView lhs, QByteArrayView rhs) noexcept {
    return lhs.compare(rhs);
}
int reverseByteOrder(QByteArrayView lhs, QByteArrayView rhs) noexcept {
    return rhs.compare(lhs);
}

QStringList selectTextColumn(SqliteDb& db, QByteArrayView sql) {
    QStringList rows;
    auto stmt = db.prepare(sql);
    if (!stmt.has_value()) {
        ADD_FAILURE() << stmt.error().what().toStdString();
        return rows;
    }
    for (auto step = stmt->step(); step.has_value() && *step == SqliteStmt::StepResult::Row;
         step = stmt->step()) {
        rows.append(stmt->columnText(0));
    }
    return rows;
}

} // namespace

TEST(SqliteDb, OpenReadWriteCreateThenBusyTimeoutPragmaReadsBack5000) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    auto db = SqliteDb::open(dbPath(dir), SqliteDb::OpenMode::ReadWriteCreate);
    ASSERT_TRUE(db.has_value()) << db.error().what().toStdString();

    // sqlite3_busy_timeout has no getter of its own; "PRAGMA busy_timeout" (no argument) is
    // sqlite3's documented way to read the current value back.
    auto stmt = db->prepare(QByteArrayView("PRAGMA busy_timeout"));
    ASSERT_TRUE(stmt.has_value()) << stmt.error().what().toStdString();
    auto step = stmt->step();
    ASSERT_TRUE(step.has_value());
    ASSERT_EQ(*step, SqliteStmt::StepResult::Row);
    EXPECT_EQ(stmt->columnInt(0), SqliteDb::kBusyTimeoutMs);
}

TEST(SqliteDb, OpenReadOnlyOnMissingFileFails) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    auto db = SqliteDb::open(dbPath(dir, QStringLiteral("nonexistent.sqlite")), SqliteDb::OpenMode::ReadOnly);
    ASSERT_FALSE(db.has_value());
    EXPECT_EQ(db.error().code(), cg::core::ErrorCode::IoError);
    EXPECT_FALSE(db.error().what().isEmpty());
}

TEST(SqliteDb, ExecBadSqlReturnsIoErrorWithNonEmptyMessage) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto db = SqliteDb::open(dbPath(dir), SqliteDb::OpenMode::ReadWriteCreate);
    ASSERT_TRUE(db.has_value());

    const auto result = db->exec(QByteArrayView("NOT VALID SQL AT ALL"));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), cg::core::ErrorCode::IoError);
    EXPECT_FALSE(result.error().what().isEmpty());
}

TEST(SqliteDb, PreparedBlobRoundTripIncludingEmbeddedNulByte) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto db = SqliteDb::open(dbPath(dir), SqliteDb::OpenMode::ReadWriteCreate);
    ASSERT_TRUE(db.has_value());

    ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE t (k INTEGER PRIMARY KEY, b BLOB)")).has_value());

    const QByteArray payload = QByteArray("\x00\x01\xff\x02\x00", 5); // embeds 0x00 twice
    {
        auto insert = db->prepare(QByteArrayView("INSERT INTO t (k, b) VALUES (1, ?)"));
        ASSERT_TRUE(insert.has_value());
        ASSERT_TRUE(insert->bindBlob(1, payload).has_value());
        const auto step = insert->step();
        ASSERT_TRUE(step.has_value());
        EXPECT_EQ(*step, SqliteStmt::StepResult::Done);
    }
    EXPECT_EQ(db->lastInsertRowId(), 1);

    auto select = db->prepare(QByteArrayView("SELECT b FROM t WHERE k = 1"));
    ASSERT_TRUE(select.has_value());
    const auto step = select->step();
    ASSERT_TRUE(step.has_value());
    ASSERT_EQ(*step, SqliteStmt::StepResult::Row);
    EXPECT_FALSE(select->isNull(0));
    EXPECT_EQ(select->columnBlob(0), payload);
}

TEST(SqliteDb, TransactionCommitPersistsRow) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto db = SqliteDb::open(dbPath(dir), SqliteDb::OpenMode::ReadWriteCreate);
    ASSERT_TRUE(db.has_value());
    ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE t (k INTEGER PRIMARY KEY)")).has_value());

    ASSERT_TRUE(db->exec(QByteArrayView("BEGIN TRANSACTION")).has_value());
    ASSERT_TRUE(db->exec(QByteArrayView("INSERT INTO t (k) VALUES (42)")).has_value());
    ASSERT_TRUE(db->exec(QByteArrayView("COMMIT TRANSACTION")).has_value());

    auto select = db->prepare(QByteArrayView("SELECT k FROM t"));
    ASSERT_TRUE(select.has_value());
    ASSERT_EQ(*select->step(), SqliteStmt::StepResult::Row);
    EXPECT_EQ(select->columnInt(0), 42);
    EXPECT_EQ(*select->step(), SqliteStmt::StepResult::Done); // exactly one row
}

TEST(SqliteDb, TransactionRollbackDiscardsRow) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto db = SqliteDb::open(dbPath(dir), SqliteDb::OpenMode::ReadWriteCreate);
    ASSERT_TRUE(db.has_value());
    ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE t (k INTEGER PRIMARY KEY)")).has_value());

    ASSERT_TRUE(db->exec(QByteArrayView("BEGIN TRANSACTION")).has_value());
    ASSERT_TRUE(db->exec(QByteArrayView("INSERT INTO t (k) VALUES (42)")).has_value());
    ASSERT_TRUE(db->exec(QByteArrayView("ROLLBACK TRANSACTION")).has_value());

    auto select = db->prepare(QByteArrayView("SELECT k FROM t"));
    ASSERT_TRUE(select.has_value());
    EXPECT_EQ(*select->step(), SqliteStmt::StepResult::Done); // no rows
}

TEST(SqliteDb, TableColumnsAndTableNamesReflectSchema) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto db = SqliteDb::open(dbPath(dir), SqliteDb::OpenMode::ReadWriteCreate);
    ASSERT_TRUE(db.has_value());
    ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE t (x INTEGER, y INTEGER, v BLOB)")).has_value());

    const auto names = db->tableNames();
    ASSERT_TRUE(names.has_value());
    EXPECT_EQ(*names, QStringList{QStringLiteral("t")});

    const auto columns = db->tableColumns(QByteArrayView("t"));
    ASSERT_TRUE(columns.has_value());
    ASSERT_EQ(columns->size(), 3u);
    EXPECT_EQ((*columns)[0], QByteArray("x"));
    EXPECT_EQ((*columns)[1], QByteArray("y"));
    EXPECT_EQ((*columns)[2], QByteArray("v"));

    // Unknown table: PRAGMA table_info silently returns no rows, not an error.
    const auto missing = db->tableColumns(QByteArrayView("nosuchtable"));
    ASSERT_TRUE(missing.has_value());
    EXPECT_TRUE(missing->empty());
}

// Regression for T-0022A-review.md findings F3 (sqlite3_close_v2) and F4 (checked binds),
// combined into one case per the rework instruction.
TEST(SqliteDb, StmtOutlivingDbDoesNotLeakAndFailingBindReportsError) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    std::optional<SqliteStmt> stmt;
    {
        auto db = SqliteDb::open(dbPath(dir), SqliteDb::OpenMode::ReadWriteCreate);
        ASSERT_TRUE(db.has_value());
        ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE t (k INTEGER PRIMARY KEY)")).has_value());

        auto prepared = db->prepare(QByteArrayView("INSERT INTO t (k) VALUES (?)"));
        ASSERT_TRUE(prepared.has_value());
        stmt.emplace(std::move(*prepared));

        // F4: binding a parameter index the statement does not have (only "?" at index 1 exists)
        // must report the sqlite error, not silently leave the parameter unbound.
        const auto badBind = stmt->bindInt(2, 1);
        ASSERT_FALSE(badBind.has_value());
        EXPECT_EQ(badBind.error().code(), cg::core::ErrorCode::IoError);
        EXPECT_FALSE(badBind.error().what().isEmpty());

        ASSERT_TRUE(stmt->bindInt(1, 7).has_value());
        const auto step = stmt->step();
        ASSERT_TRUE(step.has_value());
        EXPECT_EQ(*step, SqliteStmt::StepResult::Done);

        // `db` is destroyed here while `stmt` is still alive and unfinalized. F3: SqliteDb must
        // use sqlite3_close_v2 so this zombifies the connection instead of leaking it (ASan build)
        // or crashing when `stmt` is later used/finalized.
    }

    // The connection is still needed to finalize the statement; this must not crash or leak.
    stmt.reset();
}

// FEAT-STO-018. Table-driven port of the worked vectors in docs/specs/storage.md §2.15.1, verifying
// compareLogically() statement by statement against CompareLogicallyAnsi (u_SQLite3Handler.pas:
// 215-327). Each `expected` is compareLogically(a, b); the mirrored call is checked too, since the
// collation must be a well-defined total order (b, a) == -(a, b) for every case here.
TEST(CompareLogically, WorkedVectorsFromSpec) {
    struct Case {
        QByteArrayView a;
        QByteArrayView b;
        int expected; // -1, 0 or 1
        const char* rule;
    };
    const Case cases[] = {
        {"", "0", -1, "rule 1: empty sorts before any non-empty string"},
        {"0", "00", -1, "rule 5 (zero case): equal value 0, the shorter digit run is smaller"},
        {"3", "10", -1, "rule 5: digit runs compared by value, 3 < 10"},
        {"2.33.0", "2.100.0", -1, "rule 4 then 5: '2.' matches byte for byte, then 33 < 100"},
        {"1a", "a", -1, "rule 3: '1' is a digit and 'a' is not -- the digit side is always smaller"},
        {"RUS", "rus", -1, "rule 4: unsigned-byte, case-sensitive -- 'R' (0x52) < 'r' (0x72)"},
        {"05", "5", 0, "rule 5 (non-zero case): equal value 5, leading zero ignored"},
        {"btv1b55003119j.f2", "btv1b55003119j.f10", -1,
         "rule 4 then 5: identical prefix up to 'f', then digit runs 2 < 10"},
        // L-15 regression (review T-0022B1, finding 1): the digit-run scan and the at-end check
        // must be bounded by the view's LENGTH, not by a NUL terminator. Every case above is a
        // NUL-terminated literal, so an unbounded (legacy-shaped) scan would also stop at the NUL
        // and this table would not catch a regression. These two rows use an explicit length
        // shorter than the backing buffer, with a digit immediately past the cut: an unbounded
        // scan reads that extra digit and gets the wrong (non-zero) answer.
        {QByteArrayView("91", 1), "9", 0,
         "L-15: view length cuts a digit run before its end; the byte just past the view (the "
         "second '1' of the backing \"91\" literal) is itself a digit -- an unbounded scan would "
         "read it and return '91' > '9', not equal"},
        {QByteArrayView("a12", 2), "a1", 0,
         "L-15: same, with the digit run in the middle of the buffer -- 'a1' cut out of the "
         "backing \"a12\" literal, next byte '2' is a digit"},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.rule);
        EXPECT_EQ(compareLogically(c.a, c.b), c.expected);
        EXPECT_EQ(compareLogically(c.b, c.a), -c.expected);
    }
}

// spec §2.15.1 "Registration": the collation is only active when open(..., logicalCollation =
// true), and `sqlite3_collation_needed` registers whatever name the schema asks for -- exercised
// here with the real name, LOGICALSORTING, the region cache's `t.v` ORDER BYs use.
TEST(SqliteDb, LogicalSortingCollationIsLiveOnOrderByWhenRequested) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto db = SqliteDb::open(dbPath(dir), SqliteDb::OpenMode::ReadWriteCreate, /*logicalCollation=*/true);
    ASSERT_TRUE(db.has_value()) << db.error().what().toStdString();

    ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE t (v TEXT)")).has_value());
    ASSERT_TRUE(db->exec(QByteArrayView("INSERT INTO t (v) VALUES ('10')")).has_value());
    ASSERT_TRUE(db->exec(QByteArrayView("INSERT INTO t (v) VALUES ('9')")).has_value());

    // A plain TEXT ORDER BY would put "10" before "9" (byte order); LOGICALSORTING must not.
    auto stmt = db->prepare(QByteArrayView("SELECT v FROM t ORDER BY v COLLATE LOGICALSORTING ASC"));
    ASSERT_TRUE(stmt.has_value()) << stmt.error().what().toStdString();

    auto step = stmt->step();
    ASSERT_TRUE(step.has_value());
    ASSERT_EQ(*step, SqliteStmt::StepResult::Row);
    EXPECT_EQ(stmt->columnText(0), QStringLiteral("9"));

    step = stmt->step();
    ASSERT_TRUE(step.has_value());
    ASSERT_EQ(*step, SqliteStmt::StepResult::Row);
    EXPECT_EQ(stmt->columnText(0), QStringLiteral("10"));

    step = stmt->step();
    ASSERT_TRUE(step.has_value());
    EXPECT_EQ(*step, SqliteStmt::StepResult::Done);
}

// T-0062, docs/specs/marks.md §2.9.4: a collation registered by name orders an explicit
// `ORDER BY x COLLATE name` and the index built over a column declared with it.
TEST(SqliteDb, CreateCollationOrdersOrderByAndIndexOverDeclaredColumn) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto db = SqliteDb::open(dbPath(dir), SqliteDb::OpenMode::ReadWriteCreate);
    ASSERT_TRUE(db.has_value()) << db.error().what().toStdString();

    const auto created = db->createCollation("REVERSED", &reverseByteOrder);
    ASSERT_TRUE(created.has_value()) << created.error().what().toStdString();

    ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE t (x TEXT COLLATE REVERSED, y TEXT)")).has_value());
    ASSERT_TRUE(
        db->exec(QByteArrayView("INSERT INTO t (x, y) VALUES ('b','b'), ('a','a'), ('c','c')")).has_value());
    const auto index = db->exec(QByteArrayView("CREATE UNIQUE INDEX ix ON t(x)"));
    ASSERT_TRUE(index.has_value()) << index.error().what().toStdString();

    const QStringList descending{QStringLiteral("c"), QStringLiteral("b"), QStringLiteral("a")};
    EXPECT_EQ(selectTextColumn(*db, QByteArrayView("SELECT y FROM t ORDER BY y COLLATE REVERSED")),
              descending);
    // No COLLATE clause: the declared collation of the column decides, and the index serves it.
    EXPECT_EQ(selectTextColumn(*db, QByteArrayView("SELECT x FROM t INDEXED BY ix ORDER BY x")), descending);
    EXPECT_EQ(selectTextColumn(*db, QByteArrayView("PRAGMA integrity_check")),
              QStringList{QStringLiteral("ok")});
}

TEST(SqliteDb, CreateCollationReplacesAnExistingOne) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto db = SqliteDb::open(dbPath(dir), SqliteDb::OpenMode::ReadWriteCreate);
    ASSERT_TRUE(db.has_value()) << db.error().what().toStdString();
    ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE t (y TEXT)")).has_value());
    ASSERT_TRUE(db->exec(QByteArrayView("INSERT INTO t (y) VALUES ('b'), ('a'), ('c')")).has_value());

    ASSERT_TRUE(db->createCollation("MINE", &reverseByteOrder).has_value());
    EXPECT_EQ(selectTextColumn(*db, QByteArrayView("SELECT y FROM t ORDER BY y COLLATE MINE")),
              (QStringList{QStringLiteral("c"), QStringLiteral("b"), QStringLiteral("a")}));

    const auto replaced = db->createCollation("MINE", &byteOrder);
    ASSERT_TRUE(replaced.has_value()) << replaced.error().what().toStdString();
    EXPECT_EQ(selectTextColumn(*db, QByteArrayView("SELECT y FROM t ORDER BY y COLLATE MINE")),
              (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
}

TEST(SqliteDb, UnknownCollationStillFailsAfterAnotherNameIsRegistered) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto db = SqliteDb::open(dbPath(dir), SqliteDb::OpenMode::ReadWriteCreate);
    ASSERT_TRUE(db.has_value()) << db.error().what().toStdString();
    ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE t (y TEXT)")).has_value());
    ASSERT_TRUE(db->createCollation("MINE", &byteOrder).has_value());

    const auto stmt = db->prepare(QByteArrayView("SELECT y FROM t ORDER BY y COLLATE NOSUCH"));
    ASSERT_FALSE(stmt.has_value());
    EXPECT_EQ(stmt.error().code(), cg::core::ErrorCode::IoError);
    EXPECT_TRUE(stmt.error().what().contains(QStringLiteral("no such collation sequence: NOSUCH")))
        << stmt.error().what().toStdString();
}
