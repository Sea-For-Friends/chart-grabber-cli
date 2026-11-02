// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors.

#include "cg/core/platform/parse_double.h"

#include <charconv>

#ifndef _WIN32
#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#ifdef __APPLE__
#include <xlocale.h>
#endif
#endif

namespace cg::core::platform {

bool parseDoubleStrtod(const char* first, const char* last, double& value) {
#ifdef _WIN32
    return std::from_chars(first, last, value).ec ==
           std::errc{}; // no strtod_l pair here; Windows keeps from_chars
#else
    // strtod_l with the C locale: independent of setlocale()/LC_NUMERIC (a comma decimal separator changes
    // nothing).
    static const locale_t cLocale = ::newlocale(LC_ALL_MASK, "C", nullptr);
    if (first == last || cLocale == nullptr) {
        return false;
    }
    // Only the leading decimal number counts, as with from_chars: [-] digits [. digits] [(e|E) [+|-] digits]
    // (strtod alone would also take leading blanks, '+', hex and "inf"/"nan").
    const char* p = first;
    if (p != last && *p == '-') {
        ++p;
    }
    const auto digits = [&p, last] {
        const char* const begin = p;
        while (p != last && *p >= '0' && *p <= '9') {
            ++p;
        }
        return p - begin;
    };
    std::ptrdiff_t count = digits();
    if (p != last && *p == '.') {
        ++p;
        count += digits();
    }
    if (count == 0) {
        return false;
    }
    if (p != last && (*p == 'e' || *p == 'E')) {
        const char* const mark = p;
        ++p;
        if (p != last && (*p == '+' || *p == '-')) {
            ++p;
        }
        if (digits() == 0) {
            p = mark; // "1e" / "1e+": the exponent is not part of the number
        }
    }
    const std::string text(first, p); // strtod needs a terminated string
    errno = 0;
    char* end = nullptr;
    const double parsed = ::strtod_l(text.c_str(), &end, cLocale);
    if (end != text.c_str() + text.size()) {
        return false;
    }
    if (errno == ERANGE && (std::isinf(parsed) || parsed == 0.0)) {
        return false; // out of range: from_chars leaves the value unchanged
    }
    value = parsed;
    return true;
#endif
}

bool parseDouble(const char* first, const char* last, double& value) {
#ifdef __APPLE__
    return parseDoubleStrtod(first, last, value); // libc++ before macOS 26 has no from_chars for double
#else
    return std::from_chars(first, last, value).ec == std::errc{};
#endif
}

} // namespace cg::core::platform
