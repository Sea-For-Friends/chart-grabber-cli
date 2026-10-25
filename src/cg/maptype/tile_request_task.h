// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileRequestTask.pas, u_TileRequestTask.pas
//
// One queued tile request and the way it reports back. Legacy gives the task NO state: SetFinished is
// a plain forward to the finish notifier (u_TileRequestTask.pas:122-125). There is no "finished" flag,
// so a second SetFinished notifies a second time, a nil result is forwarded as nil, and cancelling
// before or after finishing changes nothing about it. The one-shot guarantee ("finished exactly once")
// is the job's duty, not the task's.
//
// Cancel. Legacy hands out INotifierOneOperation (soft) and INotifierOperation + OperationID (hard).
// Each becomes one std::stop_token held by value (docs/architecture/idioms.md section 8, marks.md
// Ruling A.2: pure "fire once" signals the consumer only listens to and tests); the task never
// requests a stop itself. A default-constructed token means "never cancelled" (as in
// tile_download_request_builder.h). The finish notification is NOT a stop_token: it carries the result
// and may repeat, so it stays an interface with a callback.
//   soft  - the work not yet started is dropped; a transfer in flight may finish and be saved.
//   hard  - also aborts the transfer in flight (net::RetryDownloader::run(..., soft, hard)).

#include "cg/maptype/tile_request.h"
#include "cg/maptype/tile_request_result.h"

#include <atomic>
#include <functional>
#include <memory>
#include <stop_token>

namespace cg::maptype {

// i_TileRequestTask.pas:32-45
class ITileRequestTask {
public:
    virtual ~ITileRequestTask() = default;
    [[nodiscard]] virtual ITileRequestPtr tileRequest() const = 0;
    [[nodiscard]] virtual std::stop_token softCancel() const = 0; // GetSoftCancelNotifier
    [[nodiscard]] virtual std::stop_token cancel() const = 0;     // GetCancelNotifier + GetOperationID
};
// Not const: the queue consumer reaches setFinished through dynamic_cast to ITileRequestTaskInternal
// (u_TileRequestQueueProcessorThread.pas:109 does Supports(Pull, ITileRequestTaskInternal)).
using ITileRequestTaskPtr = std::shared_ptr<ITileRequestTask>;

// :47-50
class ITileRequestTaskInternal : public ITileRequestTask {
public:
    virtual void setFinished(const ITileRequestResultPtr& result) = 0;
};

// :52-62. `task` is the task that finished (legacy passes Self as a borrowed interface), valid for the call.
class ITileRequestTaskFinishNotifier {
public:
    virtual ~ITileRequestTaskFinishNotifier() = default;
    virtual void notify(const ITileRequestTask& task, const ITileRequestResultPtr& result) = 0;
    [[nodiscard]] virtual bool enabled() const = 0;
    virtual void setEnabled(bool value) = 0;
};
using ITileRequestTaskFinishNotifierPtr = std::shared_ptr<ITileRequestTaskFinishNotifier>;

// u_TileRequestTask.pas:35-58. Thread-safe by being immutable. setFinished runs the notifier on the
// calling thread, with no lock.
class TileRequestTask final : public ITileRequestTaskInternal {
public:
    // Legacy asserts a non-null finish notifier (:93, off in the shipped build); a null one is kept
    // and setFinished then throws std::logic_error (legacy: EAccessViolation at :124, a Delphi exception with
    // handlers: a reproduction).
    TileRequestTask(ITileRequestPtr tileRequest, std::stop_token softCancel, std::stop_token cancel,
                    ITileRequestTaskFinishNotifierPtr finishNotifier);

    [[nodiscard]] ITileRequestPtr tileRequest() const override;
    [[nodiscard]] std::stop_token softCancel() const override;
    [[nodiscard]] std::stop_token cancel() const override;
    // Notifies the finish notifier with (*this, result) on every call; exceptions from the callback
    // propagate.
    void setFinished(const ITileRequestResultPtr& result) override;

private:
    ITileRequestPtr tileRequest_;
    std::stop_token softCancel_;
    std::stop_token cancel_;
    ITileRequestTaskFinishNotifierPtr finishNotifier_;
};

using TileRequestTaskFinishNotifierCallBack =
    std::function<void(const ITileRequestTask& task, const ITileRequestResultPtr& result)>;

// u_TileRequestTask.pas:65-78. Enabled (lock-free flag) at construction. Disabling does not wait for a
// callback already running and does not stop one that read the flag before; legacy has the same window.
class TileRequestTaskFinishNotifier final : public ITileRequestTaskFinishNotifier {
public:
    // Legacy asserts Assigned(callback) (:133, off in release); an empty one makes notify() on an enabled
    // notifier throw std::bad_function_call (legacy: EAccessViolation at :145, a reproduction). Disabled:
    // nothing happens.
    explicit TileRequestTaskFinishNotifier(TileRequestTaskFinishNotifierCallBack callBack);

    void notify(const ITileRequestTask& task, const ITileRequestResultPtr& result) override;
    [[nodiscard]] bool enabled() const override;
    void setEnabled(bool value) override;

private:
    std::atomic<int> enabled_;
    TileRequestTaskFinishNotifierCallBack callBack_;
};

} // namespace cg::maptype
