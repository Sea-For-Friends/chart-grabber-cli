// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_file_connection_pool.h"

#include <utility>

namespace cg::storage::sqlite {

SqliteFileConnectionPool::SqliteFileConnectionPool(ConnectionMaker maker) : maker_(std::move(maker)) {}

core::Result<std::unique_ptr<SqliteFileConnection>> SqliteFileConnectionPool::acquire() {
    // Ports Acquire's own FLock.Acquire/Release span (:79-91), which legacy holds around the
    // MakeNewConnection call too (the builder's own FLock is then acquired a second time inside
    // it) -- reproduced verbatim: two threads racing an empty pool serialise on this lock, the
    // second one re-checking (and finding empty again) only after the first connection finishes
    // opening, not building two connections in parallel.
    const std::lock_guard lock(mutex_);
    if (!free_.empty()) {
        std::unique_ptr<SqliteFileConnection> connection = std::move(free_.back());
        free_.pop_back();
        return connection;
    }

    if (!maker_) {
        return std::unique_ptr<SqliteFileConnection>();
    }
    return maker_();
}

void SqliteFileConnectionPool::release(std::unique_ptr<SqliteFileConnection> connection) {
    if (connection == nullptr) {
        return;
    }
    const std::lock_guard lock(mutex_);
    free_.push_back(std::move(connection));
}

std::size_t SqliteFileConnectionPool::freeCountForTest() const {
    const std::lock_guard lock(mutex_);
    return free_.size();
}

} // namespace cg::storage::sqlite
