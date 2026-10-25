// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_StorageStateProxy.pas, u_StorageStateProxy.pas
//
// docs/specs/storage.md §2.22.2. The state a TileStorageOfMapType publishes: it follows the state
// of the current target and answers "no access at all" while there is none.

#include "cg/core/changeable.h"
#include "cg/core/sync.h"
#include "cg/storage/tile_storage.h"

#include <memory>

namespace cg::storage {

// Ports IStorageStateProxy + TStorageStateProxy.
//
// Legacy's target is one IStorageStateChangeble object; the port has the two halves on the storage
// itself (ITileStorage::stateChangeable() / state()), so setTarget takes the storage and the proxy
// keeps it weakly: the old backend must be free to die before the next one is built.
class StorageStateProxy final : public core::Changeable {
public:
    StorageStateProxy();
    ~StorageStateProxy() override;

    // GetTarget: null also when the target has been destroyed.
    [[nodiscard]] ITileStoragePtr target() const;
    // SetTarget. The notification fires after the proxy's own lock is released.
    void setTarget(const ITileStoragePtr& value);
    // GetStatic: the cached snapshot, never asks the target.
    [[nodiscard]] StorageStatePtr getStatic() const;

private:
    void onTargetChange();

    mutable core::Mutex mutex_;
    std::weak_ptr<ITileStorage> target_; // FTarget (weak, see class comment)
    bool hasTarget_ = false;             // FTarget <> nil
    core::Subscription targetChanged_;   // FTargetChangeListener, while subscribed
    StorageStatePtr disableStatic_;      // FDisableStatic: all five flags false
    StorageStatePtr lastStatic_;         // FLastStatic, starts as disableStatic_
};

} // namespace cg::storage
