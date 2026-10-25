// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Network/u_DownloaderHttpWithTTL.pas. docs/specs/net.md §2.10. FEAT-NET-009.
#include "cg/net/downloader_ttl.h"

#include <QMutexLocker>

#include <utility>

namespace cg::net {

TtlDownloader::TtlDownloader(std::shared_ptr<IDownloaderFactory> factory, DownloaderOptions options,
                             SteadyClockFn clock)
    : factory_(std::move(factory)), options_(std::move(options)), clock_(std::move(clock)) {
    if (!clock_) {
        clock_ = [] { return std::chrono::steady_clock::now(); };
    }
}

void TtlDownloader::subscribeGcTick(cg::core::Notifier& gc) {
    tickSub_ = gc.subscribe([weak = weak_from_this()] {
        if (const auto self = weak.lock()) {
            self->onTick();
        }
    });
}

DownloadResult TtlDownloader::doRequest(const DownloadRequest& request, std::stop_token stop) {
    // Ports u_DownloaderHttpWithTTL.pas:102-124: FCS.BeginWrite is held for the WHOLE call,
    // including the inner DoRequest -- the write lock is what makes "the trim may destroy the
    // inner downloader while no request is in flight" (docs/specs/net.md §3) true.
    const QMutexLocker locker(&mutex_);
    lastUse_ = clock_();
    everUsed_ = true;
    if (!downloader_) {
        downloader_ = factory_->build(options_);
    }
    const DownloadResult result = downloader_->doRequest(request, stop);
    lastUse_ = clock_();
    return result;
}

void TtlDownloader::onTick() {
    // Finding 9 (T-0010B rework): tryLock, not a blocking QMutexLocker -- doRequest() (above) holds
    // this same mutex across the WHOLE inner request, and the tick runs on the shared GC thread
    // (T-0048), where a blocking lock would stall every other GC consumer (storage sync, ...) for up
    // to the request timeout. Legacy blocks here (BeginWrite, u_DownloaderHttpWithTTL.pas:132), on
    // the same GC thread; the port deliberately does not. A downloader
    // that is mid-request is by definition not idle, so a skipped trim costs nothing: it is
    // retried on the next tick.
    if (!mutex_.tryLock()) {
        return;
    }
    if (downloader_ && everUsed_ && clock_() - lastUse_ >= kHttpClientTtl) {
        downloader_.reset(); // drops the curl handle: connection pool AND cookie jar go with it
        everUsed_ = false;
    }
    mutex_.unlock();
}

} // namespace cg::net
