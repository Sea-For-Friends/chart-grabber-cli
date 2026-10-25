// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"

#include "cg/marks/mormot/mark_system_orm_log.h"
#include "cg/marks/mormot/mark_system_orm_model.h"
#include "cg/marks/mormot/mark_system_orm_tools.h"
#include "cg/marks/mormot/mark_system_orm_transaction.h"
#include "cg/marks/mormot/mark_system_orm_types.h"
#include "cg/marks/mormot/system_nocase.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <tuple>

using cg::storage::sqlite::SqliteDb;
using cg::storage::sqlite::SqliteStmt;

namespace cg::marks {

namespace {

constexpr QLatin1StringView kDefUserName{"sasgis"};             // cDefUserName (:124)
constexpr QLatin1StringView kDefSqlite3DbFileName{"Marks.db3"}; // cDefSQLite3DbFileName (:125)

QString tr(const char* text) {
    return QCoreApplication::translate("MarkSystemImplORMClientProvider", text);
}

[[noreturn]] void raise(const QString& message) {
    throw MarkSystemORMError(message.toStdString());
}

// Throws the SQLite message of a failed statement of the bootstrap (mORMot raises ESqlite3Exception).
void mustExec(SqliteDb& db, QByteArrayView sql) {
    if (const auto done = db.exec(sql); !done) {
        raise(done.error().what());
    }
}

} // namespace

MarkSystemImplORMClientProvider::MarkSystemImplORMClientProvider(const QString& basePath,
                                                                 const IMarkSystemImplConfigORM& config)
    : db_(buildSqlite3Client(basePath, config)) {
    // The FUserID := 0 / FInitializeTableOptions := [itoNoIndex4TID] of :162-163 are the member
    // initialiser and the fixed arguments of createMissingTablesSqlite3.
    // Not ported: ctMongoDB -> BuildMongoDBClient, ctZDBC / ctODBC -> BuildDBMSClient (:473-478);
    // spec §10 U-3 / DV-4.
    transaction_ = std::make_unique<MarkSystemORMTransaction>(db_, mutex_); // :482 (B-13)
    initUserId(config.userName(), config.isReadOnly());
}

// :197-221 (B-1)
QString MarkSystemImplORMClientProvider::sqlite3DatabaseFileName(const QString& basePath,
                                                                 const QString& fileName) {
    QString name;
    QString path;
    if (!fileName.isEmpty()) {
        const qsizetype slash = fileName.lastIndexOf(QLatin1Char('/'));
        name = fileName.mid(slash + 1); // ExtractFileName
        if (name.isEmpty()) {
            name = kDefSqlite3DbFileName;
        }
        path = fileName.left(slash + 1); // ExtractFilePath: with the trailing delimiter
        if (path.isEmpty()) {
            path = basePath;
        } else if (QDir::isRelativePath(path)) {
            // RelativeToAbsolutePath (u_FileSystemFunc.pas:38-45): PathCombine(ExtractFilePath(base), rel).
            // ExtractFilePath keeps everything up to the last delimiter, so a base path without a
            // trailing delimiter loses its last component (legacy defect, see the handoff).
            const QString baseDir = basePath.left(basePath.lastIndexOf(QLatin1Char('/')) + 1);
            path = QDir::cleanPath(baseDir + path);
        }
    } else {
        name = kDefSqlite3DbFileName;
        path = basePath;
    }
    if (!path.endsWith(QLatin1Char('/'))) { // IncludeTrailingPathDelimiter
        path += QLatin1Char('/');
    }
    return path + name;
}

// :195-239 (B-2...B-12); the Mongo / DBMS builders are not ported.
SqliteDb MarkSystemImplORMClientProvider::buildSqlite3Client(const QString& basePath,
                                                             const IMarkSystemImplConfigORM& config) {
    const QString fileName = sqlite3DatabaseFileName(basePath, config.fileName());
    const QString dir = QFileInfo(fileName).absolutePath(); // ExtractFileDir
    if (!QFileInfo(dir).isDir()) {
        raise(tr("Directory does not exists: %1").arg(dir));
    }
    if (config.isReadOnly() && !QFileInfo(fileName).isFile()) {
        raise(tr("File does not exists: %1").arg(fileName));
    }

    // TRestClientDB.Create -> TSqlDatabase.Create (db/mormot.db.raw.sqlite3.pas:7536-7583). The
    // library opens with SQLITE_OPEN_READWRITE or SQLITE_OPEN_CREATE whatever the configuration says
    // (:6806-6807, :7567-7569), so a read-only configuration also opens read-write (B-3).
    auto opened = SqliteDb::open(fileName, SqliteDb::OpenMode::ReadWriteCreate);
    if (!opened) {
        raise(opened.error().what());
    }
    SqliteDb db = std::move(*opened);
    // B-4, B-5, B-7: ExecuteNoException / unchecked in legacy (:7689-7693, :7676-7679, :7582); only
    // B-6 gates.
    std::ignore = db.exec("PRAGMA page_size=4096"); // B-4: sent on every open
    std::ignore = db.exec("PRAGMA cache_size=10000");
    mustExec(db, "select count(*) from sqlite_master"); // B-6
    std::ignore = db.createCollation("SYSTEMNOCASE", &systemNoCaseCompare);
    std::ignore = db.exec("PRAGMA journal_mode=WAL;"); // B-8: WALMode := True, result ignored (:234)
    if (config.isReadOnly()) {
        return db; // :235-237
    }
    // CreateAllMissing (:181-193): CreateMissingTables(0, [itoNoIndex4TID]) then the SAS indexes.
    if (const auto created = createMissingTablesSqlite3(db); !created) { // B-9...B-11
        raise(created.error().what());
    }
    createMissingIndexesSqlite3(db); // B-12
    return db;
}

// :486-519 (B-14)
void MarkSystemImplORMClientProvider::initUserId(const QString& userName, bool isReadOnly) {
    const QString name = userName.isEmpty() ? QString(kDefUserName) : userName;

    const std::lock_guard lock(mutex_);
    // S-01. A failing SELECT is "not found" (ExecuteList returns nil, callers test Assigned).
    qint64 id = 0;
    if (auto stmt = db_.prepare("SELECT RowID FROM User WHERE uName=? LIMIT 1")) {
        if (stmt->bindText(1, name)) {
            if (const auto step = stmt->step(); step && *step == SqliteStmt::StepResult::Row) {
                id = stmt->columnInt64(0);
            }
        }
    } else {
        ormLogInfo("User select failed: %s", qUtf8Printable(stmt.error().what()));
    }
    if (id == 0) {
        if (isReadOnly) {
            throw MarkSystemORMError("MarkSystemORM: Can't init User in read-only mode!");
        }
        TransactionRec rec = transaction_->start(isReadOnly);
        try {
            // S-02; a failed INSERT leaves the ID 0 and CheckID raises.
            qint64 inserted = 0;
            if (auto stmt = db_.prepare("INSERT INTO User (uName) VALUES (?)")) {
                if (stmt->bindText(1, name) && stmt->step()) {
                    inserted = db_.lastInsertRowId();
                }
            }
            checkId(inserted);
            id = inserted;
            transaction_->commit(rec);
        } catch (...) {
            transaction_->rollBack(rec);
            throw;
        }
    }
    userId_ = id;
}

} // namespace cg::marks
