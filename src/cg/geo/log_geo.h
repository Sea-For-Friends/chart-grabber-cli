// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing.
//
// The projection clamping notes log to a `logGeo` category (one QLoggingCategory per module).
// cg::core::log.h does not declare one yet (it lists
// logCore/logConfig/logStorage/logNet/logRender/logMarks/logRegion/logGps/logUi only). Declared
// locally here instead; trivial to move into cg::core::log.h later.

#include <QLoggingCategory>

namespace cg::geo {

Q_DECLARE_LOGGING_CATEGORY(logGeo)

} // namespace cg::geo
