// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_InterfaceListStatic.pas, u_ConfigDataElementBase.pas (the static half)
//
// The legacy "publish an immutable copy" idiom: a writer builds a fresh immutable object under
// its own lock and swaps it in; readers take the pointer once and work on a consistent view for
// as long as they hold it. Replaces the whole family of legacy static list interfaces.

#include <atomic>
#include <memory>
#include <utility>

namespace cg::core {

template <class T> class Snapshot {
public:
    using Ptr = std::shared_ptr<const T>;

    Snapshot() = default;
    explicit Snapshot(Ptr initial) : value_(std::move(initial)) {}

    [[nodiscard]] Ptr get() const noexcept { return value_.load(std::memory_order_acquire); }
    void set(Ptr next) noexcept { value_.store(std::move(next), std::memory_order_release); }

private:
    std::atomic<std::shared_ptr<const T>> value_;
};

} // namespace cg::core
