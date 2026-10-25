// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing.
//
// docs/specs/geo.md §2.5.3/§8.2 clamping notes log to a `logGeo` category (core.md §2.7's
// pattern: one QLoggingCategory per module). cg::core::log.h does not declare one yet (it lists
// logCore/logConfig/logStorage/logNet/logRender/logMarks/logRegion/logGps/logUi only) and this
// ticket's module lock forbids adding it there (docs/conventions/agent-protocol.md "Parallel
// development rule"). Declared locally here instead; trivial to move into cg::core::log.h later
// (open question recorded in docs/backlog/T-0016).

#include <QLoggingCategory>

namespace cg::geo {

Q_DECLARE_LOGGING_CATEGORY(logGeo)

} // namespace cg::geo
