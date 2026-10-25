// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/tile_info_mem_cache.h"

#include <algorithm>
#include <utility>

namespace cg::storage {

namespace {
QString versionKey(const MapVersionInfoPtr& v) {
    return v ? v->storeString() : QString();
}
} // namespace

TileInfoMemCache::TileInfoMemCache(int capacity, std::chrono::milliseconds ttl, ClearStrategy strategy,
                                   Clock clock)
    : capacity_(capacity), ttl_(ttl), clearStrategy_(strategy), clock_(std::move(clock)) {
    // L-12 (reliability audit 2026-09-28): no up-front reserve -- legacy's TList grows lazily
    // (u_TileInfoBasicMemCache.pas:165-166); reserving the full ZMP MemCacheCapacity here could
    // bad_alloc on a huge, unvalidated value that legacy never allocates for.
}

void TileInfoMemCache::notifyLocked(const Entry& e) {
    if (onTileInfoUpdate_) {
        onTileInfoUpdate_(TileUpdate{e.xy, e.zoom, e.version});
    }
}

void TileInfoMemCache::makeItCleanLocked(Entry& e) {
    if (!e.empty) {
        notifyLocked(e);
        e.deadline = std::chrono::steady_clock::time_point{};
        e.xy = geo::PointI{};
        e.zoom = 0;
        e.version.reset();
        e.info = TileInfo{};
        e.empty = true;
    }
}

void TileInfoMemCache::add(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version, TileInfo info) {
    core::WriteLock lock(mutex_);
    const QString key = versionKey(version);
    const auto now = clock_();
    const auto deadline = now + ttl_;

    int emptyIdx = -1;
    int oldestIdx = -1;
    auto oldestDeadline = std::chrono::steady_clock::time_point::max();

    for (std::size_t i = 0; i < entries_.size(); ++i) {
        Entry& e = entries_[i];
        if (!e.empty && e.xy == xy && e.zoom == zoom && versionKey(e.version) == key) {
            e.deadline = deadline;
            e.version = version;
            e.info = std::move(info);
            notifyLocked(e);
            return; // found and updated -- Add (u_TileInfoBasicMemCache.pas:254-259)
        }
        if (e.empty) {
            emptyIdx = static_cast<int>(i); // reuse the LAST empty slot, as legacy does (:261-262)
        } else if (e.deadline < oldestDeadline) {
            oldestDeadline = e.deadline;
            oldestIdx = static_cast<int>(i);
        }
    }

    int idx;
    if (emptyIdx >= 0) {
        idx = emptyIdx;
    } else if (static_cast<int>(entries_.size()) < capacity_) {
        entries_.emplace_back();
        idx = static_cast<int>(entries_.size()) - 1;
    } else if (oldestIdx >= 0) {
        idx = oldestIdx;
        makeItCleanLocked(entries_[idx]); // evict: notifies for the OLD tile, then clears it
    } else {
        return; // capacity <= 0 -- nothing to add into; legacy raises, the port no-ops (approved
                // deviation D-22, docs/parity/deviations.md)
    }

    Entry& e = entries_[idx];
    e.deadline = deadline;
    e.xy = xy;
    e.zoom = zoom;
    e.version = version;
    e.info = std::move(info);
    e.empty = false;
    notifyLocked(e); // notify again for the NEW tile (legacy: DoTileInfoUpdateNotify(VTile) at the tail)
}

bool TileInfoMemCache::remove(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version) {
    core::WriteLock lock(mutex_);
    const QString key = versionKey(version);
    for (Entry& e : entries_) {
        if (e.empty) {
            continue;
        }
        if (e.xy == xy && e.zoom == zoom && versionKey(e.version) == key) {
            makeItCleanLocked(e);
            return true;
        }
    }
    return false;
}

std::optional<TileInfo> TileInfoMemCache::get(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version,
                                              LoadMode mode, bool updateTtl) {
    core::WriteLock lock(mutex_);
    const QString key = versionKey(version);
    const auto now = clock_();
    for (Entry& e : entries_) {
        if (e.empty) {
            continue;
        }
        if (e.xy != xy || e.zoom != zoom || versionKey(e.version) != key) {
            continue;
        }
        if (e.deadline > now) {
            if (updateTtl) {
                e.deadline = now + ttl_;
            }
            if (mode == LoadMode::WithData && e.info.data.isEmpty()) {
                return std::nullopt; // "Supports(..., ITileInfoWithData)" fails on a data-less entry
            }
            return e.info;
        }
        if (clearStrategy_ == ClearStrategy::OneByOne) {
            makeItCleanLocked(e);
        }
        return std::nullopt; // expired: always a miss, matching key found -- stop scanning (legacy: Break)
    }
    return std::nullopt;
}

void TileInfoMemCache::clear() {
    core::WriteLock lock(mutex_);
    for (Entry& e : entries_) {
        makeItCleanLocked(e);
    }
    entries_.clear();
}

void TileInfoMemCache::clearExpired() {
    if (clearStrategy_ == ClearStrategy::Never) {
        return;
    }
    core::WriteLock lock(mutex_);
    const auto now = clock_();
    if (clearStrategy_ == ClearStrategy::ByOldest || clearStrategy_ == ClearStrategy::ByYoungest) {
        auto minDeadline = std::chrono::steady_clock::time_point::max();
        auto maxDeadline = std::chrono::steady_clock::time_point::min();
        for (const Entry& e : entries_) {
            if (!e.empty) {
                minDeadline = std::min(minDeadline, e.deadline);
                maxDeadline = std::max(maxDeadline, e.deadline);
            }
        }
        const bool wipeAll = (clearStrategy_ == ClearStrategy::ByOldest && minDeadline < now) ||
                             (clearStrategy_ == ClearStrategy::ByYoungest && maxDeadline < now);
        if (wipeAll) {
            for (Entry& e : entries_) {
                makeItCleanLocked(e);
            }
        }
    } else { // OneByOne
        for (Entry& e : entries_) {
            if (!e.empty && e.deadline < now) {
                makeItCleanLocked(e);
            }
        }
    }
    clearedByTtl_.notify(); // unconditional -- legacy's Notify(nil) sits in ClearByTTL's finally
}

namespace {
struct EnumItem {
    geo::PointI tile;
    quint8 zoom = 0;
    TileInfo info;
};

class MemCacheEnum final : public IEnumTileInfo {
public:
    explicit MemCacheEnum(std::vector<EnumItem> items) : items_(std::move(items)) {}

    bool next(TileInfo& out, geo::PointI& tile, quint8& zoom) override {
        if (next_ >= items_.size()) {
            return false;
        }
        const EnumItem& item = items_[next_++];
        out = item.info;
        tile = item.tile;
        zoom = item.zoom;
        return true;
    }

private:
    std::vector<EnumItem> items_;
    std::size_t next_ = 0;
};
} // namespace

EnumTileInfoPtr TileInfoMemCache::enumerate(bool ignoreTne, bool ignoreMultiVersion) const {
    (void)ignoreMultiVersion; // legacy's AIgnoreMultiVersionTiles is likewise never read -- see header
    core::ReadLock lock(mutex_);
    std::vector<EnumItem> items;
    items.reserve(entries_.size());
    for (const Entry& e : entries_) {
        if (e.empty) {
            continue;
        }
        if (!(e.info.isExists() || e.info.isExistsTne())) {
            continue; // a plain NotExists (or Unknown) entry never enumerates, TNE or not
        }
        if (e.info.isExists() && e.info.data.isEmpty()) {
            continue; // legacy: FInfoType becomes titExists only when Supports(ITileInfoWithData)
                      // holds; a data-less Exists entry stays titUnknown and is dropped (:585-598),
                      // same test get() already uses for the WithData miss (cpp: data.isEmpty())
        }
        if (ignoreTne && e.info.isExistsTne()) {
            continue;
        }
        TileInfo emitted = e.info;
        if (emitted.type != TileInfoType::Exists) {
            emitted.data.clear(); // legacy: VItem.FData is nil for anything but titExists
        }
        emitted.version = e.version; // legacy emits the cache key's version, not info.version (:581)
        items.push_back(EnumItem{e.xy, e.zoom, std::move(emitted)});
    }
    return std::make_unique<MemCacheEnum>(std::move(items));
}

void TileInfoMemCache::setOnTileInfoUpdate(std::function<void(const TileUpdate&)> cb) {
    core::WriteLock lock(mutex_);
    onTileInfoUpdate_ = std::move(cb);
}

} // namespace cg::storage
