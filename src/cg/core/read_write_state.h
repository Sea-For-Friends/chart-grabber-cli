// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ReadWriteState.pas, u_ReadWriteStateStatic.pas
//
// "May the database be read / written" as an immutable snapshot plus a Changeable that publishes
// it. The marks module hands one of these to its consumers (docs/specs/marks.md section 1.3).

#include "cg/core/changeable.h"

#include <memory>

namespace cg::core {

// FEAT-GEO-033. Ports IReadWriteStateStatic / TReadWriteStateStatic.
struct ReadWriteStateStatic {
    bool readAccess = false;
    bool writeAccess = false;
};
using ReadWriteStateStaticPtr = std::shared_ptr<const ReadWriteStateStatic>;

// Ports IReadWriteStateChangeble (sic: the legacy spelling is kept).
class IReadWriteStateChangeble : public Changeable {
public:
    [[nodiscard]] virtual ReadWriteStateStaticPtr getStatic() const = 0;
};
using ReadWriteStateChangeablePtr = std::shared_ptr<IReadWriteStateChangeble>;

} // namespace cg::core
