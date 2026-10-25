// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLite/u_TileStorageSQLiteList.pas (:176-243 entry sync/uninit, :244-668
// TSQLiteSingleList, :669-738 TSQLiteDynList) and u_TileStorageSQLiteHelper.pas (:151-273: Create,
// Destroy, Sync, InternalGetHandler, InternalHandlerFactory).
//
// docs/specs/storage.md §3.3 (D-10, amended 2026-09-27/28): the open region files of one storage
// root. One entry per .sqlitedb path, each owning ONE SqliteRegionHandler (or the remembered fact
// that the file is absent -- legacy's FDBNotFound, List.pas:361) and ONE core::Mutex held for the
// whole of every operation on it. Legacy's fixed/dynamic entry arrays (a lookup-speed device for
// z1-z11 vs z12+) are not ported, only their observable results (legacy defect L-94, (a) deviates, deviation
// of T-0211): a caller says which kind of entry the file belongs to (EntryKind) and a Fixed entry then keeps
// legacy's state machine -- a write that creates the file initialises the entry (deviation of T-0211; legacy
// left it "not initialized" so the next READ returned no tile); a remembered absence lasts until a write; a
// failed open of an existing file is cached; none of these entries is closed by the sweep until initialized.
//
// Idle sweep: legacy's InternalSync closes an entry untouched for one whole sync interval; the
// ticket/spec restate that as "closed after kIdleCloseMs of idleness" (§2.16 intro).
//
// FEAT-STO-018.

#include "cg/core/sync.h"
#include "cg/storage/sqlite/sqlite_region_handler.h"
#include "cg/storage/sqlite/sqlite_region_schema.h"

#include <QString>
#include <QtGlobal>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <stop_token>
#include <unordered_map>

namespace cg::storage::sqlite {

struct RegionEntry; // defined in sqlite_region_pool.cpp

class SqliteRegionPool final {
public:
    // Idle time after which sweepIdle() closes a file: cStorageSyncInterval = 3 * 60 * 1000 ms.
    static constexpr quint32 kIdleCloseMs = 3 * 60 * 1000;

    // A pinned, read-mode hold on one open region file: the sweep never closes it while the lease
    // lives. The scan enumerator keeps a SELECT open across next() calls (spec §3.3 amendment) and
    // runs each step through run(), which takes the file's mutex for that step only.
    class Lease final {
    public:
        explicit Lease(std::shared_ptr<RegionEntry> entry);
        ~Lease();
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        void run(const std::function<void(SqliteRegionHandler&)>& fn);

    private:
        std::shared_ptr<RegionEntry> entry_;
    };

    // `useVersionFieldInDb` / `readOnly` are handed to every handler this pool opens (legacy's
    // Helper fields FUseVersion / FIsReadOnly).
    SqliteRegionPool(std::shared_ptr<SqliteRegionSchema> schema, bool useVersionFieldInDb, bool readOnly);
    ~SqliteRegionPool();

    SqliteRegionPool(const SqliteRegionPool&) = delete;
    SqliteRegionPool& operator=(const SqliteRegionPool&) = delete;

    [[nodiscard]] const std::shared_ptr<SqliteRegionSchema>& schema() const noexcept { return schema_; }
    [[nodiscard]] bool useVersionFieldInDb() const noexcept { return useVersionFieldInDb_; }

    // z1-z11 (zero-based zoom <= c_Max_Fixed_Zoom = 10) are legacy's fixed entries, z12+ its dynamic
    // ones (List.pas:244-668 vs :669-738). The caller derives it from the zoom of the tile.
    enum class EntryKind : quint8 { Fixed, Dynamic };

    // Ports InternalGetHandler + InternalHandlerFactory. Runs `fn` on the handler of region file
    // `dbPath` with that file's mutex held, and returns true; returns false without calling `fn`
    // when legacy returns a nil handler: the file is absent and `forWrite` is false (the absence
    // is remembered, no stat on later reads) or the pool is read-only (never creates), the open
    // failed, or `stop` is signalled (ports AOper.IsOperationCancelled; a Fixed entry checks it first, a
    // Dynamic one only before opening). A Fixed entry may also return false for an existing, readable file
    // (L-94: cached failed open). `forWrite` ports
    // AForceMakeDB: only then is a missing file created, after ForceDirectories of its folder.
    // T-0276: when `openError` is given and false is returned because the open of an existing file failed
    // (also a failed open cached by a Fixed entry), it receives the handler's failure text; it is left
    // untouched for every other reason of a false return.
    bool run(const QString& dbPath, EntryKind kind, bool forWrite, const std::stop_token& stop,
             const std::function<void(SqliteRegionHandler&)>& fn, QString* openError = nullptr);

    // Opens (never creates) `dbPath` for a scan and pins it. nullptr when the file cannot be
    // opened. Ignores the remembered "absent" fact: the scan found the file on disk.
    [[nodiscard]] std::unique_ptr<Lease> lease(const QString& dbPath);

    // Ports TTileStorageSQLiteHelper.Sync -> TSQLiteSingleList.Sync: closes every file idle for at
    // least kIdleCloseMs at tick `nowMs`, skipping any whose mutex is held or that has a Lease.
    // Takes the map mutex, then each file's mutex without blocking. `nowMs` is a wrapping 32-bit
    // millisecond tick (see currentTickMs()), injected for tests.
    void sweepIdle(quint32 nowMs);
    void sweepIdle() { sweepIdle(currentTickMs()); }

    // The application GC tick, gated as legacy's TListenerTTLCheck(Sync, cStorageSyncInterval)
    // (u_ListenerTime.pas:90-108; u_TileStorageSQLiteHelper.pas:174-179, 201-210): a tick that
    // finds the use flag set only stamps the time; once kIdleCloseMs have passed since that stamp
    // with no use, sweepIdle() runs, and a sweep that followed a use re-arms the gate. `nowMs` is
    // injected for tests. Every run()/lease() raises the use flag (CheckUseTimeUpdated).
    // FEAT-STO-018.
    void gcTick(quint32 nowMs);
    void gcTick() { gcTick(currentTickMs()); }

    // Wrapping 32-bit millisecond tick from the monotonic clock (legacy GetTickCount).
    [[nodiscard]] static quint32 currentTickMs() noexcept;

    // Test/diagnostic only: number of map entries (open files and remembered absences).
    [[nodiscard]] std::size_t entryCountForTest() const;

private:
    std::shared_ptr<RegionEntry> entryFor(const QString& dbPath);
    void checkUseTimeUpdated(); // legacy TListenerTTLCheck.CheckUseTimeUpdated
    SqliteRegionHandler* handlerLocked(RegionEntry& entry, const QString& dbPath, EntryKind kind,
                                       bool forWrite, bool ignoreNotFound, const std::stop_token& stop);

    std::shared_ptr<SqliteRegionSchema> schema_;
    bool useVersionFieldInDb_;
    bool readOnly_;

    // TListenerTTLCheck state and the helper's FIsNeedCleanup flag.
    std::atomic<bool> useFlag_{false};
    std::atomic<bool> updateFlag_{false};
    std::atomic<bool> needCleanup_{false};
    std::atomic<quint32> lastUseTime_{0};

    mutable core::Mutex mapMutex_; // ports FDynamicSync / the list holder locks
    std::unordered_map<QString, std::shared_ptr<RegionEntry>> map_;
};

} // namespace cg::storage::sqlite
