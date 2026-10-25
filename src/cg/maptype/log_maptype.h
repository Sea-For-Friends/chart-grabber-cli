// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing.
//
// The module's QLoggingCategory (core.md §2.7: one category per module). cg::core::log.h has no
// logMapType and this ticket's module lock forbids adding it there; declared locally like
// cg::geo::logGeo.

#include <QLoggingCategory>

namespace cg::maptype {

Q_DECLARE_LOGGING_CATEGORY(logMapType)

} // namespace cg::maptype
