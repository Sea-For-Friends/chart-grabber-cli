// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/read_write_state_internal.h"

namespace cg::core {

ReadWriteStateInternal::ReadWriteStateInternal(bool readAccess, bool writeAccess)
    : readAccess_(readAccess), writeAccess_(writeAccess), static_(createStatic()) {}

ReadWriteStateStaticPtr ReadWriteStateInternal::createStatic() const {
    return std::make_shared<const ReadWriteStateStatic>(ReadWriteStateStatic{readAccess_, writeAccess_});
}

bool ReadWriteStateInternal::readAccess() const {
    const ReadLock lock(mutex_);
    return readAccess_;
}

bool ReadWriteStateInternal::writeAccess() const {
    const ReadLock lock(mutex_);
    return writeAccess_;
}

ReadWriteStateStaticPtr ReadWriteStateInternal::getStatic() const {
    const ReadLock lock(mutex_);
    return static_;
}

// FEAT-GEO-033
void ReadWriteStateInternal::setReadAccess(bool value) {
    bool needNotify = false;
    {
        const WriteLock lock(mutex_);
        if (readAccess_ != value) {
            readAccess_ = value;
            if (!readAccess_) {
                writeAccess_ = false;
            }
            needNotify = true;
            static_ = createStatic();
        }
    }
    if (needNotify) {
        fireChanged();
    }
}

void ReadWriteStateInternal::setWriteAccess(bool value) {
    bool needNotify = false;
    {
        const WriteLock lock(mutex_);
        if (writeAccess_ != value) {
            writeAccess_ = value;
            if (writeAccess_ && !readAccess_) {
                readAccess_ = true;
            }
            needNotify = true;
            static_ = createStatic();
        }
    }
    if (needNotify) {
        fireChanged();
    }
}

} // namespace cg::core
