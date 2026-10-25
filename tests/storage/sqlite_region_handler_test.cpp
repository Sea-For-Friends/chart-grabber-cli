// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-018. Ports the open/schema/discovery half of
// Src/TileStorage/SQLite/u_TileStorageSQLiteHandler.pas (:353-400, :486-525, :1040-1074) and
// Src/TileStorage/SQLite/u_TileStorageSQLiteHolder.pas (:102-305); see docs/specs/storage.md
// §2.16.1, §2.16.2, §2.16.4, §7.4.
#include "cg/storage/map_version.h"
#include "cg/storage/sqlite/sqlite_region_handler.h"
#include "cg/storage/sqlite/sqlite_region_schema.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>

#include "fixture_path.h"

using cg::storage::MapVersionFactory;
using cg::storage::sqlite::SqliteRegionHandler;
using cg::storage::sqlite::SqliteRegionSchema;
using cg::storage::sqlite::VersionColMode;

namespace {

std::shared_ptr<SqliteRegionSchema> makeSchema(const QString& storagePath, bool logging = false) {
    return std::make_shared<SqliteRegionSchema>(storagePath, QByteArray("image/jpg"),
                                                std::make_shared<MapVersionFactory>(), logging);
}

// Reads one PRAGMA's value back through the handler's db() accessor.
QByteArray readPragma(SqliteRegionHandler& handler, const char* pragma) {
    auto stmt = handler.db()->prepare(QByteArray("PRAGMA ") + pragma);
    if (!stmt) {
        return QByteArray();
    }
    auto step = stmt->step();
    if (!step || *step != cg::storage::sqlite::SqliteStmt::StepResult::Row) {
        return QByteArray();
    }
    return stmt->columnUtf8(0);
}

} // namespace

TEST(SqliteRegionHandler, OverrideFixtureAppliesCustomPragmasAndFiveColumnSchema) {
    // §7.4: the hand-made s.sql/t.sql/e.sql fixture. schema's storagePath is the read-only fixture
    // directory (only *.sql is read from it); the actual .sqlitedb lives in a scratch temp dir.
    const QString overridesDir =
        QDir(cgStorageFixture(QStringLiteral("cache/sqlite_overrides"))).absolutePath() + QStringLiteral("/");
    auto schema = makeSchema(overridesDir);

    QTemporaryDir dbDir;
    ASSERT_TRUE(dbDir.isValid());
    const QString dbPath = QDir(dbDir.path()).filePath(QStringLiteral("region.sqlitedb"));

    SqliteRegionHandler handler(schema, dbPath, /*useVersionFieldInDb=*/true, /*readOnly=*/false);
    ASSERT_TRUE(handler.isOpened());

    // t.sql's 5-column table: no v, c or h.
    EXPECT_EQ(handler.columnInfo().columnCount, 5);
    EXPECT_FALSE(handler.columnInfo().hasVersion);
    EXPECT_FALSE(handler.columnInfo().hasContentType);
    EXPECT_FALSE(handler.columnInfo().hasCrc);

    // s.sql's pragmas are in effect, not the built-in defaults (cache_size 2000, journal_mode wal).
    EXPECT_EQ(readPragma(handler, "cache_size"), QByteArray("500"));
    EXPECT_EQ(readPragma(handler, "journal_mode").toLower(), QByteArray("delete"));

    // e.sql ("PRAGMA optimize") ran: if it had failed, the constructor would have aborted and
    // isOpened() would be false (already asserted above).

    // T-0022B2 review finding 4: contentTypeFromDb goes through the content-type registry (not a
    // same-text passthrough). "image/jpeg" is a registered ALIAS whose canonical type is
    // "image/jpg" (content_type.cpp); an unregistered MIME string yields null/empty, matching
    // legacy's FContentTypeManager.GetInfo returning nil for an unknown type.
    EXPECT_EQ(schema->contentTypeFromDb(QByteArrayView("image/jpeg")), QByteArray("image/jpg"));
    EXPECT_TRUE(schema->contentTypeFromDb(QByteArrayView("application/x-bogus")).isEmpty());
    EXPECT_EQ(schema->contentTypeFromDb(QByteArrayView()), QByteArray("image/jpg")); // NULL -> main
}

TEST(SqliteRegionHandler, FreshFileGetsDefaultSchemaAndWalJournalMode) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = dir.path() + QStringLiteral("/"); // no s.sql/t.sql/e.sql here
    auto schema = makeSchema(storagePath);

    const QString dbPath = QDir(dir.path()).filePath(QStringLiteral("fresh.sqlitedb"));
    SqliteRegionHandler handler(schema, dbPath, /*useVersionFieldInDb=*/true, /*readOnly=*/false);
    ASSERT_TRUE(handler.isOpened());

    EXPECT_EQ(handler.columnInfo().columnCount, 8);
    EXPECT_TRUE(handler.columnInfo().hasVersion);
    EXPECT_EQ(handler.columnInfo().versionMode, VersionColMode::Int);
    EXPECT_TRUE(handler.columnInfo().hasContentType);
    EXPECT_TRUE(handler.columnInfo().hasCrc);

    EXPECT_EQ(readPragma(handler, "journal_mode").toLower(), QByteArray("wal"));
}

TEST(SqliteRegionHandler, RepoFixtureDiscoveredAsEightColumnsWithIntegerVersion) {
    // Copy the real repo fixture into a scratch dir first: opening it ReadWriteCreate (required --
    // a read-only open skips discovery entirely, see sqlite_region_handler.h's design notes) would
    // otherwise write WAL/session-pragma side effects into the checked-in fixture file.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString src = cgStorageFixture(QStringLiteral("cache/cache_sqlite/map/z1/0/0/0.0.sqlitedb"));
    const QString dbPath = QDir(dir.path()).filePath(QStringLiteral("0.0.sqlitedb"));
    ASSERT_TRUE(QFile::copy(src, dbPath));

    auto schema = makeSchema(dir.path() + QStringLiteral("/")); // no overrides; irrelevant here
                                                                // since `t` already exists
    SqliteRegionHandler handler(schema, dbPath, /*useVersionFieldInDb=*/true, /*readOnly=*/false);
    ASSERT_TRUE(handler.isOpened());

    EXPECT_EQ(handler.columnInfo().columnCount, 8);
    EXPECT_TRUE(handler.columnInfo().hasVersion);
    EXPECT_EQ(handler.columnInfo().versionMode, VersionColMode::Int);
    EXPECT_TRUE(handler.columnInfo().hasContentType);
    EXPECT_TRUE(handler.columnInfo().hasCrc);

    // T-0022B2 review finding 3: a SQL failure while opening (including one surfacing only once
    // discovery's SELECT actually executes) must fail the open, not leave a half-initialised
    // handler. A second connection holding the file's `locking_mode=EXCLUSIVE` OS-level lock on a
    // fresh copy of the same fixture reproduces this deterministically for the whole construction
    // -- a plain `BEGIN EXCLUSIVE` does not: the fixture is WAL-mode (as every region file this
    // schema opens becomes), where WAL readers are not blocked by a writer's transaction, only by
    // an actual exclusive file lock. SqliteDb::open's sqlite3_busy_timeout (5s) is unconditional,
    // so this blocks the locked handler's whole open() call.
    const QString lockedDbPath = QDir(dir.path()).filePath(QStringLiteral("locked.sqlitedb"));
    ASSERT_TRUE(QFile::copy(src, lockedDbPath));
    auto locker =
        cg::storage::sqlite::SqliteDb::open(lockedDbPath, cg::storage::sqlite::SqliteDb::OpenMode::ReadWrite);
    ASSERT_TRUE(locker);
    ASSERT_TRUE(locker->exec(QByteArrayView("PRAGMA locking_mode=EXCLUSIVE")));
    ASSERT_TRUE(locker->exec(QByteArrayView("BEGIN IMMEDIATE")));
    ASSERT_TRUE(locker->exec(QByteArrayView("SELECT 1")));

    SqliteRegionHandler blockedHandler(schema, lockedDbPath, /*useVersionFieldInDb=*/true,
                                       /*readOnly=*/false);
    EXPECT_FALSE(blockedHandler.isOpened());
}

TEST(SqliteRegionSchema, LogErrorWritesOneFormattedLineWhenLoggingEnabled) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = dir.path() + QStringLiteral("/");
    auto schema = makeSchema(storagePath, /*logging=*/true);

    schema->logError(SqliteRegionSchema::LogKind::Select, QStringLiteral("boom"));

    // No QIODevice::Text: that flag translates "\r\n" -> "\n" on read, which would hide the
    // exact line ending this test checks for.
    QFile log(storagePath + QStringLiteral("sqlite.log"));
    ASSERT_TRUE(log.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(log.readAll());

    // §2.16.4: "yyyy-mm-dd hh:nn:ss.zzzz<TAB>(select)<TAB>boom".
    static const QRegularExpression kLineRe(
        QStringLiteral(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+\t\(select\)\tboom\r\n$)"));
    EXPECT_TRUE(kLineRe.match(content).hasMatch()) << content.toStdString();
}
