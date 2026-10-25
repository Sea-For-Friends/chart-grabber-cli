// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/platform/terminal.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace cg::core::platform {

#ifdef _WIN32
bool isTerminal(std::FILE* stream) {
    return _isatty(_fileno(stream)) != 0;
}

void prepareOutputStream(std::FILE* stream) {
    if (stream == nullptr) {
        return;
    }
    // T-0335: the program writes UTF-8 bytes; the CRT text mode would turn "\n" into "\r\n".
    _setmode(_fileno(stream), _O_BINARY);
    if (isTerminal(stream)) {
        ::SetConsoleOutputCP(CP_UTF8);
    }
}

bool isBinaryStream(std::FILE* stream) {
    const int previous = _setmode(_fileno(stream), _O_BINARY); // returns the mode that was set before
    return previous == _O_BINARY;
}
#else
bool isTerminal(std::FILE* stream) {
    return ::isatty(::fileno(stream)) == 1;
}

void prepareOutputStream(std::FILE*) {}

bool isBinaryStream(std::FILE*) {
    return true;
}
#endif

} // namespace cg::core::platform
