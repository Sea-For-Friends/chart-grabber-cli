// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Network/u_TileDownloaderSimple.pas:117-318 (the retry loop and the pacing). The loop
// runs over three things the job supplies -- build, send (the slot's wrapped IDownloader) and
// save; the builders, the ITileRequest/ITileRequestResult layers and the saver are maptype's job.
// docs/specs/net.md §2.15. FEAT-NET-010.

#include "cg/net/download_stats.h"
#include "cg/net/downloader.h"

#include <QtGlobal>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>

namespace cg::net {

// Ports TTileDownloaderSimple: retry + the sleep-since-last-download pacing that live OUTSIDE the
// HTTP client (net.md §2.15, §3 -- "owned by one Worker": single-threaded use, so no lock is
// needed around the retry loop itself, only around the interruptible sleep).
//
// std::mutex/std::condition_variable, not QMutex/QWaitCondition (the spec's own suggestion,
// net.md §2.15, offers either): std::condition_variable::wait's stop_token overload (C++20) is the
// exact primitive this needs, and folding the interruptible-sleep predicate through it is safer
// than hand-rolling the wake-on-cancel plumbing a second time (download_queue.cpp needs the same
// shape for its own blocking waits).
// How run() ended; the four legacy exits of the loop that need distinct tile-request results.
enum class RetryEnd : std::uint8_t {
    CanceledBeforeBuild, // :179-181 -- the soft cancel fired before BuildRequest (also :260/:266/:272, the
                         // outer checks)
    NoRequest,           // :201-207 -- build returned nothing ("Tile does not exist"); nothing was sent
    CanceledAfterBuild,  // :208-211 -- the soft cancel fired between build and send
    Sent,                // the loop ended after at least one send; `result` is the last attempt
};
struct RetryOutcome {
    RetryEnd end = RetryEnd::CanceledBeforeBuild;
    DownloadResult result; // meaningful for Sent only (may itself be Canceled: not saved, :222-225)
};

// Called once per attempt (legacy rebuilds the request on every retry, :183-190), on the worker.
// Must not throw (like IDownloader::doRequest): legacy catches around BuildRequest and turns the
// message into a tile-request error (:191-199); the job does that conversion inside this callable.
// It closes over the HARD cancel itself (legacy passes ACancelNotifier to BuildRequest, :188).
using BuildFn = std::function<std::optional<DownloadRequest>()>;
// Receives each non-canceled attempt's result, after the response head is stored (:234-243).
// Must not throw: legacy catches around SaveDownloadResult (:236-243) and the loop goes on to its
// `until` check; the job catches inside this callable and returns normally.
using SaveFn = std::function<void(const DownloadResult&)>;

class RetryDownloader final {
public:
    // tryCount/sleepOnResetMs/waitIntervalMs are a STATIC SNAPSHOT, taken once here and never
    // re-read -- legacy did this deliberately to fix a bug (u_TileDownloaderSimple.pas:117-120,
    // "Static config used because of bug: mantis #3029"). Do not add a live config reference.
    RetryDownloader(IDownloaderPtr send, int tryCount, quint32 sleepOnResetMs, quint32 waitIntervalMs);

    // FEAT-NET-010. Ports TTileDownloaderSimple.Download's loop (:152-274): per attempt, pacing
    // sleep -> cancel check -> build -> (nothing? stop) -> cancel check -> send -> stamp time ->
    // canceled? return unsaved -> store the response head in `lastResponse` (the slot's
    // LastResponseInfo, owned by the job's slot state) -> save; `until not wasConnectError or
    // count >= tryCount`. Two cancels, as in legacy: `soft` (ASoftCancelNotifier) is checked
    // before build and between build and send (:179, :209) and wakes the pacing sleep (:172); a
    // soft cancel lets the transfer in flight finish and be saved. `hard` (ACancelNotifier) aborts
    // the transfer (:216) and also wakes the sleep (app closing, :276-279). Runs are serialized per
    // slot by a mutex (legacy FCS.BeginWrite, :168): after an idle trim an old worker may still
    // be finishing its last job when a new one starts on the same slot.
    [[nodiscard]] RetryOutcome run(const BuildFn& build, LastResponseInfo& lastResponse, const SaveFn& save,
                                   const std::stop_token& soft, const std::stop_token& hard);

private:
    // Ports SleepIfConnectErrorOrWaitInterval (u_TileDownloaderSimple.pas:293-318). Runs before
    // every attempt, including the first. Sleeps only the remainder since lastDownloadTime_,
    // interruptibly: a stop request wakes it immediately instead of waiting out the interval.
    void sleepIfNeeded(const std::stop_token& soft, const std::stop_token& hard);

    IDownloaderPtr send_;
    const int tryCount_;
    const quint32 sleepOnResetMs_;
    const quint32 waitIntervalMs_;

    std::mutex runMutex_; // legacy FCS: one Download at a time per slot
    std::mutex sleepMutex_;
    // condition_variable_any, not condition_variable: it is the one with a timed AND
    // stop_token-aware wait_until overload (condition_variable's own stop_token overload has no
    // timed form), which is exactly "sleep the remainder, interruptibly".
    std::condition_variable_any sleepCv_;
    // Default-constructed (epoch) reads as "effectively forever ago", so the very first call's
    // elapsed-since-last-download is huge and sleepIfNeeded never waits -- same shape as legacy's
    // FLastDownloadTime defaulting to 0 and GetTickCount's wraparound guard (:308-312) never firing
    // in practice on the first call either.
    std::chrono::steady_clock::time_point lastDownloadTime_{};
    bool wasConnectError_ = false;
};

} // namespace cg::net
