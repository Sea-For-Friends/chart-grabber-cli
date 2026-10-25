// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_BackgroundTask.pas, u_BackgroundTask.pas, and the Start / Terminate / priority half of
// u_InterfacedThread.pas + i_Thread.pas (their only user in this port is the background task, so
// IThread's two methods are folded into IBackgroundTask).
//
// One worker thread that sleeps until startExecute() and then runs the procedure, again and again
// while requests keep arriving:
//  * startExecute() while idle runs the procedure once; while it is running it only remembers
//    "run once more" (any number of requests collapse into one) -- the running call is not touched;
//  * stopExecute() cancels the running call through its stop_token (the next call gets a fresh,
//    uncancelled one) and drops a not-yet-consumed wake-up; a remembered request survives;
//  * terminate() = stopExecute() + the thread ends after the current call returns; so does the
//    application-closing token (legacy INotifierOneOperation, idioms section 8).
// An exception leaving the procedure is logged and the thread carries on.
// Destruction contract (D-57, legacy L-145): the destructor terminates and BLOCKS until the running
// procedure has returned, so the procedure may safely use its owner. Never destroy the task from its
// own procedure (or a listener fired by it): that is a fatal error with a named message.
// The thread priority of IThreadConfig is not applied on Linux (D-60); the config argument is kept
// for the legacy signature only.

#include "cg/core/notifier.h"
#include "cg/core/operation_notifier.h"
#include "cg/core/thread_config.h"

#include <QString>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace cg::core {

class IBackgroundTask {
public:
    virtual ~IBackgroundTask() = default;

    virtual void start() = 0;
    virtual void terminate() = 0;
    virtual void startExecute() = 0;
    virtual void stopExecute() = 0;
};

// Ports TBackgroundTaskExecuteEvent; the (OperationID, CancelNotifier) pair is one stop_token.
using BackgroundTaskExecuteEvent = std::function<void(std::stop_token)>;

// FEAT-GEO-033, FEAT-GEO-034
class BackgroundTask final : public IBackgroundTask {
public:
    // appClosing: pass the application-closing token (an empty token never fires).
    BackgroundTask(std::stop_token appClosing, BackgroundTaskExecuteEvent onExecute,
                   const std::shared_ptr<IThreadConfig>& threadConfig, QString threadDebugName);
    ~BackgroundTask() override;

    void start() override;
    void terminate() override;
    void startExecute() override;
    void stopExecute() override;

private:
    void execute();

    BackgroundTaskExecuteEvent onExecute_;
    QString debugName_;
    OperationNotifier cancel_;
    std::stop_source terminated_;
    std::mutex mutex_;
    std::condition_variable_any wakeUp_;
    bool needStart_ = false;    // FNeedStart
    bool allowExecute_ = false; // FAllowExecuteHandle
    bool started_ = false;      // FStarted
    std::optional<std::stop_callback<std::function<void()>>> appClosingCallback_;
    std::jthread thread_;
};

} // namespace cg::core
