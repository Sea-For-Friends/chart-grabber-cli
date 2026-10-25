// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileInfoBasicMemCache.pas, u_TileInfoBasicMemCache.pas (TTileInfoBasicMemCache).
//
// docs/specs/storage.md §2.9 (:550-623). One instance sits in front of every persistent backend,
// owned by the future `TMapType`-equivalent facade (out of scope here) and caches `TileInfo`
// values *including negatives* -- a NotExists result is cached just as eagerly as a hit.
//
// Locking: legacy's `Get` takes the WRITE lock even on a pure read (`FCS.BeginWrite` in
// u_TileInfoBasicMemCache.pas:362) because a hit may rewrite the deadline and a `OneByOne` miss
// may evict -- ported as `core::WriteLock` on every mutating/lookup call; a shared/read lock there
// would be a data race. `enumerate()` is the one exception: legacy's `GetEnum`
// (u_TileInfoBasicMemCache.pas:538-544) reads `FList` with NO lock at all, a genuine race in the
// original (Delphi's `TList` has the same reallocate-under-iteration hazard as `std::vector`, just
// less likely to be caught). The port takes `core::ReadLock` there instead -- observably identical
// for any single-threaded caller, and required for a TSan-clean build under concurrent use.
//
// TTL is a deadline (`GetTickCount + FTTL`), not a timestamp; an entry is live while
// `deadline > now`. Per the spec's idiom note (§5, "do not make the utility self-debouncing") this
// class owns no timer: the caller's own `QTimer`/GC-tick subscriber calls `clearExpired()`
// directly. `now()` itself is supplied by an injectable `Clock` (default
// `std::chrono::steady_clock::now`), the same test-injection shape the net GC-tick consumers use for their
// owners (T-0011B) -- production callers still construct with exactly the spec's 3 arguments;
// tests pass a 4th, fake clock to exercise TTL expiry/refresh without sleeping.

#include "cg/core/notifier.h"
#include "cg/core/sync.h"
#include "cg/geo/geo_types.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_info.h"
#include "cg/storage/tile_pyramid_notifier.h" // TileUpdate

#include <QtGlobal>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace cg::storage {

// Ports TClearByTTLStrategy (u_TileInfoBasicMemCache.pas:42-47). The integer values are the
// on-disk values of the ZMP key MemCacheClearStrategy -- keep them.
enum class ClearStrategy : std::uint8_t { ByOldest = 0, ByYoungest = 1, OneByOne = 2, Never = 3 };

// FEAT-STO-034
class TileInfoMemCache final {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    TileInfoMemCache(int capacity, std::chrono::milliseconds ttl, ClearStrategy strategy,
                     Clock clock = &std::chrono::steady_clock::now);

    TileInfoMemCache(const TileInfoMemCache&) = delete;
    TileInfoMemCache& operator=(const TileInfoMemCache&) = delete;

    // Ports Add (u_TileInfoBasicMemCache.pas:209-293). Key is (x, y, zoom,
    // version->storeString()), compared case-sensitively (`SameStr`); a null version compares as
    // "". On a fresh key: reuse an empty slot, else grow while `count < capacity`, else evict the
    // entry with the smallest deadline. There is no LRU and no hash map -- a linear scan, exactly
    // as legacy does it (do not "improve" this without measuring; the eviction order is observable
    // through `clearedByTtl()`/`setOnTileInfoUpdate()`).
    void add(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version, TileInfo info);

    // Ports Remove (u_TileInfoBasicMemCache.pas:295-344). Removes regardless of expiry.
    bool remove(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version);

    // Ports Get (u_TileInfoBasicMemCache.pas:346-417). nullopt = miss. `updateTtl` refreshes the
    // entry's deadline on a hit. An expired entry is always a miss; `OneByOne` additionally evicts
    // it right there. `mode == LoadMode::WithData` is a miss when the cached entry has no data
    // (legacy: `Supports(..., ITileInfoWithData)` fails) -- a data-less entry (including a
    // `NotExists`/`TneExists` negative-cache entry) is a miss for `WithData`, never promoted to a
    // hit. `WithoutData`/`AsIs` return whatever is cached.
    [[nodiscard]] std::optional<TileInfo> get(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version,
                                              LoadMode mode, bool updateTtl);

    // Ports Clear (u_TileInfoBasicMemCache.pas:419-435).
    void clear();

    // Ports ClearByTTL (u_TileInfoBasicMemCache.pas:437-497): `ByOldest` wipes the whole cache once
    // the MINIMUM deadline among live entries has passed; `ByYoungest` wipes the whole cache once
    // the MAXIMUM deadline has passed (the ZMP default); `OneByOne` drops only expired entries;
    // `Never` does nothing (returns immediately, no lock taken, no notification). Every other call
    // fires `clearedByTtl()` unconditionally, even when nothing was actually evicted -- legacy's
    // `FClearByTTLNotifier.Notify(nil)` sits in an unconditional `finally`, not behind the
    // "something was cleaned" flag.
    void clearExpired();

    // Ports GetEnum (u_TileInfoBasicMemCache.pas:538-603). Only `Exists`/`TneExists` entries are
    // ever produced (a cached plain `NotExists` negative never appears, matching legacy: neither
    // `Supports(ITileInfoWithData)` nor `IsExistsTNE` holds for it); `ignoreTne` additionally drops
    // `TneExists`. `ignoreMultiVersion` mirrors legacy's `AIgnoreMultiVersionTiles` parameter,
    // which `TMemCacheEnum.Create` accepts but never reads (u_TileInfoBasicMemCache.pas:548-599) --
    // kept for interface parity only.
    [[nodiscard]] EnumTileInfoPtr enumerate(bool ignoreTne, bool ignoreMultiVersion) const;

    // Ports GetClearByTTLNotifier. Fires (with no payload) every time clearExpired() runs and the
    // strategy is not Never -- see clearExpired()'s doc above.
    [[nodiscard]] core::Notifier& clearedByTtl() noexcept { return clearedByTtl_; }

    // Ports SetOnTileInfoUpdate. Called for every add()/evict() while the cache's own write lock is
    // held, exactly as legacy calls it from inside `FCS.BeginWrite` -- a callback that re-enters
    // this TileInfoMemCache will deadlock, same as legacy's listener contract.
    void setOnTileInfoUpdate(std::function<void(const TileUpdate&)> cb);

private:
    struct Entry {
        bool empty = true;
        geo::PointI xy;
        quint8 zoom = 0;
        MapVersionInfoPtr version;
        TileInfo info;
        std::chrono::steady_clock::time_point deadline;
    };

    void notifyLocked(const Entry& e);
    void makeItCleanLocked(Entry& e); // ports MakeItClean (u_TileInfoBasicMemCache.pas:499-511)

    int capacity_;
    std::chrono::milliseconds ttl_;
    ClearStrategy clearStrategy_;
    Clock clock_;

    mutable core::Mutex mutex_;
    std::vector<Entry> entries_;
    core::Notifier clearedByTtl_;
    std::function<void(const TileUpdate&)> onTileInfoUpdate_;
};

} // namespace cg::storage
