// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ThreadConfig.pas, u_ThreadConfig.pas
//
// The user-settable priority of a background thread, persisted as `ThreadPriority` (an integer:
// the Ord of Delphi's TThreadPriority). Only the values tpIdle..tpHigher (0..4) are valid; anything
// else is replaced by the default given at construction (itself replaced by tpLower when out of
// range). How BackgroundTask applies the value on Linux: see background_task.h.

#include "cg/core/config_element.h"

#include <cstdint>
#include <memory>

namespace cg::core {

// Ordinals of Delphi's TThreadPriority (they are written to the INI).
enum class ThreadPriority : std::uint8_t {
    Idle = 0,
    Lowest = 1,
    Lower = 2,
    Normal = 3,
    Higher = 4,
    Highest = 5,
    TimeCritical = 6,
};

// FEAT-GEO-034
class IThreadConfig : public ConfigElement {
public:
    [[nodiscard]] virtual ThreadPriority priority() const = 0;
    virtual void setPriority(ThreadPriority value) = 0;
};

class ThreadConfig final : public IThreadConfig {
public:
    explicit ThreadConfig(ThreadPriority defPriority);

    [[nodiscard]] ThreadPriority priority() const override;
    void setPriority(ThreadPriority value) override;

protected:
    void doReadConfig(const IConfigProviderPtr& src) override;
    void doWriteConfig(const IConfigWriterPtr& dst) const override;

private:
    void setPriorityLocked(int value);

    ThreadPriority priority_;
    ThreadPriority priorityDef_;
};

} // namespace cg::core
