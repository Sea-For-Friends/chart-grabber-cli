// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/delimited_text.h"

namespace cg::core {
namespace {

// Ports SysUtils.ExtractQuotedStr: `pos` is on the opening quote; a doubled quote inside is one
// literal quote; an unterminated run swallows the rest of the text.
[[nodiscard]] QString extractQuoted(QStringView text, qsizetype& pos, QChar quote) {
    QString token;
    ++pos; // opening quote
    while (pos < text.size()) {
        const QChar ch = text[pos];
        ++pos;
        if (ch != quote) {
            token.append(ch);
            continue;
        }
        if (pos < text.size() && text[pos] == quote) {
            token.append(quote);
            ++pos;
            continue;
        }
        break; // closing quote consumed
    }
    return token;
}

} // namespace

QStringList parseDelimitedText(QStringView text, QChar delimiter, QChar quote, bool strictDelimiter) {
    // StrictDelimiter = True: blanks are ordinary characters (no skipping, no second delimiter).
    const auto isBlank = [strictDelimiter](QChar ch) { return !strictDelimiter && ch <= u' '; };
    QStringList tokens;
    qsizetype pos = 0;
    while (pos < text.size() && isBlank(text[pos])) {
        ++pos;
    }
    while (pos < text.size()) {
        if (text[pos] == quote) {
            tokens.append(extractQuoted(text, pos, quote));
        } else {
            const qsizetype start = pos;
            while (pos < text.size() && !isBlank(text[pos]) && text[pos] != delimiter) {
                ++pos;
            }
            tokens.append(text.sliced(start, pos - start).toString());
        }
        while (pos < text.size() && isBlank(text[pos])) {
            ++pos;
        }
        if (pos < text.size() && text[pos] == delimiter) {
            if (pos + 1 >= text.size()) {
                tokens.append(QString());
            }
            ++pos;
            while (pos < text.size() && isBlank(text[pos])) {
                ++pos;
            }
        }
    }
    return tokens;
}

} // namespace cg::core
