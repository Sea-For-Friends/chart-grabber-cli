// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/explorer_sort.h"

#include <QChar>
#include <QString>

#include <cstdint>

namespace cg::core {
namespace {

[[nodiscard]] bool isDigit(QChar c) {
    return c >= u'0' && c <= u'9';
}

// A run of digits starting at `pos`: its value (wrapping, as Delphi's Integer without overflow
// checks) and its length; `pos` moves past the run.
struct DigitRun {
    qint32 value = 0;
    int count = 0;
};

[[nodiscard]] DigitRun readRun(const QString& s, qsizetype& pos) {
    std::uint32_t value = 0;
    int count = 0;
    do {
        ++count;
        value = value * 10U + static_cast<std::uint32_t>(s[pos].unicode() - u'0');
        ++pos;
    } while (pos < s.size() && isDigit(s[pos]));
    return {static_cast<qint32>(value), count};
}

} // namespace

int compareStringOrdinal(QStringView s1, QStringView s2) {
    if (s1.isEmpty()) {
        return s2.isEmpty() ? 0 : -1;
    }
    if (s2.isEmpty()) {
        return 1;
    }
    // Delphi walks a NUL-terminated buffer: an embedded NUL ends the string.
    const QString a = s1.toString().toLower();
    const QString b = s2.toString().toLower();
    const auto atEnd = [](const QString& s, qsizetype pos) { return pos >= s.size() || s[pos].isNull(); };

    qsizetype i = 0;
    qsizetype j = 0;
    while (true) {
        if (atEnd(a, i)) {
            return atEnd(b, j) ? 0 : -1;
        }
        if (atEnd(b, j)) {
            return 1;
        }
        const bool aDigit = isDigit(a[i]);
        const bool bDigit = isDigit(b[j]);
        if (aDigit != bDigit) {
            return aDigit ? -1 : 1;
        }
        if (!aDigit) {
            if (a[i] == b[j]) {
                ++i;
                ++j;
                continue;
            }
            return a[i].unicode() < b[j].unicode() ? -1 : 1;
        }
        const DigitRun ra = readRun(a, i);
        const DigitRun rb = readRun(b, j);
        if (ra.value == rb.value) {
            if (ra.value == 0 && ra.count != rb.count) {
                return ra.count < rb.count ? -1 : 1;
            }
            continue;
        }
        return ra.value < rb.value ? -1 : 1;
    }
}

} // namespace cg::core
