// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ReadWriteSyncSRW.pas (the legacy Windows backend: a slim reader/writer lock).
// New code (T-0335). core::Mutex is this type. On POSIX it is std::shared_mutex. On Windows it is the system
// SRW lock: std::shared_mutex of MinGW-w64 (winpthreads, GCC 16.2 of MSYS2 UCRT64) let two writers in at once
// (a plain unique_lock loop of 6 threads broke exclusion in about 1 of 3000 rounds;
// TileDownloadRequestBuilderFactory ConcurrentFirstCalls... failed in most runs), so it cannot be used there.

#include <shared_mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace cg::core::platform {

#ifdef _WIN32
class SharedMutex final {
public:
    SharedMutex() = default;
    SharedMutex(const SharedMutex&) = delete;
    SharedMutex& operator=(const SharedMutex&) = delete;

    void lock() { ::AcquireSRWLockExclusive(&lock_); }
    [[nodiscard]] bool try_lock() { return ::TryAcquireSRWLockExclusive(&lock_) != 0; }
    void unlock() { ::ReleaseSRWLockExclusive(&lock_); }
    void lock_shared() { ::AcquireSRWLockShared(&lock_); }
    [[nodiscard]] bool try_lock_shared() { return ::TryAcquireSRWLockShared(&lock_) != 0; }
    void unlock_shared() { ::ReleaseSRWLockShared(&lock_); }

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
};
#else
using SharedMutex = std::shared_mutex;
#endif

} // namespace cg::core::platform
