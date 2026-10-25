// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/u_MarkSystemORMTransaction.pas (docs/specs/marks.md §2.9.5).
// Not ported: TMarkSystemORMTransactionNoOp (:109-124), used only by the MongoDB client.

#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"

#include <mutex>

namespace cg::marks {

// TMarkSystemORMTransaction (:34-44). mORMot's TransactionActiveSession <> 0 is `active_`, guarded by
// the database mutex. The test "is a transaction open" is per connection, not per thread (LC-15).
class MarkSystemORMTransaction final : public IMarkSystemORMTransaction {
public:
    MarkSystemORMTransaction(storage::sqlite::SqliteDb& db, std::recursive_mutex& dbMutex);

    [[nodiscard]] TransactionRec start(bool isReadOnly) override;
    void commit(TransactionRec& rec) override;
    void rollBack(TransactionRec& rec) noexcept override;

private:
    storage::sqlite::SqliteDb& db_;
    std::recursive_mutex& mutex_;
    bool active_ = false;
};

} // namespace cg::marks
