// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/delphi_text.h"

#include <cstdint>

namespace cg::core {

namespace {

[[nodiscard]] int hexDigit(QChar c) {
    const char16_t u = c.unicode();
    if (u >= u'0' && u <= u'9') {
        return u - u'0';
    }
    if (u >= u'a' && u <= u'f') {
        return u - u'a' + 10;
    }
    if (u >= u'A' && u <= u'F') {
        return u - u'A' + 10;
    }
    return -1;
}

[[nodiscard]] bool isDigit(QChar c) {
    return c.unicode() >= u'0' && c.unicode() <= u'9';
}

} // namespace

QString delphiLowerCase(const QString& text) {
    QString out = text;
    for (QChar& c : out) {
        if (c.unicode() >= u'A' && c.unicode() <= u'Z') {
            c = QChar(static_cast<char16_t>(c.unicode() + (u'a' - u'A')));
        }
    }
    return out;
}

QString delphiUpperCase(const QString& text) {
    QString out = text;
    for (QChar& c : out) {
        if (c.unicode() >= u'a' && c.unicode() <= u'z') {
            c = QChar(static_cast<char16_t>(c.unicode() - (u'a' - u'A')));
        }
    }
    return out;
}

bool delphiSameText(const QString& a, const QString& b) {
    return delphiLowerCase(a) == delphiLowerCase(b);
}

QString delphiTrim(const QString& text) {
    qsizetype first = 0;
    qsizetype last = text.size();
    while (first < last && text[first].unicode() <= u' ') {
        ++first;
    }
    while (last > first && text[last - 1].unicode() <= u' ') {
        --last;
    }
    return text.mid(first, last - first);
}

bool delphiTryStrToInt(const QString& text, int& value) {
    qsizetype i = 0;
    const qsizetype n = text.size();
    while (i < n && text[i] == u' ') {
        ++i;
    }
    bool negative = false;
    if (i < n && (text[i] == u'-' || text[i] == u'+')) {
        negative = text[i] == u'-';
        ++i;
    }
    bool hex = false;
    if (i < n && (text[i] == u'$' || text[i] == u'x' || text[i] == u'X')) {
        hex = true;
        ++i;
    } else if (i + 1 < n && text[i] == u'0' && (text[i + 1] == u'x' || text[i + 1] == u'X')) {
        hex = true;
        i += 2;
    }
    if (i >= n) {
        return false;
    }
    std::uint64_t acc = 0;
    for (; i < n; ++i) {
        const int d = hex ? hexDigit(text[i]) : (isDigit(text[i]) ? text[i].unicode() - u'0' : -1);
        if (d < 0) {
            return false;
        }
        acc = acc * (hex ? 16U : 10U) + static_cast<std::uint64_t>(d);
        if (acc > 0xFFFFFFFFULL) {
            return false;
        }
    }
    if (hex) {
        const auto wrapped = static_cast<std::uint32_t>(negative ? (0U - static_cast<std::uint32_t>(acc))
                                                                 : static_cast<std::uint32_t>(acc));
        value = static_cast<std::int32_t>(wrapped);
        return true;
    }
    if (negative ? acc > 0x80000000ULL : acc > 0x7FFFFFFFULL) {
        return false;
    }
    value = static_cast<int>(negative ? -static_cast<std::int64_t>(acc) : static_cast<std::int64_t>(acc));
    return true;
}

// 64-bit rules unverified (RTL not in the legacy tree; by analogy with the Integer rule above).
bool delphiTryStrToInt64(const QString& text, qint64& value) {
    qsizetype i = 0;
    const qsizetype n = text.size();
    while (i < n && text[i] == u' ') {
        ++i;
    }
    bool negative = false;
    if (i < n && (text[i] == u'-' || text[i] == u'+')) {
        negative = text[i] == u'-';
        ++i;
    }
    bool hex = false;
    if (i < n && (text[i] == u'$' || text[i] == u'x' || text[i] == u'X')) {
        hex = true;
        ++i;
    } else if (i + 1 < n && text[i] == u'0' && (text[i + 1] == u'x' || text[i + 1] == u'X')) {
        hex = true;
        i += 2;
    }
    if (i >= n) {
        return false;
    }
    constexpr std::uint64_t kMax = UINT64_MAX;
    const std::uint64_t base = hex ? 16U : 10U;
    std::uint64_t acc = 0;
    for (; i < n; ++i) {
        const int d = hex ? hexDigit(text[i]) : (isDigit(text[i]) ? text[i].unicode() - u'0' : -1);
        if (d < 0) {
            return false;
        }
        if (acc > (kMax - static_cast<std::uint64_t>(d)) / base) {
            return false;
        }
        acc = acc * base + static_cast<std::uint64_t>(d);
    }
    constexpr std::uint64_t kInt64Min = std::uint64_t{1} << 63U;
    if (!hex && acc > (negative ? kInt64Min : kInt64Min - 1U)) {
        return false;
    }
    value = static_cast<qint64>(negative ? 0U - acc : acc);
    return true;
}

bool delphiTryStrToFloat(const QString& text, double& value) {
    qsizetype i = 0;
    qsizetype n = text.size();
    while (i < n && text[i] == u' ') {
        ++i;
    }
    while (n > i && text[n - 1] == u' ') {
        --n;
    }
    qsizetype p = i;
    if (p < n && (text[p] == u'-' || text[p] == u'+')) {
        ++p;
    }
    int mantissaDigits = 0;
    while (p < n && isDigit(text[p])) {
        ++p;
        ++mantissaDigits;
    }
    if (p < n && text[p] == u'.') {
        ++p;
        while (p < n && isDigit(text[p])) {
            ++p;
            ++mantissaDigits;
        }
    }
    if (mantissaDigits == 0) {
        return false;
    }
    if (p < n && (text[p] == u'e' || text[p] == u'E')) {
        ++p;
        if (p < n && (text[p] == u'-' || text[p] == u'+')) {
            ++p;
        }
        int exponentDigits = 0;
        while (p < n && isDigit(text[p])) {
            ++p;
            ++exponentDigits;
        }
        if (exponentDigits == 0) {
            return false;
        }
    }
    if (p != n) {
        return false;
    }
    bool ok = false;
    const double parsed = text.mid(i, n - i).toDouble(&ok);
    if (!ok) {
        return false; // out of the double range
    }
    value = parsed;
    return true;
}

} // namespace cg::core
