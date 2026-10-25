// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Delphi System.SysUtils text semantics for text read from foreign files (an XML attribute, a
// SQLite column) -- Qt's QString::toInt/toDouble accept and reject different spellings.
// Only the functions that have a caller in this port. Ports: LowerCase, SameText, Trim,
// StrToInt/TryStrToInt, TryStrToFloat (with FormatSettings.DecimalSeparator = '.').
//
// Delphi's documented behaviour, as implemented here:
//  * LowerCase folds the 7-bit ASCII letters only.
//  * SameText is equality of the two LowerCase results (ASCII folding only).
//  * Trim removes every character <= ' ' (U+0020) from both ends -- control characters included,
//    NBSP and other Unicode spaces NOT (unlike QString::trimmed).
//  * StrToInt skips leading blanks (' ' only), then an optional '+' or '-', then either decimal
//    digits or a hex number introduced by '$', 'x', 'X', '0x' or '0X'. Nothing may follow the
//    digits (trailing blanks are an error). A decimal outside Integer's range is an error; a hex
//    number takes up to 32 bits and wraps into the Integer range ('$FFFFFFFF' is -1).
//  * TryStrToInt64 follows the same rules widened to 64 bits (a hex number takes up to 64 bits and wraps
//    into the Int64 range). UNVERIFIED: the RTL is not in the legacy tree; the 64-bit part (blanks, hex
//    wrap) is by analogy with the Integer rule.
//  * TryStrToFloat skips leading and trailing blanks (' ' only) and accepts
//    [sign] digits ['.' digits] [('e'|'E') [sign] digits] with at least one mantissa digit.

#include <QString>

namespace cg::core {

[[nodiscard]] QString delphiLowerCase(const QString& text);
[[nodiscard]] QString delphiUpperCase(const QString& text);
[[nodiscard]] bool delphiSameText(const QString& a, const QString& b);
[[nodiscard]] QString delphiTrim(const QString& text);
[[nodiscard]] bool delphiTryStrToInt(const QString& text, int& value);
[[nodiscard]] bool delphiTryStrToInt64(const QString& text, qint64& value); // 64-bit rules unverified
[[nodiscard]] bool delphiTryStrToFloat(const QString& text, double& value);

} // namespace cg::core
