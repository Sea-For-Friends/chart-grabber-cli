// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/operation_notifier.h"

#include <utility>

namespace cg::core {

OperationNotifier::~OperationNotifier() {
    // No notification: subscribers may already be gone while the owner is being destroyed.
    const WriteLock lock(mutex_);
    current_.request_stop();
}

std::stop_token OperationNotifier::begin() {
    std::stop_token token;
    {
        const WriteLock lock(mutex_);
        current_.request_stop();
        current_ = std::stop_source{};
        token = current_.get_token();
    }
    generationChanged_.notify();
    return token;
}

void OperationNotifier::cancel() {
    {
        const WriteLock lock(mutex_);
        current_.request_stop();
    }
    generationChanged_.notify();
}

bool OperationNotifier::isCancelled() const {
    const ReadLock lock(mutex_);
    return current_.stop_requested();
}

} // namespace cg::core
