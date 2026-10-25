// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing (T-0335, Windows port).
// The one place a QString path becomes a std::filesystem::path. On Windows the path is built from the UTF-16
// text, so a name with non-ASCII characters survives (toStdString() would give UTF-8 bytes that the narrow
// Windows APIs read as the ANSI code page). Elsewhere it is the UTF-8 text, as `toStdString()` always was.

#include <QString>

#include <filesystem>

namespace cg::core::platform {

// True when this program runs on Windows, where a drive-letter path is a native path and not a foreign one.
#ifdef _WIN32
inline constexpr bool kWindowsHost = true;
#else
inline constexpr bool kWindowsHost = false;
#endif

[[nodiscard]] inline std::filesystem::path nativePath(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

} // namespace cg::core::platform
