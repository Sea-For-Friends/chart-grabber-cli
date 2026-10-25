// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_NotifierOperation.pas, u_NotifierOperation.pas
//
// Cooperative cancellation for long operations (downloads, region processing, tile scans).
// The legacy "current operation id" integer that every worker had to remember and compare is
// replaced by std::stop_token: a worker holds a value that cannot go stale unnoticed.

#include "cg/core/notifier.h"
#include "cg/core/sync.h"

#include <stop_token>

namespace cg::core {

class OperationNotifier {
public:
    OperationNotifier() = default;
    ~OperationNotifier();
    OperationNotifier(const OperationNotifier&) = delete;
    OperationNotifier& operator=(const OperationNotifier&) = delete;

    // Cancels the previous generation and starts a fresh one; ports NextOperation.
    [[nodiscard]] std::stop_token begin();

    // Cancels the current generation without starting a new one.
    void cancel();

    [[nodiscard]] bool isCancelled() const;

    // Fires after every begin() and cancel(), on the caller's thread, outside the mutex.
    [[nodiscard]] Notifier& generationChanged() noexcept { return generationChanged_; }

private:
    mutable Mutex mutex_;
    std::stop_source current_;
    Notifier generationChanged_;
};

} // namespace cg::core
