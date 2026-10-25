// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing.
// New code (no legacy unit). Inherits: frm_ProgressDownload.pas (the progress window is the user's view of a
// run); the console asks whether stderr is a terminal to decide how the progress line is drawn.

#include <cstdio>

namespace cg::core::platform {

// isatty(fileno(stream)) == 1
[[nodiscard]] bool isTerminal(std::FILE* stream);

// T-0335: Windows only (a no-op elsewhere): `stream` becomes binary (no "\n" -> "\r\n"), and a console gets
// the UTF-8 output code page. Called once per stream by Console.
void prepareOutputStream(std::FILE* stream);

// True when "\n" written to `stream` reaches the file as one byte: always on POSIX; on Windows only in binary
// mode (so after prepareOutputStream). A test seam for the Windows port; on Windows it leaves the stream
// binary.
[[nodiscard]] bool isBinaryStream(std::FILE* stream);

} // namespace cg::core::platform
