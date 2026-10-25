// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Network/u_DownloaderHttpWithTTL.pas. docs/specs/net.md §2.10. FEAT-NET-009.
//
// Wraps a factory. Builds the real IDownloader on first use, drops it after kHttpClientTtl of
// idleness (dropping it destroys the curl handle, and with it the connection pool AND the cookie
// jar -- that is observable behavior), rebuilds on demand. Legacy drives this from a global
// 1-second INotifierTime tick with a TListenerTTLCheck comparing a stamped last-use time; the
// port subscribes to the same application tick (cg::core::Notifier driven by the
// GarbageCollectorThread, T-0048; u_DownloaderHttpWithTTL.pas:87-88) but compares elapsed time
// directly against kHttpClientTtl rather than porting the "used since last tick" flag dance --
// equivalent effect (to within one tick), simpler state.
//
// Threads: doRequest() runs on the owner's (worker) thread, onTick() on the GC thread. All state
// they share (downloader_, lastUse_, everUsed_) is guarded by mutex_.

#include "cg/core/notifier.h" // Notifier, Subscription
#include "cg/net/downloader.h"

#include <QMutex>

#include <chrono>
#include <functional>
#include <memory>

namespace cg::net {

inline constexpr std::chrono::milliseconds kHttpClientTtl{300'000}; // u_DownloaderHttpWithTTL.pas:79, 5 min

// Time source for the idle checks; the default is steady_clock::now. Tests inject their own.
using SteadyClockFn = std::function<std::chrono::steady_clock::time_point()>;

// Must be owned by a std::shared_ptr: the tick slot holds only a weak reference (see
// subscribeGcTick), so a tick racing the destructor never touches a dead object.
class TtlDownloader final : public IDownloader, public std::enable_shared_from_this<TtlDownloader> {
public:
    explicit TtlDownloader(std::shared_ptr<IDownloaderFactory> factory, DownloaderOptions options,
                           SteadyClockFn clock = {});

    // Ports FGCNotifier.Add(FTTLListener) (u_DownloaderHttpWithTTL.pas:88). Call once, after the
    // object is owned by a shared_ptr. `gc` is the application notifier; the
    // subscription ends with this object.
    void subscribeGcTick(cg::core::Notifier& gc);

    [[nodiscard]] DownloadResult doRequest(const DownloadRequest& request, std::stop_token stop) override;

private:
    void onTick(); // ports OnTTLTrim (u_DownloaderHttpWithTTL.pas:126-134)

    std::shared_ptr<IDownloaderFactory> factory_;
    DownloaderOptions options_;

    mutable QMutex mutex_;
    IDownloaderPtr downloader_;
    std::chrono::steady_clock::time_point lastUse_;
    bool everUsed_ = false;

    SteadyClockFn clock_;

    // Declared LAST deliberately -- do NOT reorder (review: "destructor-order siblings"). Members
    // are destroyed in reverse declaration order, so tickSub_ is torn down FIRST. A tick that
    // already copied the slot holds only a weak_ptr and locks it before touching anything, so it
    // can never reach a half-destroyed TtlDownloader (T-0046 race class).
    cg::core::Subscription tickSub_;
};

} // namespace cg::net
