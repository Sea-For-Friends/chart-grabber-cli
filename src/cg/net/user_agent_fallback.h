// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing -- no legacy counterpart; a deviation from legacy.
//
// Per-process memory of the hosts that refused the default User-Agent with HTTP 403 and accepted the
// curl one. One object is shared by every CurlDownloader built by one CurlDownloaderFactory.

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QString>

namespace cg::net {

class UserAgentFallback {
public:
    static constexpr int kMaxFailedTries = 3;

    explicit UserAgentFallback(
        QByteArray agent); // the factory passes curlUserAgent(); tests pass a fixed string

    [[nodiscard]] const QByteArray& agent() const noexcept { return agent_; } // immutable, no lock
    [[nodiscard]] bool isSwitched(const QString& host) const; // sticky: true once a retry was accepted
    [[nodiscard]] bool
    shouldRetry(const QString& host) const; // !isSwitched && failedTries(host) < kMaxFailedTries

    // Records one retry. accepted: the host is switched; returns true iff THIS call switched it (first
    // acceptance). !accepted: failedTries(host) += 1 (unless already switched); returns false.
    bool noteRetry(const QString& host, bool accepted);

private:
    const QByteArray agent_;
    mutable QMutex mutex_;
    QSet<QString> switched_;
    QHash<QString, int> failedTries_;
};

} // namespace cg::net
