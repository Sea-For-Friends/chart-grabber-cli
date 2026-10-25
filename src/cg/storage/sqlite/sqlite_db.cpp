// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_db.h"

#include <sqlite3.h> // the only translation unit allowed to -- tools/validate.sh SQLITE3-LEAK

#include <utility>

namespace cg::storage::sqlite {

namespace {

// Ports TSQLite3DbHandler.RaiseSQLite3Error's message format (u_SQLite3Handler.pas:552-560).
QString formatSqliteError(sqlite3* handle) {
    if (handle == nullptr) {
        return QStringLiteral("SQLite3 error");
    }
    return QStringLiteral("%1 ( error code: %2)")
        .arg(QString::fromUtf8(sqlite3_errmsg(handle)))
        .arg(sqlite3_errcode(handle));
}

// The sqlite3 xCompare trampoline (LogicalCollactionCompareUTF8/UTF16LE/UTF16BE all reduce to this
// same call, u_SQLite3Handler.pas:329-345): sqlite hands over raw byte buffers with explicit
// lengths, not necessarily NUL-terminated, exactly what compareLogically() expects.
int collationNeededXCompare(void* /*pUser*/, int n1, const void* z1, int n2, const void* z2) {
    const auto toView = [](const void* p, int n) -> QByteArrayView {
        if (p == nullptr || n <= 0) {
            return QByteArrayView();
        }
        return QByteArrayView(reinterpret_cast<const char*>(p), static_cast<qsizetype>(n));
    };
    return compareLogically(toView(z1, n1), toView(z2, n2));
}

// Ports Callback4CollationNeeded's SQLITE_UTF8/SQLITE_UTF16LE/SQLITE_UTF16BE branches
// (u_SQLite3Handler.pas:347-380): all three register the SAME comparator with SQLITE_UTF8, so
// sqlite always converts and hands over UTF-8 bytes (spec §2.15.1 "Registration"). The `else`
// (an unknown eTextRep) mirrors legacy's `Assert(False, ...)`, which is a silent no-op in a
// release build (assertions off -- legacy defect L-30): this branch is unreachable in practice
// since sqlite3_collation_needed only ever passes one of the three UTF encodings.
void collationNeededCallback(void* /*pCollNeededArg*/, sqlite3* db, int eTextRep, const char* zExternal) {
    switch (eTextRep) {
    case SQLITE_UTF8:
    case SQLITE_UTF16LE:
    case SQLITE_UTF16BE:
        sqlite3_create_collation(db, zExternal, SQLITE_UTF8, nullptr, &collationNeededXCompare);
        break;
    default:
        break;
    }
}

// The xCompare trampoline of SqliteDb::createCollation: the registered function pointer arrives as
// the user-data argument. SQLite passes byte buffers with explicit lengths (not NUL-terminated).
int namedCollationXCompare(void* pUser, int n1, const void* z1, int n2, const void* z2) {
    const auto compare = reinterpret_cast<SqliteDb::CollationCompare>(pUser);
    return compare(QByteArrayView(static_cast<const char*>(z1), static_cast<qsizetype>(n1)),
                   QByteArrayView(static_cast<const char*>(z2), static_cast<qsizetype>(n2)));
}

} // namespace

// --- SqliteDb ----------------------------------------------------------------

core::Result<SqliteDb> SqliteDb::open(const QString& path, OpenMode mode, bool logicalCollation) {
    int flags = SQLITE_OPEN_NOMUTEX;
    switch (mode) {
    case OpenMode::ReadOnly:
        flags |= SQLITE_OPEN_READONLY | SQLITE_OPEN_URI;
        break;
    case OpenMode::ReadWrite:
        flags |= SQLITE_OPEN_READWRITE;
        break;
    case OpenMode::ReadWriteCreate:
        flags |= SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        break;
    }

    const QByteArray pathUtf8 = path.toUtf8();
    sqlite3* handle = nullptr;
    const int rc = sqlite3_open_v2(pathUtf8.constData(), &handle, flags, nullptr);
    if (rc != SQLITE_OK) {
        // u_SQLite3Handler.pas:717-724: close whatever sqlite3_open_v2 allocated (it may return a
        // non-null handle even on failure) before reporting the error, so no ASan leak on the
        // open-failure path.
        const QString message = handle != nullptr
                                    ? formatSqliteError(handle)
                                    : QStringLiteral("sqlite3_open_v2 failed ( error code: %1)").arg(rc);
        if (handle != nullptr) {
            sqlite3_close(handle);
        }
        return core::fail(core::ErrorCode::IoError, message);
    }

    sqlite3_busy_timeout(handle, kBusyTimeoutMs); // Deviation D-6, see sqlite_db.h

    if (logicalCollation) {
        // Ports TSQLite3DbHandler.RegisterCollationNeededCallback (u_SQLite3Handler.pas:830-833).
        sqlite3_collation_needed(handle, nullptr, &collationNeededCallback);
    }

    return SqliteDb(handle);
}

SqliteDb::SqliteDb(SqliteDb&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

SqliteDb& SqliteDb::operator=(SqliteDb&& other) noexcept {
    if (this != &other) {
        closeHandle();
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

SqliteDb::~SqliteDb() {
    closeHandle();
}

void SqliteDb::closeHandle() noexcept {
    if (handle_ != nullptr) {
        // sqlite3_close_v2, not sqlite3_close (T-0022A-review.md finding F3): the port's
        // independent move-only SqliteDb/SqliteStmt types let a SqliteStmt outlive the SqliteDb it
        // was prepared from (e.g. member declaration order, or a statement kept in a pool).
        // sqlite3_close would then return SQLITE_BUSY and leave the connection open forever --
        // leaking the file handle and WAL lock. _v2 instead marks the connection a zombie that is
        // deallocated once its last outstanding statement is finalized. Ports
        // TSQLite3DbHandler.Close, u_SQLite3Handler.pas:563-568 (legacy is safe with plain close()
        // only because every statement is finalized in a local try/finally).
        sqlite3_close_v2(handle_);
        handle_ = nullptr;
    }
}

core::Result<int> SqliteDb::exec(QByteArrayView sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return core::fail(core::ErrorCode::IoError, lastError());
    }

    for (;;) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            continue; // OpenSqlWithText with no callback: rows are stepped through and discarded
        }
        if (rc == SQLITE_DONE) {
            break;
        }
        const QString message = lastError();
        sqlite3_finalize(stmt);
        return core::fail(core::ErrorCode::IoError, message);
    }

    const int changes = sqlite3_changes(handle_);
    if (sqlite3_finalize(stmt) != SQLITE_OK) {
        return core::fail(core::ErrorCode::IoError, lastError());
    }
    return changes;
}

core::Result<SqliteStmt> SqliteDb::prepare(QByteArrayView sql) {
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(handle_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return core::fail(core::ErrorCode::IoError, lastError());
    }
    return SqliteStmt(stmt, handle_);
}

core::Result<std::vector<QByteArray>> SqliteDb::tableColumns(QByteArrayView table) {
    QByteArray name(table.data(), static_cast<qsizetype>(table.size()));
    name.replace("\"", "\"\"");
    const QByteArray sql = "PRAGMA table_info(\"" + name + "\")";

    auto stmt = prepare(sql);
    if (!stmt) {
        return core::Unexpected(stmt.error());
    }

    std::vector<QByteArray> columns;
    for (;;) {
        auto step = stmt->step();
        if (!step) {
            return core::Unexpected(step.error());
        }
        if (*step == SqliteStmt::StepResult::Done) {
            break;
        }
        columns.push_back(stmt->columnUtf8(1)); // table_info: 0=cid, 1=name, 2=type, ...
    }
    return columns;
}

core::Result<QStringList> SqliteDb::tableNames() {
    auto stmt = prepare(QByteArrayView("SELECT name FROM sqlite_master WHERE type='table'"));
    if (!stmt) {
        return core::Unexpected(stmt.error());
    }

    QStringList names;
    for (;;) {
        auto step = stmt->step();
        if (!step) {
            return core::Unexpected(step.error());
        }
        if (*step == SqliteStmt::StepResult::Done) {
            break;
        }
        names << stmt->columnText(0);
    }
    return names;
}

core::Result<void> SqliteDb::createCollation(const char* name, CollationCompare compare) {
    // The function pointer travels as the `void*` user data: a conversion between a function pointer
    // and `void*` is only conditionally-supported in C++ (fine on POSIX).
    const int rc = sqlite3_create_collation_v2(handle_, name, SQLITE_UTF8, reinterpret_cast<void*>(compare),
                                               &namedCollationXCompare, nullptr);
    if (rc != SQLITE_OK) {
        return core::fail(core::ErrorCode::IoError, lastError());
    }
    return {};
}

qint64 SqliteDb::lastInsertRowId() const {
    return sqlite3_last_insert_rowid(handle_);
}

QString SqliteDb::lastError() const {
    return formatSqliteError(handle_);
}

// --- SqliteStmt ----------------------------------------------------------------

SqliteStmt::SqliteStmt(SqliteStmt&& other) noexcept
    : stmt_(std::exchange(other.stmt_, nullptr)), db_(std::exchange(other.db_, nullptr)) {}

SqliteStmt& SqliteStmt::operator=(SqliteStmt&& other) noexcept {
    if (this != &other) {
        finalizeHandle();
        stmt_ = std::exchange(other.stmt_, nullptr);
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

SqliteStmt::~SqliteStmt() {
    finalizeHandle();
}

void SqliteStmt::finalizeHandle() noexcept {
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_); // ports TSQLite3StmtData.Fin, u_SQLite3Handler.pas:511-513
        stmt_ = nullptr;
    }
}

core::Result<void> SqliteStmt::bindInt(int index, int value) {
    if (sqlite3_bind_int(stmt_, index, value) != SQLITE_OK) {
        return core::fail(core::ErrorCode::IoError, formatSqliteError(db_));
    }
    return {};
}

core::Result<void> SqliteStmt::bindInt64(int index, qint64 value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) {
        return core::fail(core::ErrorCode::IoError, formatSqliteError(db_));
    }
    return {};
}

core::Result<void> SqliteStmt::bindText(int index, const QString& value) {
    const QByteArray utf8 = value.toUtf8();
    // sqlite3_bind_text64, not sqlite3_bind_text: the 32-bit API truncates a size above INT_MAX
    // through the static_cast<int> this replaces (T-0022A-review.md finding F4). SQLITE_UTF8 tells
    // sqlite the byte encoding of the buffer being bound.
    const int rc =
        sqlite3_bind_text64(stmt_, index, utf8.constData(), static_cast<sqlite3_uint64>(utf8.size()),
                            SQLITE_TRANSIENT, SQLITE_UTF8);
    if (rc != SQLITE_OK) {
        return core::fail(core::ErrorCode::IoError, formatSqliteError(db_));
    }
    return {};
}

core::Result<void> SqliteStmt::bindBlob(int index, const QByteArray& value) {
    // sqlite3_bind_blob64, not sqlite3_bind_blob: same INT_MAX truncation as bindText, and legacy
    // checks this bind's result too (ExecSqlWithBlob's CheckResult, u_SQLite3Handler.pas:635-637).
    const int rc = sqlite3_bind_blob64(stmt_, index, value.constData(),
                                       static_cast<sqlite3_uint64>(value.size()), SQLITE_STATIC);
    if (rc != SQLITE_OK) {
        return core::fail(core::ErrorCode::IoError, formatSqliteError(db_));
    }
    return {};
}

core::Result<void> SqliteStmt::bindNull(int index) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) {
        return core::fail(core::ErrorCode::IoError, formatSqliteError(db_));
    }
    return {};
}

core::Result<SqliteStmt::StepResult> SqliteStmt::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return StepResult::Row;
    }
    if (rc == SQLITE_DONE) {
        return StepResult::Done;
    }
    return core::fail(core::ErrorCode::IoError, formatSqliteError(db_));
}

core::Result<void> SqliteStmt::reset() {
    if (sqlite3_reset(stmt_) != SQLITE_OK) {
        return core::fail(core::ErrorCode::IoError, formatSqliteError(db_));
    }
    return {};
}

int SqliteStmt::changes() const {
    return sqlite3_changes(db_);
}

int SqliteStmt::columnCount() const {
    return sqlite3_column_count(stmt_);
}

bool SqliteStmt::isNull(int index) const {
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

bool SqliteStmt::isInteger(int index) const {
    return sqlite3_column_type(stmt_, index) == SQLITE_INTEGER;
}

int SqliteStmt::columnInt(int index) const {
    return sqlite3_column_int(stmt_, index);
}

qint64 SqliteStmt::columnInt64(int index) const {
    return sqlite3_column_int64(stmt_, index);
}

double SqliteStmt::columnDouble(int index) const {
    return sqlite3_column_double(stmt_, index);
}

QString SqliteStmt::columnText(int index) const {
    return QString::fromUtf8(columnUtf8(index));
}

QByteArray SqliteStmt::columnUtf8(int index) const {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, index));
    const int bytes = sqlite3_column_bytes(stmt_, index);
    return text != nullptr ? QByteArray(text, bytes) : QByteArray();
}

QByteArray SqliteStmt::columnBlob(int index) const {
    const auto* data = reinterpret_cast<const char*>(sqlite3_column_blob(stmt_, index));
    const int bytes = sqlite3_column_bytes(stmt_, index);
    return data != nullptr ? QByteArray(data, bytes) : QByteArray(); // one copy, unavoidable
}

QByteArray SqliteStmt::columnName(int index) const {
    const auto* name = sqlite3_column_name(stmt_, index);
    return name != nullptr ? QByteArray(name) : QByteArray();
}

QByteArray SqliteStmt::columnDeclType(int index) const {
    const auto* type = sqlite3_column_decltype(stmt_, index);
    return type != nullptr ? QByteArray(type) : QByteArray();
}

// --- compareLogically ---------------------------------------------------------
//
// Ports CompareLogicallyAnsi (u_SQLite3Handler.pas:215-327) statement by statement. See the header
// comment and docs/specs/storage.md §2.15.1 for the semantics and the worked vectors. Legacy defect
// L-15 (docs/parity/legacy-defects.md): the port bounds every scan by the remaining length instead
// of reproducing legacy's over-read past the buffer when a string ends in digits. Deviation D-30
// (docs/parity/deviations.md).
int compareLogically(QByteArrayView a, QByteArrayView b) noexcept {
    // 1. Empty strings (legacy: "z1 = nil or n1 = 0").
    const bool aEmpty = a.data() == nullptr || a.size() == 0;
    const bool bEmpty = b.data() == nullptr || b.size() == 0;
    if (aEmpty) {
        return bEmpty ? 0 : -1;
    }
    if (bEmpty) {
        return 1;
    }

    const char* p1 = a.data();
    const char* p2 = b.data();
    qsizetype n1 = a.size();
    qsizetype n2 = b.size();

    for (;;) {
        // 2. At-end check: remaining length 0, OR an embedded NUL ends the string.
        const bool aAtEnd = n1 == 0 || *p1 == '\0';
        const bool bAtEnd = n2 == 0 || *p2 == '\0';
        if (aAtEnd) {
            return bAtEnd ? 0 : -1;
        }
        if (bAtEnd) {
            return 1;
        }

        // 3. Digit versus non-digit: the digit side sorts first.
        const bool aIsDigit = *p1 >= '0' && *p1 <= '9';
        const bool bIsDigit = *p2 >= '0' && *p2 <= '9';
        if (aIsDigit && !bIsDigit) {
            return -1;
        }
        if (!aIsDigit && bIsDigit) {
            return 1;
        }

        if (!aIsDigit) { // 4. Two non-digits: unsigned-byte, case-sensitive comparison.
            if (*p1 == *p2) {
                ++p1;
                ++p2;
                --n1;
                --n2;
                continue;
            }
            return static_cast<unsigned char>(*p1) < static_cast<unsigned char>(*p2) ? -1 : 1;
        }

        // 5. Two digit runs: accumulate as an unsigned 32-bit value, bounded by the remaining
        // length (this bound is the L-15 fix; legacy's `until not CharInSet(...)` has none).
        std::uint32_t v1 = 0;
        qsizetype c1 = 0;
        while (n1 > 0 && *p1 >= '0' && *p1 <= '9') {
            v1 = v1 * 10 + static_cast<std::uint32_t>(*p1 - '0');
            ++p1;
            --n1;
            ++c1;
        }
        std::uint32_t v2 = 0;
        qsizetype c2 = 0;
        while (n2 > 0 && *p2 >= '0' && *p2 <= '9') {
            v2 = v2 * 10 + static_cast<std::uint32_t>(*p2 - '0');
            ++p2;
            --n2;
            ++c2;
        }

        const auto sv1 = static_cast<std::int32_t>(v1);
        const auto sv2 = static_cast<std::int32_t>(v2);
        if (sv1 == sv2) {
            if (sv1 == 0) { // equal and zero: the shorter run (fewer leading zeros) is smaller
                if (c1 < c2) {
                    return -1;
                }
                if (c1 > c2) {
                    return 1;
                }
            }
            continue; // equal and non-zero: leading zeros ignored, keep comparing
        }
        return sv1 < sv2 ? -1 : 1;
    }
}

QString sqliteLibVersion() {
    return QStringLiteral("sqlite3 %1").arg(QString::fromUtf8(sqlite3_libversion()));
}

} // namespace cg::storage::sqlite
