// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors.

#pragma once
// Ports: nothing (T-0396, macOS port). The directory of the running executable, for the bundled proj.db
// (T-0376).

#include <QString>

namespace cg::core::platform {

// Canonical directory of the running executable: Linux via /proc/self/exe, macOS via _NSGetExecutablePath.
// Empty where unknown (Windows: libproj already looks in its own directory) or on failure.
[[nodiscard]] QString executableDir();

} // namespace cg::core::platform
