// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing of the SAS tree (the call sites are the ORM statements of u_MarkCategoryDbImplORMHelper and
// u_MarkDbImplORMHelper; mORMot's own execute path).
// Shared by the SQLite backend helpers (categories/ and marks/): runs one statement the way mORMot does,
// under the connection mutex ("one lock per statement", docs/specs/marks.md §2.9.5). Hoisted from the
// anonymous namespaces of the category helper (T-0086) and the mark helper (T-0095).

#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <functional>
#include <variant>
#include <vector>

namespace cg::marks {

// A bound parameter: ids, flags and numbers are bound as int64, text as text, a blob as a blob.
using OrmArg = std::variant<qint64, QString, QByteArray>;

// Prepares, binds and steps one statement to the end. `onRow` is called for every row; returns false when
// preparing, binding or stepping failed (a failed write is reported by the caller's Check*, a failed SELECT
// is "no rows"). `insertedId` receives last_insert_rowid() after a successful run.
bool runStatement(IMarkSystemImplORMClientProvider& provider, const QByteArray& sql,
                  const std::vector<OrmArg>& args,
                  const std::function<void(const storage::sqlite::SqliteStmt&)>& onRow,
                  qint64* insertedId = nullptr);

// INSERT: the new RowID, 0 when the insert failed (CheckID then throws).
qint64 insertRow(IMarkSystemImplORMClientProvider& provider, const QByteArray& sql,
                 const std::vector<OrmArg>& args);

// UPDATE / DELETE: true when the statement ran (zero matching rows is a success, as in mORMot).
bool runWrite(IMarkSystemImplORMClientProvider& provider, const QByteArray& sql,
              const std::vector<OrmArg>& args = {});

// The first column of the first row of a SELECT RowID ... LIMIT 1, 0 when there is none.
qint64 selectRowId(IMarkSystemImplORMClientProvider& provider, const QByteArray& sql,
                   const std::vector<OrmArg>& args);

// Runs `body`; commits when `useTransactions` and the body returned, rolls back and rethrows when it threw
// (the legacy `Start; try ... Commit; except RollBack; raise; end` of the mark helper). Start is outside the
// guard, as in legacy: a refused Start throws with nothing to roll back.
template <class Body>
void inTransaction(IMarkSystemImplORMClientProvider& provider, bool useTransactions, bool isReadOnly,
                   Body&& body) {
    TransactionRec transaction;
    if (useTransactions) {
        transaction = provider.transaction().start(isReadOnly);
    }
    try {
        body();
        if (useTransactions) {
            provider.transaction().commit(transaction);
        }
    } catch (...) {
        if (useTransactions) {
            provider.transaction().rollBack(transaction);
        }
        throw;
    }
}

// mORMot's text of a Double inside an INSERT / UPDATE (docs/specs/marks.md 2.9.2): 15 significant digits,
// spliced into the statement as a numeric literal.
inline QByteArray doubleLiteral(double value) {
    return QByteArray::number(value, 'g', 15);
}

} // namespace cg::marks
