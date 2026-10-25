// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_request_task.h"

#include <stdexcept>
#include <utility>

namespace cg::maptype {

TileRequestTask::TileRequestTask(ITileRequestPtr tileRequest, std::stop_token softCancel,
                                 std::stop_token cancel, ITileRequestTaskFinishNotifierPtr finishNotifier)
    : tileRequest_(std::move(tileRequest)), softCancel_(std::move(softCancel)), cancel_(std::move(cancel)),
      finishNotifier_(std::move(finishNotifier)) {}

ITileRequestPtr TileRequestTask::tileRequest() const {
    return tileRequest_;
}

std::stop_token TileRequestTask::softCancel() const {
    return softCancel_;
}

std::stop_token TileRequestTask::cancel() const {
    return cancel_;
}

void TileRequestTask::setFinished(const ITileRequestResultPtr& result) {
    if (!finishNotifier_) {
        throw std::logic_error("TileRequestTask: no finish notifier");
    }
    finishNotifier_->notify(*this, result);
}

TileRequestTaskFinishNotifier::TileRequestTaskFinishNotifier(TileRequestTaskFinishNotifierCallBack callBack)
    : enabled_(1), callBack_(std::move(callBack)) {}

void TileRequestTaskFinishNotifier::notify(const ITileRequestTask& task,
                                           const ITileRequestResultPtr& result) {
    if (enabled_.load() > 0) {
        callBack_(task, result); // empty callback: std::bad_function_call
    }
}

bool TileRequestTaskFinishNotifier::enabled() const {
    return enabled_.load() > 0;
}

void TileRequestTaskFinishNotifier::setEnabled(bool value) {
    enabled_.store(value ? 1 : 0);
}

} // namespace cg::maptype
