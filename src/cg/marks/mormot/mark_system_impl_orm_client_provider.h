// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/i_MarkSystemImplORMClientProvider.pas (interfaces, TTransactionRec) and
// u_MarkSystemImplORMClientProvider.pas (SQLite path :150-239, :472-549; docs/specs/marks.md §2.9.3).
//
// Not ported: BuildMongoDBClient, BuildDBMSClient (:241-470) and the client types ctMongoDB / ctZDBC /
// ctODBC; GetRestClientType (one client type: every `if FClientType = ctSQLite3` of the other units is
// taken as true); the dynamic sqlite3 library loader (:127-144); the TOrmModel instance (the model is
// static, mark_system_orm_model.h).

#include "cg/marks/mormot/mark_system_impl_config_orm.h"
#include "cg/storage/sqlite/sqlite_db.h"

#include <QString>
#include <QtGlobal>

#include <memory>
#include <mutex>

namespace cg::marks {

// TTransactionRec (i_...:34-38).
struct TransactionRec {
    bool isReadOnly = true;
    bool isInternal = false;
    quint32 sessionId = 0;
};

// IMarkSystemORMTransaction (i_...:40-45); the TOrmClass argument of Start only selected a mORMot
// table lock and is dropped.
class IMarkSystemORMTransaction {
public:
    virtual ~IMarkSystemORMTransaction() = default;
    [[nodiscard]] virtual TransactionRec start(bool isReadOnly) = 0; // throws MarkSystemORMError
    virtual void commit(TransactionRec& rec) = 0; // never raises: a failed COMMIT is logged
    virtual void rollBack(TransactionRec& rec) noexcept = 0;
};

// IMarkSystemImplORMClientProvider (i_...:47-60). `db()` is GetRestClient; the lock every statement
// runs under (mORMot's DB.Lock) is exposed as `dbMutex()`.
class IMarkSystemImplORMClientProvider {
public:
    virtual ~IMarkSystemImplORMClientProvider() = default;
    [[nodiscard]] virtual qint64 userId() const = 0;
    [[nodiscard]] virtual storage::sqlite::SqliteDb& db() = 0;
    [[nodiscard]] virtual std::recursive_mutex& dbMutex() = 0;
    [[nodiscard]] virtual IMarkSystemORMTransaction& transaction() = 0;
};

// TMarkSystemImplORMClientProvider (u_...:67-110), SQLite client only. The constructor does Build
// (:471-484): opens (creating) the file, bootstraps the schema, makes the transaction object and
// finds or creates the user. Throws MarkSystemORMError on every failure; no half-built object exists.
class MarkSystemImplORMClientProvider final : public IMarkSystemImplORMClientProvider {
public:
    MarkSystemImplORMClientProvider(const QString& basePath, const IMarkSystemImplConfigORM& config);

    [[nodiscard]] qint64 userId() const override { return userId_; }
    [[nodiscard]] storage::sqlite::SqliteDb& db() override { return db_; }
    [[nodiscard]] std::recursive_mutex& dbMutex() override { return mutex_; }
    [[nodiscard]] IMarkSystemORMTransaction& transaction() override { return *transaction_; }

private:
    static QString sqlite3DatabaseFileName(const QString& basePath, const QString& fileName); // :197-221
    static storage::sqlite::SqliteDb buildSqlite3Client(const QString& basePath,
                                                        const IMarkSystemImplConfigORM& config); // :195-239
    void initUserId(const QString& userName, bool isReadOnly);                                   // :486-519

    std::recursive_mutex mutex_;
    storage::sqlite::SqliteDb db_;
    std::unique_ptr<IMarkSystemORMTransaction> transaction_;
    qint64 userId_ = 0;
};

} // namespace cg::marks
