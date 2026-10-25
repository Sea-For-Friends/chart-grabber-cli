// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_StorageState.pas, i_StorageStateInternal.pas, u_StorageStateInternal.pas,
//        u_StorageStateStatic.pas
//
// docs/specs/storage.md §2.7 (:503-530). IStorageStateStatic + IStorageStateChangeble/
// IStorageStateInternal collapse into one pair: an immutable StorageState published through
// core::Snapshot, and StorageStateHolder, the mutable object that publishes it. Legacy's IsSame
// (u_StorageStateStatic.pas) becomes operator==.

#include "cg/core/changeable.h"
#include "cg/core/snapshot.h"
#include "cg/core/sync.h"
#include "cg/storage/tile_storage_abilities.h"

#include <functional>
#include <memory>

namespace cg::storage {

// FEAT-STO-001. Ports IStorageStateStatic.
struct StorageState {
    bool readAccess = false;
    bool scanAccess = false;
    bool addAccess = false;
    bool deleteAccess = false;
    bool replaceAccess = false;

    [[nodiscard]] bool operator==(const StorageState&) const = default; // ports IsSame
};
using StorageStatePtr = std::shared_ptr<const StorageState>;

// Ports IStorageStateChangeble + IStorageStateInternal + TStorageStateInternal, collapsed
// (idioms.md 4): one mutable holder that publishes an immutable StorageState snapshot and fires
// Changeable::changed() (via fireChanged()) on a real transition.
//
// u_StorageStateInternal.pas quirk, reproduced verbatim: SetScanAccess/SetAddAccess/
// SetDeleteAccess/SetReplaceAccess each AND the requested value with the constructor-supplied
// ability before storing it, but SetReadAccess does NOT -- it stores the requested value
// unconditionally, even above the ability ceiling. This is not a typo to "fix": legacy wins
// (protocol rule 5); see docs/parity/deviations.md.
class StorageStateHolder final : public core::Changeable {
public:
    explicit StorageStateHolder(TileStorageAbilities abilities);

    [[nodiscard]] StorageStatePtr get() const;

    void setReadAccess(bool value);    // NOT clamped by abilities_.allowRead -- see class comment
    void setScanAccess(bool value);    // clamped by abilities_.allowScan
    void setAddAccess(bool value);     // clamped by abilities_.allowAdd
    void setDeleteAccess(bool value);  // clamped by abilities_.allowDelete
    void setReplaceAccess(bool value); // clamped by abilities_.allowReplace

private:
    // Applies `mutate` to a copy of the current state under the lock; publishes the new snapshot
    // and fires changed() -- outside the lock -- only when the copy actually differs (mirrors the
    // CS.BeginWrite/EndWrite pair plus the `if <>` guard that precedes every legacy
    // SetXxxAccess's DoChangeNotify).
    void applyLocked(const std::function<void(StorageState&)>& mutate);

    mutable core::Mutex mutex_;
    TileStorageAbilities abilities_;
    StorageState current_; // guarded by mutex_
    core::Snapshot<StorageState> snapshot_;
};

} // namespace cg::storage
