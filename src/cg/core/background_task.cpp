// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/background_task.h"

#include "cg/core/log.h"
#include "cg/core/platform/thread_os.h"

#include <exception>
#include <thread>
#include <utility>

namespace cg::core {

BackgroundTask::BackgroundTask(std::stop_token appClosing, BackgroundTaskExecuteEvent onExecute,
                               const std::shared_ptr<IThreadConfig>& /*threadConfig*/,
                               QString threadDebugName)
    : onExecute_(std::move(onExecute)), debugName_(std::move(threadDebugName)) {
    // Runs at once when the token is already stopped (legacy: IsExecuted -> OnAppClosing).
    appClosingCallback_.emplace(std::move(appClosing), std::function<void()>([this] { terminate(); }));
}

BackgroundTask::~BackgroundTask() {
    appClosingCallback_.reset();
    terminate();
    if (thread_.joinable()) {
        if (thread_.get_id() == std::this_thread::get_id()) {
            qFatal("BackgroundTask '%s' destroyed on its own worker thread (from inside its procedure or a "
                   "listener it fired): the last owner reference must be dropped elsewhere",
                   qUtf8Printable(debugName_));
        }
        thread_.join();
    }
}

// TInterfacedThread.Start: only once, never after terminate().
void BackgroundTask::start() {
    const std::lock_guard lock(mutex_);
    if (!started_ && !terminated_.stop_requested()) {
        started_ = true;
        thread_ = std::jthread([this] { execute(); });
    }
}

// FEAT-GEO-033
void BackgroundTask::startExecute() {
    {
        const std::lock_guard lock(mutex_);
        needStart_ = true;
        allowExecute_ = true;
    }
    wakeUp_.notify_all();
}

void BackgroundTask::stopExecute() {
    cancel_.cancel();
    const std::lock_guard lock(mutex_);
    allowExecute_ = false;
}

void BackgroundTask::terminate() {
    stopExecute();
    terminated_.request_stop(); // wakes the worker out of its wait
}

// TBackgroundTask.Execute
void BackgroundTask::execute() {
    platform::setCurrentThreadName(debugName_);
    const std::stop_token terminated = terminated_.get_token();
    std::unique_lock lock(mutex_);
    while (!terminated.stop_requested()) {
        if (!wakeUp_.wait(lock, terminated, [this] { return allowExecute_; }) ||
            terminated.stop_requested()) {
            break;
        }
        // Invariant under mutex_: allowExecute_ implies needStart_ (legacy's two objects, set one
        // after the other without a lock, can disagree: L-144).
        bool needStart = std::exchange(needStart_, false);
        while (needStart) {
            allowExecute_ = false;
            const std::stop_token operation = cancel_.begin();
            lock.unlock();
            try {
                onExecute_(operation);
            } catch (const std::exception& e) {
                qCWarning(logCore, "background task '%s': %s", qUtf8Printable(debugName_), e.what());
            } catch (...) {
                qCWarning(logCore, "background task '%s': unknown exception", qUtf8Printable(debugName_));
            }
            lock.lock();
            if (terminated.stop_requested()) {
                break;
            }
            needStart = std::exchange(needStart_, false);
        }
    }
    lock.unlock();
}

} // namespace cg::core
