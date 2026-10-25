// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_Changeable.pas, u_ChangeableBase.pas
//
// Base for any object that publishes "I am about to change / I changed / I have finished
// changing". The three-phase order is load-bearing: derived objects rebuild their published
// snapshot in the before-hook so that subscribers of `changed()` already see the new state.

#include "cg/core/notifier.h"

namespace cg::core {

class Changeable {
public:
    virtual ~Changeable() = default;
    Changeable(const Changeable&) = delete;
    Changeable& operator=(const Changeable&) = delete;

    [[nodiscard]] Notifier& beforeChanged() noexcept { return before_; }
    [[nodiscard]] Notifier& changed() noexcept { return changed_; }
    [[nodiscard]] Notifier& afterChanged() noexcept { return after_; }

protected:
    Changeable() = default;

    // Fires before -> changed -> after, in that order (u_ChangeableBase.pas:87-95).
    void fireChanged();
    virtual void doBeforeChangeNotify() {}
    virtual void doChangeNotify() {}
    virtual void doAfterChangeNotify() {}

private:
    Notifier before_;
    Notifier changed_;
    Notifier after_;
};

} // namespace cg::core
