// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ReadWriteStateInternalByOther.pas
//
// A read/write state that follows another one. Until setOther() is given a source it reports
// (read = false, write = false). It forwards every change notification of the source and also
// notifies when the source itself is swapped. It keeps its source alive and unsubscribes from it
// on destruction.

#include "cg/core/read_write_state.h"
#include "cg/core/sync.h"

namespace cg::core {

class IReadWriteStateInternalByOther : public IReadWriteStateChangeble {
public:
    virtual void setOther(const ReadWriteStateChangeablePtr& state) = 0;
};

// FEAT-GEO-033
class ReadWriteStateInternalByOther final : public IReadWriteStateInternalByOther {
public:
    ReadWriteStateInternalByOther();
    ~ReadWriteStateInternalByOther() override;

    void setOther(const ReadWriteStateChangeablePtr& state) override;
    [[nodiscard]] ReadWriteStateStaticPtr getStatic() const override;

private:
    mutable Mutex mutex_;
    ReadWriteStateChangeablePtr other_;
    ReadWriteStateStaticPtr default_;
    Subscription otherSubscription_;
};

} // namespace cg::core
