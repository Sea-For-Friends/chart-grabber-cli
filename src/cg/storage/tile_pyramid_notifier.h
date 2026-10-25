// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_NotifierTilePyramidUpdate.pas, u_NotifierTilePyramidUpdate.pas
//
// docs/specs/storage.md §2.8 (:531-549). Legacy's notifier lets a listener subscribe to one zoom,
// one tile-rect-per-zoom, or everything, and only calls listeners whose registered rect contains
// the changed tile (u_NotifierTilePyramidUpdate.pas TNotifierTileRectUpdateOneZoomSimple) -- a
// filtering optimization for render-thread invalidation. The spec collapses this to "every
// subscriber sees every update": the render-side by-rect index is a later ticket's job (not in
// T-0021D's scope) and can filter on TileUpdate.tile/zoom itself; nothing here re-derives it.
//
// Deviation (already flagged, not re-derived here): the tile coordinate type is geo::PointI, not
// geo::TilePoint -- same as tile_info.h / tile_name_codec.h (T-0021A).

#include "cg/core/notifier.h"
#include "cg/geo/geo_types.h"
#include "cg/storage/map_version.h"

#include <QtGlobal>

#include <functional>
#include <memory>

namespace cg::storage {

// FEAT-STO-035 (the notification half; ITileStorage::tileRectInfo/scanTiles cover the rest).
// Ports the payload of the legacy IListener.Notification(ATileKey) / (nil) calls.
struct TileUpdate {
    geo::PointI tile;
    quint8 zoom = 0;
    MapVersionInfoPtr version; // null when the backend ignores versions
};

// Ports INotifierTilePyramidUpdate + INotifierTilePyramidUpdateInternal, collapsed to one
// concrete class: nothing outside `storage` implements this interface, so there is no separate
// internal/external split to preserve.
class TilePyramidNotifier final {
public:
    TilePyramidNotifier();
    ~TilePyramidNotifier();
    TilePyramidNotifier(const TilePyramidNotifier&) = delete;
    TilePyramidNotifier& operator=(const TilePyramidNotifier&) = delete;

    // The slot's pointer is legacy's IListener.Notification(ATileKey): non-null for one changed tile
    // (valid only during the call), null for a full update, Notification(nil), which means "every
    // tile of every zoom may have changed". A slot must test it before use.
    [[nodiscard]] core::Subscription subscribe(std::function<void(const TileUpdate*)> slot);
    void
    notify(const TileUpdate& update) const; // TileUpdateNotify(ATileKey); copy-then-call as core::Notifier
    void notifyFull() const;                // TileFullUpdateNotify: every subscriber gets nullptr

private:
    // core::Notifier owns the subscription bookkeeping (id allocation, copy-then-call dispatch,
    // Subscription lifetime); see the .cpp for how the TileUpdate payload is threaded through its
    // plain void() slots without an extra lock around the dispatch.
    core::Notifier notifier_;
};

// Shared ownership form: `TileStorageOfMapType`-equivalent facades own ONE instance across
// backend swaps (u_TileStorageOfMapType.pas:214,281-285) and hand this same alias down to every
// `TileStorageBase` they build (:327), so subscribers survive a cache-type/path change instead of
// being dropped with the old backend.
using TilePyramidNotifierPtr = std::shared_ptr<TilePyramidNotifier>;

} // namespace cg::storage
