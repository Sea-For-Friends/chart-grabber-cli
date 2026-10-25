// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ReadWriteStateInternal.pas, u_ReadWriteStateInternal.pas
//
// The writable state. Invariant kept by the setters (not by the constructor, which stores what it
// is given, as legacy does): lowering read access also lowers write access; raising write access
// also raises read access. changed() fires only on a real change of the stored value.

#include "cg/core/read_write_state.h"
#include "cg/core/sync.h"

namespace cg::core {

class IReadWriteStateInternal : public IReadWriteStateChangeble {
public:
    [[nodiscard]] virtual bool readAccess() const = 0;
    virtual void setReadAccess(bool value) = 0;
    [[nodiscard]] virtual bool writeAccess() const = 0;
    virtual void setWriteAccess(bool value) = 0;
};

// FEAT-GEO-033
class ReadWriteStateInternal final : public IReadWriteStateInternal {
public:
    ReadWriteStateInternal(bool readAccess, bool writeAccess);

    [[nodiscard]] bool readAccess() const override;
    void setReadAccess(bool value) override;
    [[nodiscard]] bool writeAccess() const override;
    void setWriteAccess(bool value) override;
    [[nodiscard]] ReadWriteStateStaticPtr getStatic() const override;

private:
    [[nodiscard]] ReadWriteStateStaticPtr createStatic() const;

    mutable Mutex mutex_;
    bool readAccess_;
    bool writeAccess_;
    ReadWriteStateStaticPtr static_;
};

} // namespace cg::core
