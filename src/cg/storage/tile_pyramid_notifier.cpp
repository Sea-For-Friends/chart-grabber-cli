// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/tile_pyramid_notifier.h"

#include <memory>
#include <utility>

namespace cg::storage {

namespace {
// core::Notifier's slots are std::function<void()>; TilePyramidNotifier needs to hand each slot
// the TileUpdate that triggered this particular notify() call. Dispatch is synchronous and runs
// on the calling thread (core::Notifier::notify() copies the slot list under its own lock,
// releases it, then calls every slot on the caller's thread) -- so a plain thread_local pointer,
// saved and restored around the call to notifier_.notify(), threads the payload through safely:
// concurrent notify() calls on different threads never see each other's pointer (each thread has
// its own copy), and a slot that re-enters notify() on the SAME thread (e.g. a render-thread
// listener that invalidates and re-reads a tile, triggering a nested save/delete) nests correctly
// because save/restore is a plain stack local, not a shared/global mutable.
// Null means a full update (Notification(nil)); slots only run inside dispatch().
thread_local const TileUpdate* dispatching = nullptr;

void dispatch(const core::Notifier& notifier, const TileUpdate* update) {
    const TileUpdate* previous = dispatching;
    dispatching = update;
    notifier.notify();
    dispatching = previous;
}
} // namespace

TilePyramidNotifier::TilePyramidNotifier() = default;
TilePyramidNotifier::~TilePyramidNotifier() = default;

core::Subscription TilePyramidNotifier::subscribe(std::function<void(const TileUpdate*)> slot) {
    auto boxed = std::make_shared<std::function<void(const TileUpdate*)>>(std::move(slot));
    return notifier_.subscribe([boxed] { (*boxed)(dispatching); });
}

void TilePyramidNotifier::notify(const TileUpdate& update) const {
    dispatch(notifier_, &update);
}

void TilePyramidNotifier::notifyFull() const {
    dispatch(notifier_, nullptr);
}

} // namespace cg::storage
