// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/xml_escape.h"

#include <cstdint>

namespace cg::vector {

namespace {

// Table entries of u_XmlEscape.pas:37-47 (XET_NONE ... XET_ILLEGAL).
enum class Esc : std::uint8_t { None, Tab, Lf, Cr, Lt, Gt, Amp, Quot, Apos, Illegal };

// The 64-entry tables of FillTable / InitTables (u_XmlEscape.pas:93-124; XML_ESC_HIGH = 63, :32).
// Characters above U+003F are not in the table and are never escaped. The attribute table adds the
// two quotes.
Esc escKind(char16_t c, bool attr) {
    if (c > 63) {
        return Esc::None;
    }
    if (c >= 1 && c <= 31) {
        switch (c) {
        case 9:
            return Esc::Tab;
        case 10:
            return Esc::Lf;
        case 13:
            return Esc::Cr;
        default:
            return Esc::Illegal;
        }
    }
    switch (c) {
    case '<':
        return Esc::Lt;
    case '>':
        return Esc::Gt;
    case '&':
        return Esc::Amp;
    case '"':
        return attr ? Esc::Quot : Esc::None;
    case '\'':
        return attr ? Esc::Apos : Esc::None;
    default:
        return Esc::None;
    }
}

// XML_NAMED (u_XmlEscape.pas:59-72) and XML_NUMERIC (:74-87). Illegal characters map to ''.
QString replacement(Esc kind, bool numeric) {
    if (numeric) {
        switch (kind) {
        case Esc::Tab:
            return QStringLiteral("&#09;");
        case Esc::Lf:
            return QStringLiteral("&#10;");
        case Esc::Cr:
            return QStringLiteral("&#13;");
        case Esc::Lt:
            return QStringLiteral("&#60;");
        case Esc::Gt:
            return QStringLiteral("&#62;");
        case Esc::Amp:
            return QStringLiteral("&#38;");
        case Esc::Quot:
            return QStringLiteral("&#34;");
        case Esc::Apos:
            return QStringLiteral("&#39;");
        default:
            return {};
        }
    }
    switch (kind) {
    case Esc::Tab:
        return QStringLiteral("&#x09;");
    case Esc::Lf:
        return QStringLiteral("&#x0a;");
    case Esc::Cr:
        return QStringLiteral("&#x0d;");
    case Esc::Lt:
        return QStringLiteral("&lt;");
    case Esc::Gt:
        return QStringLiteral("&gt;");
    case Esc::Amp:
        return QStringLiteral("&amp;");
    case Esc::Quot:
        return QStringLiteral("&quot;");
    case Esc::Apos:
        return QStringLiteral("&apos;");
    default:
        return {};
    }
}

// XmlEscape (u_XmlEscape.pas:126-211). The legacy pass 1 only sizes the result and pass 2 copies;
// the port appends in one pass. The NUL rule is kept as legacy: the scan stops at U+0000 (also the
// end of the text), and a text in which nothing was escaped before that point is returned as it is,
// including whatever follows the NUL (legacy :171-173).
QString xmlEscape(const QString& text, bool attr, bool numeric) {
    if (text.isEmpty()) {
        return {};
    }

    const auto charAt = [&text](qsizetype i) -> char16_t {
        return i < text.size() ? text.at(i).unicode() : char16_t{0};
    };
    // A character is copied unchanged unless the table has an entry for it (:153, :186).
    // U+0000 is a stop, not a copied character (table entry 0 is XET_TAB, :99; :159, :197).
    const auto isStop = [attr](char16_t c) { return c == 0 || escKind(c, attr) != Esc::None; };

    QString out;
    out.reserve(text.size());
    bool isModified = false;
    qsizetype i = 0;
    for (;;) {
        const qsizetype start = i;
        while (!isStop(charAt(i))) {
            ++i;
        }
        out += text.mid(start, i - start);

        const char16_t c = charAt(i);
        if (c == 0) {
            break;
        }
        out += replacement(escKind(c, attr), numeric);
        isModified = true;
        ++i;
    }

    return isModified ? out : text;
}

} // namespace

QString xmlEscapeText(const QString& text, bool useNumericReference) {
    return xmlEscape(text, false, useNumericReference); // :213-220
}

QString xmlEscapeAttr(const QString& value, bool useNumericReference) {
    return xmlEscape(value, true, useNumericReference); // :222-229
}

} // namespace cg::vector
