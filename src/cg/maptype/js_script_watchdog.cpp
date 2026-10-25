// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/js_script_watchdog.h"

#include <QJSEngine>

#include <algorithm>

namespace cg::maptype {

void JsScriptWatchdog::Entry::arm(QJSEngine* engine, std::chrono::milliseconds limit) {
    const std::scoped_lock lock(mutex_);
    engine_ = engine;
    deadline_ = Clock::now() + limit;
    pauseDepth_ = 0;
    fired_ = false;
}

bool JsScriptWatchdog::Entry::disarm() {
    const std::scoped_lock lock(mutex_);
    engine_ = nullptr;
    return fired_;
}

void JsScriptWatchdog::Entry::pause() {
    const std::scoped_lock lock(mutex_);
    if (pauseDepth_++ == 0) {
        pausedAt_ = Clock::now();
    }
}

void JsScriptWatchdog::Entry::resume() {
    const std::scoped_lock lock(mutex_);
    if (pauseDepth_ > 0) {
        --pauseDepth_;
        if (pauseDepth_ == 0) {
            deadline_ += Clock::now() - pausedAt_; // the paused time is not script time
        }
    }
}

JsScriptWatchdog::JsScriptWatchdog(std::chrono::milliseconds tick)
    : tick_(tick), thread_([this](const std::stop_token& stop) { run(stop); }) {}

JsScriptWatchdog::~JsScriptWatchdog() {
    thread_.request_stop();
    wake_.notify_all();
}

std::shared_ptr<JsScriptWatchdog::Entry> JsScriptWatchdog::add() {
    auto entry = std::make_shared<Entry>();
    const std::scoped_lock lock(mutex_);
    std::erase_if(entries_, [](const std::weak_ptr<Entry>& e) { return e.expired(); });
    entries_.push_back(entry);
    return entry;
}

void JsScriptWatchdog::run(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        std::vector<std::shared_ptr<Entry>> live;
        {
            std::unique_lock lock(mutex_);
            // A timed wait that only a stop request ends early: the tick of the loop, not a busy wait.
            (void)wake_.wait_for(lock, stop, tick_, [] { return false; });
            for (const auto& weak : entries_) {
                if (auto entry = weak.lock()) {
                    live.push_back(std::move(entry));
                }
            }
        }
        for (const auto& entry : live) {
            const std::scoped_lock lock(entry->mutex_);
            if (entry->engine_ != nullptr && entry->pauseDepth_ == 0 && !entry->fired_ &&
                Clock::now() >= entry->deadline_) {
                entry->fired_ = true;
                entry->engine_->setInterrupted(true);
            }
        }
    }
}

} // namespace cg::maptype
