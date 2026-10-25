// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: the parsing half of u_ContentTypeSubstByList.pas:87-133 (Delphi
//        TStrings.DelimitedText with StrictDelimiter = False)
//
// Splits a list value the way the Delphi RTL does, quirks included, because real .zmp files were
// written against exactly those quirks (quoted MIME types with embedded semicolons, whitespace
// acting as a second delimiter).

#include <QChar>
#include <QStringList>
#include <QStringView>

namespace cg::core {

// `strictDelimiter` ports StrictDelimiter = True (u_ZmpInfo.pas:947-950): only `delimiter` separates.
// Parses `text` the way Delphi's TStrings.DelimitedText getter does, with
// StrictDelimiter = False (the default):
//   * a token may be quoted with `quote`; inside a quoted token, a doubled quote is
//     one literal quote; the quoted run ends at the closing quote;
//   * outside quotes, `delimiter` ends a token, and so does ANY character <= ' '
//     (space, tab, CR, LF);
//   * runs of delimiters produce empty tokens; leading/trailing whitespace between
//     tokens is skipped.
[[nodiscard]] QStringList parseDelimitedText(QStringView text, QChar delimiter, QChar quote = u'"',
                                             bool strictDelimiter = false);

} // namespace cg::core
