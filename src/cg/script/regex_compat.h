// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Includes/RegExpr.pas (TRegExpr 0.952, Henry-Spencer backtracking NFA, 8-bit byte
// semantics) via Includes/RegExprUtils.pas (RegExprGetMatchSubStr/RegExprReplaceMatchSubStr)
// and Src/PascalScript/u_PascalScriptRegExpr.pas (the registration shell; no logic of its own).
//
// `compileTRegExpr` rewrites a TRegExpr pattern into what `QRegularExpression` (PCRE2) needs to
// match identically. `tRegExprMatch`/`tRegExprReplace` are the bodies of RegExprGetMatchSubStr
// and RegExprReplaceMatchSubStr. Each rewrite cites its RegExpr.pas source lines.

#include <QByteArray>
#include <QRegularExpression>
#include <QString>

namespace cg::script {

// Result of translating+compiling one TRegExpr pattern. `ok == false` covers both a rewrite this
// port cannot express (Cyrillic ModifierR ranges) and a pattern QRegularExpression itself
// rejects after rewriting - either way the regex functions must surface a *runtime error*, never
// a silent "no match": legacy's `TRegExpr.Create; .Expression := pattern` raises `ERegExpr`,
// uncaught, out of the script.
struct CompiledRegex {
    QRegularExpression re;
    bool ok = false;
    QString error;
};

// Rewrites `pattern` and compiles it, always with `DotMatchesEverythingOption` set (TRegExpr's
// ModifierS defaults ON and `RegExprUtils.pas` never touches it) and `UseUnicodePropertiesOption`
// left off (TRegExpr's `\w`/`\b` are ASCII-only, PCRE2's default without that option already
// matches). Cached in a 32-entry LRU keyed by the raw pattern bytes - `RegExprReplaceMatchSubStr`
// is on the hot path of every tile request for scripts that use it, and recompiling a
// `QRegularExpression` per call was the actual legacy behaviour (fresh `TRegExpr` per call) but is
// wasteful to reproduce literally.
[[nodiscard]] CompiledRegex compileTRegExpr(const QByteArray& pattern);

// `RegExprUtils.pas:33-52`: `Exec` on `subject`, then `Match[matchId]` (`0` = whole match,
// `1..14` = capture groups, `NSUBEXP = 15`). No match, or `matchId` outside `[0, SubExprMatchCount]`
// -> `""`. Caller must check `compileTRegExpr(pattern).ok` first and raise a runtime error itself
// when it is false - this function has no error channel of its own, matching its declared
// signature.
[[nodiscard]] QByteArray tRegExprMatch(const QByteArray& subject, const QByteArray& pattern, int matchId);

// `RegExprUtils.pas:54-68`: no match -> `subject` unchanged. A match -> global replace
// (`Replace(subject, replacement, AUseSubstitution := True)`, `RegExpr.pas:3700-3813`) using
// `replacement` as a *substitution template*, not a literal string. Same caller contract as
// `tRegExprMatch` (an invalid pattern is the caller's runtime error).
[[nodiscard]] QByteArray tRegExprReplace(const QByteArray& subject, const QByteArray& pattern,
                                         const QByteArray& replacement);

} // namespace cg::script
