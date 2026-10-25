// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_Listener.pas, i_Notifier.pas, u_Notifier.pas, u_ListenerByEvent.pas,
//        u_ListenerNotifierLinksList.pas
//
// Change notification inside core, where Qt signals are not available because core objects are
// not QObjects. A subscriber gets a move-only Subscription whose destruction unsubscribes, which
// replaces the legacy "remember to call Remove in your destructor" listener contract.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace cg::core {

class NotifierState; // opaque, defined in the .cpp

class Subscription {
public:
    Subscription() = default;
    ~Subscription();
    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    void reset() noexcept; // ports IListenerDisconnectable.Disconnect
    [[nodiscard]] bool isActive() const noexcept;

private:
    friend class Notifier;
    Subscription(std::weak_ptr<NotifierState> owner, std::uint64_t id);

    std::weak_ptr<NotifierState> owner_;
    std::uint64_t id_ = 0;
};

class Notifier {
public:
    Notifier();
    ~Notifier();
    Notifier(const Notifier&) = delete;
    Notifier& operator=(const Notifier&) = delete;

    [[nodiscard]] Subscription subscribe(std::function<void()> slot);
    void notify() const;
    [[nodiscard]] std::size_t subscriberCount() const; // tests only

private:
    std::shared_ptr<NotifierState> state_;
};

} // namespace cg::core
