// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_region_handler.h"

namespace cg::storage::sqlite {

SqliteRegionHandler::SqliteRegionHandler(std::shared_ptr<SqliteRegionSchema> schema, QString dbFilename,
                                         bool useVersionFieldInDb, bool readOnly)
    : schema_(std::move(schema)), dbFilename_(std::move(dbFilename)),
      useVersionFieldInDb_(useVersionFieldInDb) {
    Q_ASSERT(schema_ != nullptr);

    // Ports the constructor's outer try/except (u_TileStorageSQLiteHandler.pas:390-399): ANY
    // failure below logs the (portable) library version, closes the connection and logs the
    // actual error, leaving isOpened() == false. Never throws.
    const auto failAndClose = [this](const QString& message) {
        schema_->logError(SqliteRegionSchema::LogKind::Init, sqliteLibVersion());
        schema_->logError(SqliteRegionSchema::LogKind::Init, message);
        openError_ = message;
        db_.reset();
    };

    if (readOnly) {
        // u_TileStorageSQLiteHandler.pas:376-379: URI form with immutable=1; no session/
        // established SQL and no structure check run on this path -- columnInfo_ stays default
        // (see the header's design-notes comment on why this is legacy behaviour, reproduced).
        const QString uri = QStringLiteral("file:///") + dbFilename_ + QStringLiteral("?immutable=1");
        auto opened = SqliteDb::open(uri, SqliteDb::OpenMode::ReadOnly, /*logicalCollation=*/true);
        if (!opened) {
            failAndClose(opened.error().what());
            return;
        }
        db_ = std::make_unique<SqliteDb>(std::move(*opened));
        return;
    }

    auto opened = SqliteDb::open(dbFilename_, SqliteDb::OpenMode::ReadWriteCreate, /*logicalCollation=*/true);
    if (!opened) {
        failAndClose(opened.error().what());
        return;
    }
    db_ = std::make_unique<SqliteDb>(std::move(*opened));

    // ExecMakeSession(s.sql / default).
    for (const QByteArray& stmt : schema_->sessionSql()) {
        auto result = db_->exec(stmt);
        if (!result) {
            failAndClose(result.error().what());
            return;
        }
    }

    // InternalCheckStructure: discover, create the table if missing, discover again.
    if (auto discovered = discoverColumns(); !discovered) {
        failAndClose(discovered.error().what());
        return;
    }
    if (columnInfo_.columnCount == 0) {
        for (const QByteArray& stmt : schema_->newTableSql()) {
            auto result = db_->exec(stmt);
            if (!result) {
                failAndClose(result.error().what());
                return;
            }
        }
        if (auto discovered = discoverColumns(); !discovered) {
            failAndClose(discovered.error().what());
            return;
        }
    }
    if (columnInfo_.columnCount < 5 || columnInfo_.columnCount > 8) {
        // Ports LogError(c_Log_Init, 'Invalid column count') with the default ARaiseError = False:
        // informational only, does not abort the open.
        schema_->logError(SqliteRegionSchema::LogKind::Init, QStringLiteral("Invalid column count"));
    }

    // ExecEstablished(e.sql / none).
    for (const QByteArray& stmt : schema_->establishedSql()) {
        auto result = db_->exec(stmt);
        if (!result) {
            failAndClose(result.error().what());
            return;
        }
    }

    // Deviation D-6 already covers SetBusyTryCount(3): SqliteDb::open sets sqlite3_busy_timeout
    // unconditionally, so nothing more is needed here.
}

SqliteRegionHandler::~SqliteRegionHandler() = default;

core::Result<void> SqliteRegionHandler::discoverColumns() {
    // Ports InternalCheckStructure's FillChar(FTBColInfo, SizeOf(FTBColInfo), 0)
    // (u_TileStorageSQLiteHandler.pas:1043): safe to re-zero on every call here because the first
    // of the (at most two) calls per construction never mutates these fields when it fails (the
    // callback only runs on a successful prepare).
    columnInfo_ = ColumnInfo{};
    if (!db_) {
        return {};
    }

    // Ports DeclareSql's ARaiseOnOpenError = False (u_TileStorageSQLiteHandler.pas:1046-1051): a
    // prepare failure (typically "no such table: t") is silent, columnCount stays 0.
    auto stmt = db_->prepare(QByteArrayView("SELECT * FROM t WHERE 0=1"));
    if (!stmt) {
        return {};
    }

    // DeclareSql (u_SQLite3Handler.pas:594-596) steps exactly once and invokes the callback
    // regardless of whether that step returned a row -- column metadata is fixed at prepare time
    // either way. But DeclareSql ends with an UNCONDITIONAL `CheckResult(sqlite3_finalize(...))`
    // (:605), which raises into Create's `except` on a step error that survives to finalize (BUSY
    // past the timeout, IOERR, CORRUPT, ...) regardless of ARaiseOnOpenError -- so, unlike the
    // prepare failure above, this one must fail the open (T-0022B2 review finding 3).
    auto stepResult = stmt->step();
    if (!stepResult) {
        return core::fail(stepResult.error().code(), stepResult.error().what());
    }

    const int count = stmt->columnCount();
    columnInfo_.columnCount = count;

    // Ports CallbackSelectCols (:486-525).
    int found = 0;
    for (int i = 0; i < count && found < 3; ++i) {
        const QByteArray name = stmt->columnName(i);
        if (name.compare("v", Qt::CaseInsensitive) == 0) {
            const QByteArray declType = stmt->columnDeclType(i);
            columnInfo_.hasVersion = true;
            columnInfo_.versionMode = declType.compare("TEXT", Qt::CaseInsensitive) == 0
                                          ? VersionColMode::Text
                                          : VersionColMode::Int;
            ++found;
        } else if (name.compare("c", Qt::CaseInsensitive) == 0) {
            columnInfo_.hasContentType = true;
            ++found;
        } else if (name.compare("h", Qt::CaseInsensitive) == 0) {
            columnInfo_.hasCrc = true;
            ++found;
        }
    }
    return {};
}

} // namespace cg::storage::sqlite
