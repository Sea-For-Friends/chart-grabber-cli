// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// No legacy counterpart; a deviation from legacy.
#include "cg/net/user_agent_fallback.h"

#include <QMutexLocker>

#include <utility>

namespace cg::net {

UserAgentFallback::UserAgentFallback(QByteArray agent) : agent_(std::move(agent)) {}

bool UserAgentFallback::isSwitched(const QString& host) const {
    const QMutexLocker locker(&mutex_);
    return switched_.contains(host);
}

bool UserAgentFallback::shouldRetry(const QString& host) const {
    const QMutexLocker locker(&mutex_);
    return !switched_.contains(host) && failedTries_.value(host, 0) < kMaxFailedTries;
}

bool UserAgentFallback::noteRetry(const QString& host, bool accepted) {
    const QMutexLocker locker(&mutex_);
    if (accepted) {
        if (switched_.contains(host)) {
            return false;
        }
        switched_.insert(host);
        return true;
    }
    if (!switched_.contains(host)) {
        ++failedTries_[host];
    }
    return false;
}

} // namespace cg::net
