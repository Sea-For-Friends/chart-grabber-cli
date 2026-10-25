// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/notifier.h"

#include "cg/core/sync.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace cg::core {

class NotifierState {
public:
    mutable Mutex mutex;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> slots;
    std::uint64_t nextId = 1;
};

Subscription::Subscription(std::weak_ptr<NotifierState> owner, std::uint64_t id)
    : owner_(std::move(owner)), id_(id) {}

Subscription::Subscription(Subscription&& other) noexcept : owner_(std::move(other.owner_)), id_(other.id_) {
    other.owner_.reset();
    other.id_ = 0;
}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        reset();
        owner_ = std::move(other.owner_);
        id_ = other.id_;
        other.owner_.reset();
        other.id_ = 0;
    }
    return *this;
}

Subscription::~Subscription() {
    reset();
}

void Subscription::reset() noexcept {
    if (id_ == 0) {
        return;
    }
    if (const std::shared_ptr<NotifierState> state = owner_.lock()) {
        const WriteLock lock(state->mutex);
        const auto it = std::find_if(state->slots.begin(), state->slots.end(),
                                     [this](const auto& entry) { return entry.first == id_; });
        if (it != state->slots.end()) {
            state->slots.erase(it);
        }
    }
    owner_.reset();
    id_ = 0;
}

bool Subscription::isActive() const noexcept {
    return id_ != 0 && !owner_.expired();
}

Notifier::Notifier() : state_(std::make_shared<NotifierState>()) {}

Notifier::~Notifier() = default;

Subscription Notifier::subscribe(std::function<void()> slot) {
    const WriteLock lock(state_->mutex);
    const std::uint64_t id = state_->nextId++;
    state_->slots.emplace_back(id, std::move(slot));
    return Subscription{state_, id};
}

void Notifier::notify() const {
    // u_Notifier.pas:92-111: copy the list, release the lock, then call. Slots routinely
    // subscribe/unsubscribe from inside a notification; dispatching under the lock deadlocks.
    std::vector<std::function<void()>> pending;
    {
        const ReadLock lock(state_->mutex);
        pending.reserve(state_->slots.size());
        for (const auto& entry : state_->slots) {
            pending.push_back(entry.second);
        }
    }
    for (const auto& slot : pending) {
        slot();
    }
}

std::size_t Notifier::subscriberCount() const {
    const ReadLock lock(state_->mutex);
    return state_->slots.size();
}

} // namespace cg::core
