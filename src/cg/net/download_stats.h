// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_GlobalInternetState.pas, u_GlobalInternetState.pas, i_DownloadInfoSimple.pas,
// u_DownloadInfoSimple.pas, i_LastResponseInfo.pas, u_LastResponseInfo.pas. docs/specs/net.md §2.19.
//
// Three tiny, unrelated things that the download path writes and the UI reads. No FEAT ID names any
// of them individually (net.md §6); they back FEAT-NET-013/016's plumbing.

#include "cg/core/changeable.h"

#include <QByteArray>
#include <QMutex>
#include <QtGlobal>

#include <atomic>
#include <memory>

namespace cg::net {

// Number of requests currently queued anywhere; drives the "globe" status-bar icon.
class GlobalInternetState {
public:
    void incQueue();
    void decQueue(); // clamps at 0, never goes negative (u_GlobalInternetState.pas:52-58)
    [[nodiscard]] int queueCount() const;

private:
    std::atomic<int> queueCount_{0};
};

// Per-session counters; a child instance forwards to its parent (legacy TDownloadInfoSimple takes
// an optional parent and adds to both).
class DownloadInfo {
public:
    explicit DownloadInfo(std::shared_ptr<DownloadInfo> parent = {}, quint64 tileCount = 0, quint64 size = 0);

    void add(quint64 count, quint64 size);
    void reset(); // self only -- does NOT forward to the parent (u_DownloadInfoSimple.pas:108-116)
    [[nodiscard]] quint64 tileCount() const;
    [[nodiscard]] quint64 size() const;

private:
    std::shared_ptr<DownloadInfo> parent_;
    std::atomic<quint64> tileCount_;
    std::atomic<quint64> size_;
};

// The per-map "headers of the previous response", fed to the next script run as ResponseHead
// (ADR-0003, FEAT-NET-003). Updated by RetryDownloader from any result with
// hasServerRespond() == true (u_TileDownloaderSimple.pas:228-230).
class LastResponseInfo final : public cg::core::Changeable {
public:
    LastResponseInfo() = default;

    [[nodiscard]] QByteArray responseHead() const;
    void setResponseHead(QByteArray value); // no-op, no notify, when unchanged

private:
    mutable QMutex mutex_;
    QByteArray responseHead_;
};

} // namespace cg::net
