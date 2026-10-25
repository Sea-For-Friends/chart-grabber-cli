// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/read_write_state_internal_by_other.h"

namespace cg::core {

ReadWriteStateInternalByOther::ReadWriteStateInternalByOther()
    : default_(std::make_shared<const ReadWriteStateStatic>()) {}

ReadWriteStateInternalByOther::~ReadWriteStateInternalByOther() {
    otherSubscription_.reset();
}

ReadWriteStateStaticPtr ReadWriteStateInternalByOther::getStatic() const {
    const ReadLock lock(mutex_);
    return other_ ? other_->getStatic() : default_;
}

// FEAT-GEO-033
void ReadWriteStateInternalByOther::setOther(const ReadWriteStateChangeablePtr& state) {
    bool needNotify = false;
    {
        const WriteLock lock(mutex_);
        if (other_ != state) {
            otherSubscription_.reset();
            other_ = state;
            if (other_) {
                otherSubscription_ = other_->changed().subscribe([this] { fireChanged(); });
            }
            needNotify = true;
        }
    }
    if (needNotify) {
        fireChanged();
    }
}

} // namespace cg::core
