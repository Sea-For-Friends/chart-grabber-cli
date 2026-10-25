// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing.
//
// The module's QLoggingCategory (core.md §2.7: one category per module). Declared locally like
// cg::maptype::logMapType and cg::geo::logGeo; cg::core::log.h has no logRegion.

#include <QLoggingCategory>

namespace cg::region {

Q_DECLARE_LOGGING_CATEGORY(logRegion)

} // namespace cg::region
