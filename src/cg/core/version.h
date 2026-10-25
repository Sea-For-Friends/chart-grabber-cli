// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_BuildInfo.pas, u_BuildInfo.pas
//
// Build-system smoke test target, extended (T-0002B) to carry the data the About dialog needs.
// FEAT-UI-194 (the About dialog itself) is a `ui` ticket; this only supplies the data.

namespace cg::core {

const char* versionString();

struct BuildInfo {
    const char* version;   // the CMake PROJECT_VERSION
    const char* gitHash;   // from CMake, "unknown" when not a git checkout
    const char* buildDate; // __DATE__ " " __TIME__
    const char* compiler;  // e.g. "GCC 14.2.1"
    const char* qtVersion; // QT_VERSION_STR
};

[[nodiscard]] const BuildInfo& buildInfo();

} // namespace cg::core
