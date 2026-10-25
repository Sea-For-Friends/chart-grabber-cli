// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit).
// Ports: nothing.
//
// The module's QLoggingCategory (core.md §2.7: one category per module). Declared locally like
// cg::region::logRegion; cg::core::log.h has no logApp.

#include <QLoggingCategory>

namespace cg::app {

Q_DECLARE_LOGGING_CATEGORY(logApp)

} // namespace cg::app
