// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/System/u_GarbageCollectorThread.pas (TGarbageCollectorThread).
//
// The application's GC thread: started by the constructor (legacy `inherited Create(false)`), it
// loops "notify the tick, then sleep for the interval, cancelable" until it is stopped (legacy
// u_GlobalState.pas:735-741 creates it with a 1000 ms interval, `[SleepByClass]` overrides it, and
// u_GlobalState.pas:978-981 shuts it down with Terminate + WaitFor). Every GC-tick consumer (storage:
// BerkeleyDB sync, SQLite region idle close; later net and the mem cache) therefore runs on THIS
// thread, never on the GUI thread. The notifier belongs to the application and must outlive the thread and
// every storage type that subscribes to it (the types keep a reference to it).
//
// Not ported: the performance counter (no debug-info subsystem yet) and the app-closing listener
// (the owner destroys the thread at shutdown, which is what the listener's Terminate does).

#include "cg/core/notifier.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace cg::core {

class GarbageCollectorThread final {
public:
    // FEAT-STO-026.
    GarbageCollectorThread(Notifier& notifier, std::chrono::milliseconds sleepTime);
    ~GarbageCollectorThread(); // Terminate + WaitFor: a tick in progress runs to its end

    GarbageCollectorThread(const GarbageCollectorThread&) = delete;
    GarbageCollectorThread& operator=(const GarbageCollectorThread&) = delete;

private:
    void execute(const std::stop_token& stop);

    Notifier& notifier_;
    std::chrono::milliseconds sleepTime_;
    std::mutex sleepMutex_;
    std::condition_variable_any sleepCancel_; // legacy FCancelEvent
    std::jthread thread_;                     // last: started after the members above exist
};

} // namespace cg::core
