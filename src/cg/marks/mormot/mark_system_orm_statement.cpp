// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/mark_system_orm_statement.h"

#include "cg/marks/mormot/mark_system_orm_log.h"

#include <mutex>

namespace cg::marks {

using storage::sqlite::SqliteStmt;

bool runStatement(IMarkSystemImplORMClientProvider& provider, const QByteArray& sql,
                  const std::vector<OrmArg>& args, const std::function<void(const SqliteStmt&)>& onRow,
                  qint64* insertedId) {
    const std::lock_guard lock(provider.dbMutex());
    auto stmt = provider.db().prepare(sql);
    if (!stmt) {
        ormLogInfo("prepare failed: %s", qPrintable(stmt.error().what()));
        return false;
    }
    int index = 1;
    for (const OrmArg& arg : args) {
        core::Result<void> bound;
        if (std::holds_alternative<qint64>(arg)) {
            bound = stmt->bindInt64(index, std::get<qint64>(arg));
        } else if (std::holds_alternative<QString>(arg)) {
            bound = stmt->bindText(index, std::get<QString>(arg));
        } else {
            bound = stmt->bindBlob(index, std::get<QByteArray>(arg));
        }
        if (!bound) {
            ormLogInfo("bind failed: %s", qPrintable(bound.error().what()));
            return false;
        }
        ++index;
    }
    for (;;) {
        const auto step = stmt->step();
        if (!step) {
            ormLogInfo("step failed: %s", qPrintable(step.error().what()));
            return false;
        }
        if (*step == SqliteStmt::StepResult::Done) {
            break;
        }
        if (onRow) {
            onRow(*stmt);
        }
    }
    if (insertedId != nullptr) {
        *insertedId = provider.db().lastInsertRowId();
    }
    return true;
}

qint64 insertRow(IMarkSystemImplORMClientProvider& provider, const QByteArray& sql,
                 const std::vector<OrmArg>& args) {
    qint64 id = 0;
    if (!runStatement(provider, sql, args, nullptr, &id)) {
        return 0;
    }
    return id;
}

bool runWrite(IMarkSystemImplORMClientProvider& provider, const QByteArray& sql,
              const std::vector<OrmArg>& args) {
    return runStatement(provider, sql, args, nullptr);
}

qint64 selectRowId(IMarkSystemImplORMClientProvider& provider, const QByteArray& sql,
                   const std::vector<OrmArg>& args) {
    qint64 id = 0;
    bool got = false;
    runStatement(provider, sql, args, [&](const SqliteStmt& row) {
        if (!got) {
            id = row.columnInt64(0);
            got = true;
        }
    });
    return id;
}

} // namespace cg::marks
