// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Network/u_TileDownloaderSimple.pas:117-318. docs/specs/net.md §2.15. FEAT-NET-010.
#include "cg/net/retry_downloader.h"

#include "cg/net/download_result_factory.h"

#include <utility>

namespace cg::net {

RetryDownloader::RetryDownloader(IDownloaderPtr send, int tryCount, quint32 sleepOnResetMs,
                                 quint32 waitIntervalMs)
    : send_(std::move(send)), tryCount_(tryCount), sleepOnResetMs_(sleepOnResetMs),
      waitIntervalMs_(waitIntervalMs) {}

RetryOutcome RetryDownloader::run(const BuildFn& build, LastResponseInfo& lastResponse, const SaveFn& save,
                                  const std::stop_token& soft, const std::stop_token& hard) {
    const std::lock_guard runLock(runMutex_);
    // Ports the loop of u_TileDownloaderSimple.pas:152-274 in its exact order.
    int count = 0;
    RetryOutcome outcome;
    for (;;) {
        sleepIfNeeded(soft, hard); // :178
        if (soft.stop_requested()) {
            return {RetryEnd::CanceledBeforeBuild, {}};
        }
        const std::optional<DownloadRequest> request = build(); // :183-190
        if (!request) {
            return {RetryEnd::NoRequest, {}};
        }
        if (soft.stop_requested()) {
            return {RetryEnd::CanceledAfterBuild, {}};
        }
        outcome.end = RetryEnd::Sent;
        outcome.result = send_->doRequest(*request, hard); // :213-218
        ++count;
        lastDownloadTime_ = std::chrono::steady_clock::now(); // stamped AFTER the request, :220
        if (outcome.result.kind == ResultKind::Canceled) {
            return outcome; // :222-225, not saved
        }
        if (outcome.result.isServerExists()) { // :226-231
            wasConnectError_ = false;
            if (outcome.result.hasServerRespond()) {
                lastResponse.setResponseHead(outcome.result.rawResponseHeader);
            }
        } else {
            wasConnectError_ = true;
        }
        save(outcome.result);                          // :234-243
        if (!wasConnectError_ || count >= tryCount_) { // :258
            return outcome;
        }
    }
}

void RetryDownloader::sleepIfNeeded(const std::stop_token& soft, const std::stop_token& hard) {
    const quint32 interval = wasConnectError_ ? sleepOnResetMs_ : waitIntervalMs_;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - lastDownloadTime_;
    if (elapsed >= std::chrono::milliseconds(interval)) {
        return;
    }
    const auto deadline = now + std::chrono::milliseconds(interval) -
                          std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

    // The hard cancel (app closing) also ends the sleep, :276-279. Its callback is registered
    // BEFORE the lock is taken (it may run inline) and takes the lock briefly so the wake-up
    // cannot slip between the predicate check and the wait.
    const std::stop_callback wake(hard, [this] {
        {
            const std::lock_guard sync(sleepMutex_);
        }
        sleepCv_.notify_all();
    });
    std::unique_lock lock(sleepMutex_);
    sleepCv_.wait_until(lock, soft, deadline, [&hard] { return hard.stop_requested(); });
}

} // namespace cg::net
