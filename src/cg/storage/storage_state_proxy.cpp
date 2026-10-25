// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/storage_state_proxy.h"

namespace cg::storage {

StorageStateProxy::StorageStateProxy()
    : disableStatic_(std::make_shared<const StorageState>()), lastStatic_(disableStatic_) {}

StorageStateProxy::~StorageStateProxy() {
    const core::WriteLock lock(mutex_);
    targetChanged_.reset();
    target_.reset();
    hasTarget_ = false;
}

StorageStatePtr StorageStateProxy::getStatic() const {
    const core::ReadLock lock(mutex_);
    return lastStatic_;
}

ITileStoragePtr StorageStateProxy::target() const {
    const core::ReadLock lock(mutex_);
    return target_.lock();
}

void StorageStateProxy::onTargetChange() {
    bool needNotify = false;
    {
        const core::WriteLock lock(mutex_);
        const auto t = target_.lock();
        StorageStatePtr state = t ? t->state() : nullptr;
        if (!state) {
            state = disableStatic_;
        }
        if (!(*lastStatic_ == *state)) {
            lastStatic_ = state;
            needNotify = true;
        }
    }
    if (needNotify) {
        fireChanged();
    }
}

void StorageStateProxy::setTarget(const ITileStoragePtr& value) {
    bool needNotify = false;
    {
        const core::WriteLock lock(mutex_);
        const bool same = hasTarget_ ? (value != nullptr && target_.lock() == value) : (value == nullptr);
        if (same) {
            return;
        }
        StorageStatePtr state;
        targetChanged_.reset();
        target_ = value;
        hasTarget_ = (value != nullptr);
        if (value) {
            targetChanged_ = value->stateChangeable().changed().subscribe([this] { onTargetChange(); });
            state = value->state();
        }
        if (!state) {
            state = disableStatic_;
        }
        if (!(*lastStatic_ == *state)) {
            lastStatic_ = state;
            needNotify = true;
        }
    }
    if (needNotify) {
        fireChanged();
    }
}

} // namespace cg::storage
