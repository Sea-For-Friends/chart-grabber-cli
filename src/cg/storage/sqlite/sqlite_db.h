// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Includes/libsqlite3.pas (deleted -- system <sqlite3.h> replaces the hand-rolled binding)
//        and the parts of Src/u_SQLite3Handler.pas this module uses (TSQLite3DbHandler,
//        TSQLite3StmtData).
//
// docs/specs/storage.md §2.15 (:1074-1110) and §3.3 (:1597-1608). ADR-0005 decision 2: this is
// the ONLY translation unit that may include <sqlite3.h> (tools/validate.sh's SQLITE3-LEAK rule);
// everything above this layer (the SQLite region tile storage, T-0022B) goes through SqliteDb /
// SqliteStmt only. No QtSql anywhere in this module (tech-lead-rulings.md, 2026-09-17).
//
// Scope note: this class is the raw connection/statement primitive only. The per-thread
// connection pool, the per-file write mutex and the idle-close sweep of §3.3 belong to the
// SQLite region tile storage (T-0022B) that will own a pool of SqliteDb instances -- nothing
// here assumes a particular caller or schema (ticket T-0022A Scope (out): "Any schema or storage
// class").

#include "cg/core/result.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace cg::storage::sqlite {

class SqliteStmt;

// FEAT-STO-018. One sqlite3* connection, opened with SQLITE_OPEN_NOMUTEX (never shared across
// threads -- §3.3: "one connection per (region file, thread)"). Move-only RAII: sqlite3_close on
// destruction/reassignment, never a leaked handle on any path (open() itself never returns a
// live SqliteDb on error -- CheckResult's `except Close; raise;` in u_SQLite3Handler.pas:717-724
// becomes "close and return fail() before construction").
class SqliteDb final {
public:
    enum class OpenMode : std::uint8_t {
        ReadOnly,        // SQLITE_OPEN_READONLY | SQLITE_OPEN_URI | SQLITE_OPEN_NOMUTEX --
                         // u_TileStorageSQLiteFileConnection.pas:220
        ReadWrite,       // SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX
        ReadWriteCreate, // SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX --
                         // u_TileStorageSQLiteFileConnection.pas:224
    };

    // Deviation D-6 (docs/parity/deviations.md): legacy has no per-connection equivalent of
    // sqlite3_busy_timeout in this unit -- SetBusyTryCount/LocalSQLiteBusyHandler
    // (u_SQLite3Handler.pas:203-210, :835-838) install a Sleep(1)-and-retry busy HANDLER, and only
    // one caller outside this ticket's scope ever arms it (u_TileStorageSQLiteFileConnection.pas:
    // 230, SetBusyTryCount(3)). sqlite3_busy_timeout is the engine's built-in equivalent of that
    // retry loop and is set unconditionally on every open() here.
    static constexpr int kBusyTimeoutMs = 5000;

    // logicalCollation = true ports TSQLite3DbHandler.Open(..., ASupportLogicalCollation = True)
    // (u_SQLite3Handler.pas:715-734): registers sqlite3_collation_needed so that ANY collation name
    // the schema references resolves to compareLogically() below (docs/specs/storage.md §2.15.1).
    // Only the native region cache (T-0022B) passes true; every other caller keeps the default.
    [[nodiscard]] static core::Result<SqliteDb> open(const QString& path, OpenMode mode,
                                                     bool logicalCollation = false);

    SqliteDb(const SqliteDb&) = delete;
    SqliteDb& operator=(const SqliteDb&) = delete;
    SqliteDb(SqliteDb&& other) noexcept;
    SqliteDb& operator=(SqliteDb&& other) noexcept;
    ~SqliteDb();

    // Prepares, steps to completion and finalizes ASqlText in one call (ports ExecSql's call
    // through OpenSqlWithText with no callback and no bound text, u_SQLite3Handler.pas:614-619).
    // Only the FIRST statement of `sql` runs -- sqlite3_prepare_v2 stops at the first ';' and the
    // remainder is silently discarded, exactly as legacy's pzTail-less prepare call does.
    [[nodiscard]] core::Result<int> exec(QByteArrayView sql); // returns sqlite3_changes()

    [[nodiscard]] core::Result<SqliteStmt> prepare(QByteArrayView sql);

    // PRAGMA table_info(table)'s "name" column, in declaration order. Empty (not an error) when
    // `table` does not exist -- PRAGMA table_info silently returns no rows for an unknown table.
    [[nodiscard]] core::Result<std::vector<QByteArray>> tableColumns(QByteArrayView table);

    // SELECT name FROM sqlite_master WHERE type='table', in sqlite_master's row order.
    [[nodiscard]] core::Result<QStringList> tableNames();

    // T-0062 (docs/specs/marks.md §2.9.4): a named UTF-8 collation of this connection, the
    // counterpart of mORMot's sqlite3.create_collation(DB, 'SYSTEMNOCASE', SQLITE_UTF8, nil, ...)
    // (db/mormot.db.raw.sqlite3.pas:7582). A plain function pointer: it travels as the callback's
    // user-data argument, so the connection owns nothing and there is nothing to destroy.
    using CollationCompare = int (*)(QByteArrayView lhs, QByteArrayView rhs) noexcept;
    // Registers (or replaces) the collation `name` (ASCII). Independent of the `logicalCollation`
    // flag of open(). Error: the SQLite message, as every other SqliteDb call.
    [[nodiscard]] core::Result<void> createCollation(const char* name, CollationCompare compare);

    [[nodiscard]] qint64 lastInsertRowId() const;

    // sqlite3_errmsg() + " ( error code: " + sqlite3_errcode() + ")" -- the exact format of
    // TSQLite3DbHandler.RaiseSQLite3Error (u_SQLite3Handler.pas:552-560).
    [[nodiscard]] QString lastError() const;

private:
    explicit SqliteDb(sqlite3* handle) noexcept : handle_(handle) {}
    void closeHandle() noexcept;

    sqlite3* handle_ = nullptr;
};

// A prepared statement (ports TSQLite3StmtData). Move-only; sqlite3_finalize on destruction. Never
// outlives the SqliteDb it was prepared from.
class SqliteStmt final {
public:
    enum class StepResult : std::uint8_t { Row, Done };

    SqliteStmt(const SqliteStmt&) = delete;
    SqliteStmt& operator=(const SqliteStmt&) = delete;
    SqliteStmt(SqliteStmt&& other) noexcept;
    SqliteStmt& operator=(SqliteStmt&& other) noexcept;
    ~SqliteStmt();

    // 1-based column indices, matching sqlite3_bind_*. Each returns fail() with the sqlite error
    // on a non-SQLITE_OK bind result (T-0022A-review.md finding F4; legacy checks the blob bind,
    // u_SQLite3Handler.pas:635) instead of silently leaving the parameter NULL.
    [[nodiscard]] core::Result<void> bindInt(int index, int value);
    [[nodiscard]] core::Result<void> bindInt64(int index, qint64 value);
    [[nodiscard]] core::Result<void>
    bindText(int index, const QString& value); // copied
                                               // (SQLITE_TRANSIENT): value is a temporary
                                               // UTF-8 re-encoding, unlike blob
    // `value` must stay alive and unmodified until the following step() returns -- bound with
    // SQLITE_STATIC, no copy (docs/specs/storage.md :1108-1110).
    [[nodiscard]] core::Result<void> bindBlob(int index, const QByteArray& value);
    // sqlite3_bind_null (T-0179: legacy's nil-buffer BindBlob of a zero-size tile ends as bind_null)
    [[nodiscard]] core::Result<void> bindNull(int index);

    [[nodiscard]] core::Result<StepResult> step();
    [[nodiscard]] core::Result<void> reset();

    // sqlite3_changes() on the owning connection, read right after this statement's step() runs an
    // INSERT/UPDATE/DELETE to completion -- ports the VRowsAffected out-param of ExecSQL/
    // ExecSQLWithBLOB (u_SQLite3Handler.pas), needed by T-0022B4's saveTile once `h`/`b` are bound
    // parameters instead of literal SQL text (exec()'s own sqlite3_changes() only covers the
    // no-bind-parameters path).
    [[nodiscard]] int changes() const;

    // 0-based column indices, matching sqlite3_column_*.
    [[nodiscard]] int columnCount() const;
    [[nodiscard]] bool isNull(int index) const;
    // sqlite3_column_type() == SQLITE_INTEGER. T-0022B3: callers that must branch on SQLite's
    // per-row dynamic storage class -- the same NULL/INTEGER/else split CallbackSelectTileInfo and
    // CallbackSelectVersions run on the `v` column (u_TileStorageSQLiteHandler.pas:560-605,
    // :739-759), because a column's declared type is only an affinity hint, not a per-row
    // guarantee (e.g. a version string like "X701" stored in an INTEGER-affinity column stays
    // SQLITE_TEXT) -- combine this with isNull(); anything that is neither NULL nor INTEGER is
    // TEXT/FLOAT/BLOB, read with columnText()/columnUtf8().
    [[nodiscard]] bool isInteger(int index) const;
    [[nodiscard]] int columnInt(int index) const;
    [[nodiscard]] qint64 columnInt64(int index) const;
    [[nodiscard]] double columnDouble(int index) const;
    [[nodiscard]] QString columnText(int index) const;    // sqlite3_column_text, UTF-8 decoded
    [[nodiscard]] QByteArray columnUtf8(int index) const; // sqlite3_column_text, raw bytes
    [[nodiscard]] QByteArray columnBlob(int index) const; // sqlite3_column_blob (one copy)

    // sqlite3_column_name / sqlite3_column_decltype -- schema introspection for T-0022B2's column
    // discovery (spec §2.16.2, ports CallbackSelectCols's ColumnName/ColumnDeclType calls,
    // u_TileStorageSQLiteHandler.pas:499-508). Both are valid to call regardless of whether the
    // preceding step() returned a row: column metadata is fixed at prepare time. `columnDeclType`
    // is empty for an expression column (no declared type, e.g. a computed SELECT), matching a
    // null ColumnDeclType return in legacy.
    [[nodiscard]] QByteArray columnName(int index) const;
    [[nodiscard]] QByteArray columnDeclType(int index) const;

private:
    friend class SqliteDb;
    SqliteStmt(sqlite3_stmt* stmt, sqlite3* db) noexcept : stmt_(stmt), db_(db) {}
    void finalizeHandle() noexcept;

    sqlite3_stmt* stmt_ = nullptr;
    sqlite3* db_ = nullptr; // not owned; only used to format sqlite3_errmsg() on step() failure
};

// Ports CompareLogicallyAnsi (u_SQLite3Handler.pas:215-327), the routine behind the
// `LOGICALSORTING` collation (docs/specs/storage.md §2.15.1). Needs no sqlite3 type, so it is
// declared here and tests call it directly instead of going through a live connection.
//
// Legacy defect L-15 (docs/parity/legacy-defects.md): CompareLogicallyAnsi's digit-run scan
// (`until not CharInSet(Cursor^, ['0'..'9'])`) never checks the remaining length, so it reads past
// the buffer when a string ends in digits -- memory-unsafe on reachable input. This is the ticket's
// one CRITICAL-defect exception (Rule 2): the port bounds every scan by the length instead of
// reproducing the over-read.
[[nodiscard]] int compareLogically(QByteArrayView a, QByteArrayView b) noexcept;

// Ports the useful half of TSQLite3DbHandler.LibVersionInfo (u_SQLite3Handler.pas:702-712): the
// diagnostic string SqliteRegionSchema::logError (T-0022B2, §2.16.4) writes ahead of an open
// failure. Legacy's version also names the DLL module handle it loaded the symbol from -- Windows-
// only trivia with no Linux equivalent -- so the port keeps just the version string.
[[nodiscard]] QString sqliteLibVersion();

} // namespace cg::storage::sqlite
