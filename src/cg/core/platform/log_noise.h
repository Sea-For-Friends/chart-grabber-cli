// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors.

#pragma once
// Ports: nothing (T-0396, macOS port).

namespace cg::core::platform {

// macOS only: Qt prints a one-off "qt.qpa.fonts: Populating font family aliases ..." line to stderr at the
// first font lookup, and the CLI's stderr carries only its own messages. Sets
// QT_LOGGING_RULES=qt.qpa.fonts.warning=false unless the user already set QT_LOGGING_RULES; call it before
// the first Qt logging category is used (main(), or a static initialiser in the tests). Does nothing on other
// platforms: the fonts diagnostics stay visible there (the bundled fontconfig needs them).
void silenceQtPlatformLogNoise();

} // namespace cg::core::platform
