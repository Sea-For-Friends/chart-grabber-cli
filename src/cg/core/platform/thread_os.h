// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ReadableThreadNames.pas (SetCurrentThreadName). Linux only; best effort.

#include <QString>

namespace cg::core::platform {

// Names the calling thread (shown by top/gdb); truncated to the 15 characters Linux allows.
void setCurrentThreadName(const QString& name) noexcept;

// The OS thread id (Linux gettid()) of the calling thread, as the script log prints it.
[[nodiscard]] long currentThreadId() noexcept;

} // namespace cg::core::platform
