// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/mark_system_orm_transaction.h"

#include "cg/marks/mormot/mark_system_orm_log.h"
#include "cg/marks/mormot/mark_system_orm_types.h"

#include <QDebug>

#include <tuple>

namespace cg::marks {

MarkSystemORMTransaction::MarkSystemORMTransaction(storage::sqlite::SqliteDb& db,
                                                   std::recursive_mutex& dbMutex)
    : db_(db), mutex_(dbMutex) {}

// :67-83
TransactionRec MarkSystemORMTransaction::start(bool isReadOnly) {
    TransactionRec rec;
    rec.isReadOnly = isReadOnly;
    if (isReadOnly) {
        return rec;
    }
    const std::lock_guard lock(mutex_);
    rec.sessionId = 1; // legacy GetTickCount: only "non-zero" is observable
    if (active_) {
        rec.isInternal = false; // joins the open transaction
        return rec;
    }
    // A refused BEGIN: legacy leaves the client's flag set (orm/mormot.orm.client.pas:660-670), so every
    // later Start joins and nothing is ever committed. Not reproduced (D-79): the flag stays clear.
    // TransactionBegin: BEGIN TRANSACTION; (db/mormot.db.raw.sqlite3.pas:6761-6777)
    if (!db_.exec("BEGIN TRANSACTION;")) {
        throw MarkSystemORMError("MarkSystemORM: Start transaction is failed!");
    }
    active_ = true;
    rec.isInternal = true;
    return rec;
}

// :85-95 (never raises, see below). Legacy asserts sessionId > 0 here; the guard below simply does nothing
// for 0.
void MarkSystemORMTransaction::commit(TransactionRec& rec) {
    if (rec.isReadOnly || !rec.isInternal || rec.sessionId == 0) {
        return;
    }
    const std::lock_guard lock(mutex_);
    // COMMIT TRANSACTION; (db/mormot.db.raw.sqlite3.pas:6779-6788). Legacy never raises: the call
    // reaches TRestOrmServerDB.Commit (orm/mormot.orm.sqlite3.pas:2328-2339), which swallows the
    // exception, and the flags are reset either way. A failure is logged and the caller sees success.
    if (const auto done = db_.exec("COMMIT TRANSACTION;"); !done) {
        qCWarning(logMarksOrm()) << "COMMIT failed, ignored as in legacy:" << done.error().what();
    }
    active_ = false;
    rec.sessionId = 0;
}

// :97-108
void MarkSystemORMTransaction::rollBack(TransactionRec& rec) noexcept {
    if (rec.isReadOnly || !rec.isInternal || rec.sessionId == 0) {
        return;
    }
    const std::lock_guard lock(mutex_);
    std::ignore = db_.exec("ROLLBACK TRANSACTION;"); // errors swallowed
    active_ = false;
    rec.sessionId = 0;
}

} // namespace cg::marks
