// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLiteFile/u_TileStorageSQLiteFileConnectionPool.pas:51-104.
//
// docs/specs/storage.md §2.17 ("a LIFO free list under a mutex; empty list -> the maker builds a
// new connection"). A connection is used by exactly one thread at a time (SqliteDb is opened
// NOMUTEX; D-10's last sentence). There is no idle-close and no maximum size here -- legacy's pool
// has neither (ticket T-0022C1 scope: base + pool only, no per-format sweep timer).

#include "cg/storage/sqlite/sqlite_file_connection.h"

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace cg::storage::sqlite {

// Ports TTileStorageSQLiteFileConnectionPool. `maker` ports
// ITileStorageSQLiteFileConnectionBuilder.MakeNewConnection -- T-0022C4 supplies the actual format
// dispatch; this class only calls it when the free list is empty. `maker` returns a
// core::Result (T-0022C1-review.md F1): legacy's `Create` (reached through this pool's own
// `Acquire`, `ConnectionPool.pas:87`) can raise -- the same "Create reached through Acquire" case
// F1 lists for the connection base -- and MakeNewConnection has no try/except of its own, so the
// exception is uncaught and propagates straight out of Acquire. An error Result here reproduces
// that: acquire() propagates it to its own caller instead of returning a connection.
class SqliteFileConnectionPool final {
public:
    using ConnectionMaker = std::function<core::Result<std::unique_ptr<SqliteFileConnection>>()>;

    explicit SqliteFileConnectionPool(ConnectionMaker maker);
    ~SqliteFileConnectionPool() = default; // unique_ptrs in free_ release their own connections

    SqliteFileConnectionPool(const SqliteFileConnectionPool&) = delete;
    SqliteFileConnectionPool& operator=(const SqliteFileConnectionPool&) = delete;

    // Ports Acquire (:75-92): pops the most-recently-released connection (LIFO -- FList[Count-1]),
    // or calls `maker` when the list is empty. A successful Result never holds nullptr unless
    // `maker` itself returns one.
    [[nodiscard]] core::Result<std::unique_ptr<SqliteFileConnection>> acquire();

    // Ports Release (:94-102): pushes the connection back onto the free list (FList.Add).
    void release(std::unique_ptr<SqliteFileConnection> connection);

    // Test/diagnostic only -- not in legacy (TList exposes Count too, but nothing in
    // u_TileStorageSQLiteFileConnectionPool.pas reads it).
    [[nodiscard]] std::size_t freeCountForTest() const;

private:
    ConnectionMaker maker_;
    mutable std::mutex mutex_; // ports FLock (TCriticalSection): exclusive-only, no readers
    std::vector<std::unique_ptr<SqliteFileConnection>> free_; // ports FList as a LIFO stack
};

} // namespace cg::storage::sqlite
