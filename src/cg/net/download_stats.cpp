// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: i_GlobalInternetState.pas, u_GlobalInternetState.pas, i_DownloadInfoSimple.pas,
// u_DownloadInfoSimple.pas, i_LastResponseInfo.pas, u_LastResponseInfo.pas. docs/specs/net.md §2.19.
#include "cg/net/download_stats.h"

#include <QMutexLocker>

#include <utility>

namespace cg::net {

void GlobalInternetState::incQueue() {
    queueCount_.fetch_add(1, std::memory_order_relaxed);
}

void GlobalInternetState::decQueue() {
    int current = queueCount_.load(std::memory_order_relaxed);
    while (current > 0) {
        if (queueCount_.compare_exchange_weak(current, current - 1, std::memory_order_relaxed)) {
            return;
        }
    }
}

int GlobalInternetState::queueCount() const {
    return queueCount_.load(std::memory_order_relaxed);
}

DownloadInfo::DownloadInfo(std::shared_ptr<DownloadInfo> parent, quint64 tileCount, quint64 size)
    : parent_(std::move(parent)), tileCount_(tileCount), size_(size) {}

void DownloadInfo::add(quint64 count, quint64 size) {
    tileCount_.fetch_add(count, std::memory_order_relaxed);
    size_.fetch_add(size, std::memory_order_relaxed);
    if (parent_) {
        parent_->add(count, size);
    }
}

void DownloadInfo::reset() {
    tileCount_.store(0, std::memory_order_relaxed);
    size_.store(0, std::memory_order_relaxed);
}

quint64 DownloadInfo::tileCount() const {
    return tileCount_.load(std::memory_order_relaxed);
}

quint64 DownloadInfo::size() const {
    return size_.load(std::memory_order_relaxed);
}

QByteArray LastResponseInfo::responseHead() const {
    const QMutexLocker locker(&mutex_);
    return responseHead_;
}

void LastResponseInfo::setResponseHead(QByteArray value) {
    bool didChange = false;
    {
        const QMutexLocker locker(&mutex_);
        if (responseHead_ != value) {
            responseHead_ = std::move(value);
            didChange = true;
        }
    }
    if (didChange) {
        fireChanged(); // outside the lock, matches core.md 3's "firing happens after unlock" rule
    }
}

} // namespace cg::net
