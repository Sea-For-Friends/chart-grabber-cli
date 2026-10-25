// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing (NEW code, ADR-0014 point 5). The script-time limit of the JavaScript builders: one thread
// per factory looks every 25 ms at the engines of its builders and interrupts one whose deadline has passed.
// Time inside http and the wait for shared.locked is paused, not counted.

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class QJSEngine;

namespace cg::maptype {

class JsScriptWatchdog {
public:
    using Clock = std::chrono::steady_clock;

    // The clock of one builder. arm / disarm / pause / resume are called by the builder's own thread; the
    // watchdog thread reads the same state under the mutex.
    class Entry {
    public:
        void arm(QJSEngine* engine, std::chrono::milliseconds limit);
        // Stops watching the engine (the watchdog never touches it afterwards) and says whether the limit
        // fired.
        [[nodiscard]] bool disarm();
        void pause();
        void resume();

    private:
        friend class JsScriptWatchdog;
        std::mutex mutex_;
        QJSEngine* engine_ = nullptr;
        Clock::time_point deadline_;
        Clock::time_point pausedAt_;
        int pauseDepth_ = 0;
        bool fired_ = false;
    };

    explicit JsScriptWatchdog(std::chrono::milliseconds tick = std::chrono::milliseconds(25));
    ~JsScriptWatchdog();
    JsScriptWatchdog(const JsScriptWatchdog&) = delete;
    JsScriptWatchdog& operator=(const JsScriptWatchdog&) = delete;

    [[nodiscard]] std::shared_ptr<Entry> add();

private:
    void run(const std::stop_token& stop);

    std::chrono::milliseconds tick_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::vector<std::weak_ptr<Entry>> entries_;
    std::jthread thread_; // last: starts after the members above exist, joins first
};

} // namespace cg::maptype
