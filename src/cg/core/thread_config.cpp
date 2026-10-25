// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/thread_config.h"

#include "cg/core/config_provider.h"
#include "cg/core/config_writer.h"

namespace cg::core {

namespace {

[[nodiscard]] bool isValid(int value) {
    return value >= static_cast<int>(ThreadPriority::Idle) &&
           value <= static_cast<int>(ThreadPriority::Higher);
}

} // namespace

ThreadConfig::ThreadConfig(ThreadPriority defPriority) : priorityDef_(defPriority) {
    if (!isValid(static_cast<int>(priorityDef_))) {
        priorityDef_ = ThreadPriority::Lower;
    }
    priority_ = priorityDef_;
}

ThreadPriority ThreadConfig::priority() const {
    const ReadGuard guard = readLock();
    return priority_;
}

void ThreadConfig::setPriority(ThreadPriority value) {
    const WriteGuard guard = writeLock();
    setPriorityLocked(static_cast<int>(value));
}

// FEAT-GEO-034
void ThreadConfig::doReadConfig(const IConfigProviderPtr& src) {
    if (src) {
        setPriorityLocked(src->readInt(u"ThreadPriority", static_cast<qint32>(priority_)));
    }
}

void ThreadConfig::doWriteConfig(const IConfigWriterPtr& dst) const {
    dst->writeInt(u"ThreadPriority", static_cast<qint32>(priority_));
}

void ThreadConfig::setPriorityLocked(int value) {
    if (!isValid(value)) {
        value = static_cast<int>(priorityDef_);
    }
    if (static_cast<int>(priority_) != value) {
        priority_ = static_cast<ThreadPriority>(value);
        setChanged();
    }
}

} // namespace cg::core
