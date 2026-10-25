// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/garbage_collector_thread.h"

#include "cg/core/log.h"

#include <exception>

namespace cg::core {

GarbageCollectorThread::GarbageCollectorThread(Notifier& notifier, std::chrono::milliseconds sleepTime)
    : notifier_(notifier), sleepTime_(sleepTime),
      thread_([this](const std::stop_token& stop) { execute(stop); }) {}

GarbageCollectorThread::~GarbageCollectorThread() {
    thread_.request_stop(); // jthread's destructor joins; the wait below wakes on the stop token
}

void GarbageCollectorThread::execute(const std::stop_token& stop) {
    try {
        while (!stop.stop_requested()) {
            notifier_.notify();
            if (sleepTime_.count() > 0) { // SleepCancelable
                std::unique_lock lock(sleepMutex_);
                sleepCancel_.wait_for(lock, stop, sleepTime_, [] { return false; });
            }
        }
    } catch (const std::exception& e) {
        // Legacy shows the exception and the thread ends (try/except wraps the whole loop).
        qCWarning(logCore) << "GarbageCollectorThread stopped by an exception:" << e.what();
    } catch (...) { // legacy's `except` catches everything
        qCWarning(logCore) << "GarbageCollectorThread stopped by an unknown exception";
    }
}

} // namespace cg::core
