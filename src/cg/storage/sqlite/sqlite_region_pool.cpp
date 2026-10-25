// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_region_pool.h"

#include <QDir>
#include <QFileInfo>

#include <chrono>
#include <utility>

namespace cg::storage::sqlite {

struct RegionEntry {
    core::Mutex mutex; // ports FFixSync: held for the whole of every operation on this file
    std::unique_ptr<SqliteRegionHandler> handler;
    bool notFound = false;    // ports FDBNotFound
    bool fixed = false;       // z1-z11 entry (L-94)
    QString openError;        // T-0276: why the last open of this file failed (empty: it did not fail)
    bool initialized = false; // ports FOpState >= 2 (fixed entries only)
    bool cachedNil = false;   // fixed entry in state 2 whose open failed or was cancelled: run() returns nil
    bool dead = false;        // erased from the map by the sweep; a waiter must look the entry up again
    quint32 lastUsed = 0;
    std::atomic<int> pins{0}; // live Leases
};

SqliteRegionPool::Lease::Lease(std::shared_ptr<RegionEntry> entry) : entry_(std::move(entry)) {
    entry_->pins.fetch_add(1);
}

SqliteRegionPool::Lease::~Lease() {
    entry_->pins.fetch_sub(1);
}

void SqliteRegionPool::Lease::run(const std::function<void(SqliteRegionHandler&)>& fn) {
    const core::WriteLock lock(entry_->mutex);
    fn(*entry_->handler);
}

SqliteRegionPool::SqliteRegionPool(std::shared_ptr<SqliteRegionSchema> schema, bool useVersionFieldInDb,
                                   bool readOnly)
    : schema_(std::move(schema)), useVersionFieldInDb_(useVersionFieldInDb), readOnly_(readOnly) {}

SqliteRegionPool::~SqliteRegionPool() = default;

quint32 SqliteRegionPool::currentTickMs() noexcept {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
    return static_cast<quint32>(ms.count());
}

std::size_t SqliteRegionPool::entryCountForTest() const {
    const core::ReadLock lock(mapMutex_);
    return map_.size();
}

void SqliteRegionPool::checkUseTimeUpdated() {
    if (!updateFlag_.load()) {
        updateFlag_.store(true);
        useFlag_.store(true);
    }
}

void SqliteRegionPool::gcTick(quint32 nowMs) {
    if (useFlag_.exchange(false)) {
        lastUseTime_.store(nowMs);
        return;
    }
    const quint32 lastUse = lastUseTime_.load();
    if (lastUse == 0) {
        return;
    }
    const quint32 cleanTime = lastUse + kIdleCloseMs;
    if (cleanTime <= nowMs || (nowMs < (1U << 29) && cleanTime > (1U << 30))) {
        updateFlag_.store(false);
        sweepIdle(nowMs); // TTileStorageSQLiteHelper.Sync
        if (needCleanup_.exchange(false)) {
            checkUseTimeUpdated();
        }
        lastUseTime_.store(0);
    }
}

std::shared_ptr<RegionEntry> SqliteRegionPool::entryFor(const QString& dbPath) {
    checkUseTimeUpdated(); // InternalGetHandler: CheckUseTimeUpdated + FIsNeedCleanup.SetFlag
    needCleanup_.store(true);
    const core::WriteLock lock(mapMutex_);
    std::shared_ptr<RegionEntry>& slot = map_[dbPath];
    if (!slot) {
        slot = std::make_shared<RegionEntry>();
        slot->lastUsed = currentTickMs();
    }
    return slot;
}

// Ports GetFixedHandler (List.pas:489-566) / GetDynHandler (:334-449) plus InternalHandlerFactory
// (u_TileStorageSQLiteHelper.pas:216-273). Caller holds entry.mutex. Never loops.
SqliteRegionHandler* SqliteRegionPool::handlerLocked(RegionEntry& entry, const QString& dbPath,
                                                     EntryKind kind, bool forWrite, bool ignoreNotFound,
                                                     const std::stop_token& stop) {
    const bool fixed = kind == EntryKind::Fixed;
    if (fixed) {
        entry.fixed = true;
        if (stop.stop_requested()) { // GetFixedHandler: IsOperationCancelled at the top (:502-505)
            return nullptr;
        }
        if (entry.initialized) {
            return entry.cachedNil
                       ? nullptr
                       : entry.handler.get(); // state >= 2; nil when the open failed (cached, :531-532)
        }
    } else if (entry.handler) {
        return entry.handler.get();
    }
    if (entry.notFound && !forWrite && !ignoreNotFound) {
        return nullptr; // remembered absence: no stat on every read (List.pas:361, :535-538)
    }

    // InternalHandlerFactory
    const QFileInfo info(dbPath);
    const bool missing = !(info.exists() && !info.isDir()); // Delphi FileExists
    if (!ignoreNotFound) {
        entry.notFound = missing; // a scan's lease must not change what Fixed run() sees
    }
    if (missing) {
        entry.openError.clear();
        if (readOnly_ || !forWrite) {
            return nullptr;
        }
        QDir().mkpath(info.absolutePath()); // ForceDirectories(ExtractFilePath(path))
    }
    if (stop.stop_requested()) {
        if (fixed) { // Helper.pas:253-256 returns nil and the list goes to state 2 (:531-532)
            entry.initialized = !entry.notFound;
            entry.cachedNil = entry.initialized && !entry.handler;
        }
        return nullptr;
    }
    if (!entry.handler) {
        auto handler =
            std::make_unique<SqliteRegionHandler>(schema_, dbPath, useVersionFieldInDb_, readOnly_);
        entry.openError = handler->openError();
        if (handler->isOpened()) {
            entry.handler = std::move(handler);
        }
    }
    if (fixed) {
        // Deviation of T-0211 (docs/parity/deviations.md; legacy L-94(a)): legacy leaves the entry in state 0
        // while FDBNotFound is set (:529-530), so reads find nothing until the next write; here a creating
        // write that produced a handler initialises it. A failed create stays "not initialized" as in legacy.
        entry.initialized = !entry.notFound || entry.handler != nullptr;
        entry.cachedNil = entry.initialized && !entry.handler;
    }
    return entry.handler.get();
}

// FEAT-STO-018
bool SqliteRegionPool::run(const QString& dbPath, EntryKind kind, bool forWrite, const std::stop_token& stop,
                           const std::function<void(SqliteRegionHandler&)>& fn, QString* openError) {
    for (;;) {
        const std::shared_ptr<RegionEntry> entry = entryFor(dbPath);
        const core::WriteLock lock(entry->mutex);
        if (entry->dead) {
            continue; // swept between lookup and lock
        }
        SqliteRegionHandler* handler = handlerLocked(*entry, dbPath, kind, forWrite, false, stop);
        entry->lastUsed = currentTickMs(); // ports FSyncCallListener.CheckUseTimeUpdated / FOpState++
        if (handler == nullptr) {
            if (openError != nullptr && !stop.stop_requested()) {
                *openError = entry->openError;
            }
            return false;
        }
        fn(*handler);
        return true;
    }
}

std::unique_ptr<SqliteRegionPool::Lease> SqliteRegionPool::lease(const QString& dbPath) {
    for (;;) {
        const std::shared_ptr<RegionEntry> entry = entryFor(dbPath);
        const core::WriteLock lock(entry->mutex);
        if (entry->dead) {
            continue;
        }
        SqliteRegionHandler* handler = handlerLocked(*entry, dbPath, EntryKind::Dynamic, /*forWrite=*/false,
                                                     /*ignoreNotFound=*/true, std::stop_token{});
        entry->lastUsed = currentTickMs();
        if (handler == nullptr) {
            return nullptr;
        }
        return std::make_unique<Lease>(entry); // pinned before `lock` releases
    }
}

void SqliteRegionPool::sweepIdle(quint32 nowMs) {
    const core::WriteLock mapLock(mapMutex_);
    for (auto it = map_.begin(); it != map_.end();) {
        const std::shared_ptr<RegionEntry> keep = it->second; // outlives the unique_lock below
        std::unique_lock<core::Mutex> fileLock(keep->mutex, std::try_to_lock);
        // legacy's InternalSync leaves a fixed entry below state 2 alone (:183-185). A negative
        // signed difference means `lastUsed` is newer than `nowMs`: not idle.
        if (!fileLock.owns_lock() || keep->pins.load() > 0 || (keep->fixed && !keep->initialized) ||
            static_cast<qint32>(nowMs - keep->lastUsed) < static_cast<qint32>(kIdleCloseMs)) {
            ++it;
            continue;
        }
        keep->dead = true;
        keep->handler.reset(); // closes the sqlite connection under the file's mutex
        it = map_.erase(it);
    }
}

} // namespace cg::storage::sqlite
