// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/i_RegionProcessTask.pas:27-30

#include "cg/core/result.h"

namespace cg::region {

class IRegionProcessTask {
public:
    virtual ~IRegionProcessTask() = default;
    // Legacy ProcessRegion is a procedure that reports failure by raising; the port returns the error.
    [[nodiscard]] virtual core::Result<void> processRegion() = 0;
};

} // namespace cg::region
