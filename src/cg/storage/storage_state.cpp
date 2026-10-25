// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/storage_state.h"

#include <utility>

namespace cg::storage {

StorageStateHolder::StorageStateHolder(TileStorageAbilities abilities)
    : abilities_(abilities), current_{abilities.allowRead, abilities.allowScan, abilities.allowAdd,
                                      abilities.allowDelete, abilities.allowReplace} {
    snapshot_.set(std::make_shared<const StorageState>(current_));
}

StorageStatePtr StorageStateHolder::get() const {
    return snapshot_.get();
}

void StorageStateHolder::applyLocked(const std::function<void(StorageState&)>& mutate) {
    StorageState next;
    {
        const core::WriteLock lock(mutex_);
        next = current_;
        mutate(next);
        if (next == current_) {
            return; // no real transition -- ports the `if FXxxAccess <> AValue` guard
        }
        current_ = next;
    }
    // Publish + notify outside the lock (spec §3.1 rule 3; also avoids self-deadlock if a
    // subscriber reads state() from inside its own changed() slot).
    snapshot_.set(std::make_shared<const StorageState>(next));
    fireChanged();
}

void StorageStateHolder::setReadAccess(bool value) {
    applyLocked([value](StorageState& s) { s.readAccess = value; });
}

void StorageStateHolder::setScanAccess(bool value) {
    const bool allowed = value && abilities_.allowScan;
    applyLocked([allowed](StorageState& s) { s.scanAccess = allowed; });
}

void StorageStateHolder::setAddAccess(bool value) {
    const bool allowed = value && abilities_.allowAdd;
    applyLocked([allowed](StorageState& s) { s.addAccess = allowed; });
}

void StorageStateHolder::setDeleteAccess(bool value) {
    const bool allowed = value && abilities_.allowDelete;
    applyLocked([allowed](StorageState& s) { s.deleteAccess = allowed; });
}

void StorageStateHolder::setReplaceAccess(bool value) {
    const bool allowed = value && abilities_.allowReplace;
    applyLocked([allowed](StorageState& s) { s.replaceAccess = allowed; });
}

} // namespace cg::storage
