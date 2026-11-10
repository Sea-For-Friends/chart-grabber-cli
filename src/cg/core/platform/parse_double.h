// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors.

#pragma once
// Ports: nothing (macOS port). Apple's libc++ has std::from_chars for double only from macOS 26, so the
// deployment target 12 needs another parser there; Linux and Windows keep std::from_chars.

#include <cstddef>

namespace cg::core::platform {

// Parses a decimal floating-point literal in the C locale. The script lexer passes only
// digits[.digits][(e|E)[+|-]digits]; that grammar is what is promised to be identical on every platform (an
// optional leading '-' as well). Same contract as std::from_chars(first, last, value): true when a leading
// decimal number parsed (trailing characters are ignored, e.g. "1,5" gives 1; the end position is not
// reported); on out-of-range (overflow, or a result that is zero/infinite by range error) or no digits,
// `value` is left unchanged and the result is false. Other spellings ("+1", blanks, hex, "inf", "nan") are
// not promised.
[[nodiscard]] bool parseDouble(const char* first, const char* last, double& value);

// The strtod_l implementation that parseDouble uses on Apple, compiled on every POSIX platform (on Windows it
// is std::from_chars) so that its scanner is exercised by the Linux tests: tests/core/parse_double_test.cpp
// compares it with std::from_chars.
[[nodiscard]] bool parseDoubleStrtod(const char* first, const char* last, double& value);

} // namespace cg::core::platform
