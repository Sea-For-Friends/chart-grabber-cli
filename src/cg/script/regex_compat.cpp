// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/script/regex_compat.h"

#include "cg/core/sync.h"

#include <utility>
#include <vector>

namespace cg::script {

namespace {

// Escapes TRegExpr recognises *outside* a character class (`RegExpr.pas` ParseAtom, the
// `EscChar:` branch, ~:2458-2524): `b`/`B` word boundary, `A` absolute start, `Z` end of string
// (rewritten separately, R2), `d`/`D`/`s`/`S`/`w`/`W` shorthand classes, `1`-`9` backreference
// (rewritten separately, R10), and `t`/`n`/`r`/`f`/`a`/`e`/`x` via `UnQuoteChar` (~:2225-2260).
// Anything else falls to `UnQuoteChar`'s default case - the literal char, backslash dropped (R3).
bool isKnownEscapeOutsideClass(char c) {
    switch (c) {
    case 'b':
    case 'B':
    case 'A':
    case 'd':
    case 'D':
    case 's':
    case 'S':
    case 'w':
    case 'W':
    case 't':
    case 'n':
    case 'r':
    case 'f':
    case 'a':
    case 'e':
    case 'x':
        return true;
    default:
        return false;
    }
}

// Escapes TRegExpr recognises *inside* `[...]` (ParseAtom's class-member loop, ~:2388-2397): only
// lowercase `d`/`w`/`s` expand to a range string; everything else - including uppercase `D`/`W`/`S`
// (R4) - falls to the same `UnQuoteChar` default as outside a class, so `t`/`n`/`r`/`f`/`a`/`e`/`x`
// are still recognised there too (`UnQuoteChar` is shared).
bool isKnownEscapeInsideClass(char c) {
    switch (c) {
    case 'd':
    case 'w':
    case 's':
    case 't':
    case 'n':
    case 'r':
    case 'f':
    case 'a':
    case 'e':
    case 'x':
        return true;
    default:
        return false;
    }
}

bool isAsciiAlnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// R7: ModifierR is always on (RegExprUtils.pas never clears it) and expands `а-я`/`А-Я`-style class
// ranges via a CP1251 Cyrillic table this port does not vendor. Detected by byte value - CP1251's
// Cyrillic letters (upper and lower, including Ё/ё) all sit at 0xC0 and above; zero corpus uses
// this (§6.2 R7), so this is a defensive check, not one exercised by a real map script.
bool isCp1251CyrillicLetter(unsigned char b) {
    return b >= 0xC0;
}

struct RewriteResult {
    QByteArray pattern;
    bool ok = true;
    QString error;
};

// The R1-R16 pattern rewrite (spec docs/specs/script.md:1254-1271, §6.2). R1 (dot-matches-newline)
// and R14 (ASCII-only \w/\b) are not pattern text rewrites - they are QRegularExpression options
// set unconditionally by the caller. R8/R9/R11 (PCRE2-superset constructs TRegExpr rejected) need
// no rewrite - left as-is, on purpose (a strict improvement, D-9).
RewriteResult rewritePattern(const QByteArray& src) {
    RewriteResult out;
    out.pattern.reserve(src.size() + 8);
    const qsizetype n = src.size();
    bool inClass = false;
    // R5: the previous character class member was a `\d`/`\w` shorthand (not range-eligible in
    // TRegExpr - a following `-` is a literal dash there, but PCRE2 raises error 150 for it).
    bool prevWasShorthand = false;

    for (qsizetype i = 0; i < n;) {
        const unsigned char c = static_cast<unsigned char>(src[i]);

        if (c == '\\') {
            if (i + 1 >= n) {        // trailing backslash: TRegExpr raises reeTrailingBackSlash; leave it
                out.pattern += '\\'; // for QRegularExpression::isValid() to reject too (R16).
                ++i;
                continue;
            }
            const char e = src[i + 1];
            if (!inClass) {
                if (e == 'Z') { // R2: \Z is the same "true end of string" opcode as bare $
                    out.pattern += "\\z";
                    i += 2;
                    continue;
                }
                if (e == 's') { // R13: TRegExpr's SpaceChars has no \v; PCRE2's \s does
                    out.pattern += "[ \\t\\n\\r\\f]";
                    i += 2;
                    continue;
                }
                if (e == 'S') {
                    out.pattern += "[^ \\t\\n\\r\\f]";
                    i += 2;
                    continue;
                }
                if (e >= '1' && e <= '9') {
                    // R10: TRegExpr's backreference escape is always exactly one digit - `\10` is
                    // `\1` followed by the literal char '0'. PCRE2's own multi-digit backreference
                    // lookahead would re-absorb a plain `\1` + literal `0` right back into a
                    // (non-existent, for fewer than 10 groups) 2-digit reference - verified with
                    // `grep -P '(a)\10'` against `(a)a` failing to match while `(a)\g{1}0` matches
                    // - so the bounded `\g{n}` form is used instead of bare `\n`, which never
                    // absorbs trailing digits.
                    out.pattern += "\\g{";
                    out.pattern += e;
                    out.pattern += '}';
                    i += 2;
                    while (i < n && src[i] >= '0' && src[i] <= '9') {
                        out.pattern += src[i]; // literal digit(s) after the single-digit backref
                        ++i;
                    }
                    continue;
                }
                if (isKnownEscapeOutsideClass(e)) {
                    out.pattern += '\\';
                    out.pattern += e;
                    i += 2;
                    continue;
                }
            } else {
                if (e == 's') { // R13, merged in place - already inside an open `[...]`
                    out.pattern += " \\t\\n\\r\\f";
                    prevWasShorthand = true; // R5: \s is a shorthand too - a bordering '-' stays literal
                    i += 2;
                    continue;
                }
                if (isKnownEscapeInsideClass(e)) {
                    out.pattern += '\\';
                    out.pattern += e;
                    prevWasShorthand = (e == 'd' || e == 'w');
                    i += 2;
                    continue;
                }
            }
            // R3/R4/digit-0: unknown escape, in or out of a class - TRegExpr's UnQuoteChar always
            // drops the backslash and emits the literal char. For punctuation, PCRE2 already
            // treats `\<punct>` as that literal char (same as TRegExpr), so it is left untouched -
            // stripping it would turn an escaped metachar (`\.`, `\*`, ...) into an active one.
            if (isAsciiAlnum(e)) {
                out.pattern += e;
            } else {
                out.pattern += '\\';
                out.pattern += e;
            }
            prevWasShorthand = false;
            i += 2;
            continue;
        }

        if (!inClass && c == '[') {
            inClass = true;
            out.pattern += '[';
            ++i;
            if (i < n && src[i] == '^') {
                out.pattern += '^';
                ++i;
            }
            if (i < n && src[i] == ']') { // leading `]` is a literal member, same in both engines
                out.pattern += ']';
                ++i;
            }
            continue;
        }

        if (inClass) {
            if (c == '[') { // R6: TRegExpr has no nested/POSIX class - a 2nd `[` is a literal char
                out.pattern += "\\[";
                prevWasShorthand = false;
                ++i;
                continue;
            }
            if (c == ']') {
                inClass = false;
                prevWasShorthand = false;
                out.pattern += ']';
                ++i;
                continue;
            }
            if (c == '-') {
                // R5: RegExpr.pas:2345-2349 makes '-' literal next to any of \d \D \s \S \w \W,
                // not just \d/\w.
                const bool nextIsShorthand = (i + 2 < n) && src[i + 1] == '\\' &&
                                             (src[i + 2] == 'd' || src[i + 2] == 'D' || src[i + 2] == 's' ||
                                              src[i + 2] == 'S' || src[i + 2] == 'w' || src[i + 2] == 'W');
                if (prevWasShorthand || nextIsShorthand) { // R5
                    out.pattern += "\\-";
                    prevWasShorthand = false;
                    ++i;
                    continue;
                }
                if (i > 0 && i + 1 < n) { // R7
                    const auto prevByte = static_cast<unsigned char>(src[i - 1]);
                    const auto nextByte = static_cast<unsigned char>(src[i + 1]);
                    if (isCp1251CyrillicLetter(prevByte) && isCp1251CyrillicLetter(nextByte)) {
                        out.ok = false;
                        out.error = QStringLiteral("Cyrillic range in a character class is not supported");
                        return out;
                    }
                }
                out.pattern += '-';
                prevWasShorthand = false;
                ++i;
                continue;
            }
            out.pattern += static_cast<char>(c);
            prevWasShorthand = false;
            ++i;
            continue;
        }

        // --- outside a class ---
        if (c == '$') { // R2
            out.pattern += "\\z";
            ++i;
            continue;
        }
        if (c == '{' && i + 1 < n && src[i + 1] == ',') { // R12
            out.pattern += "\\{";
            ++i;
            continue;
        }

        out.pattern += static_cast<char>(c);
        ++i;
    }
    return out;
}

// TRegExpr.Substitute (RegExpr.pas:3700-3813), reimplemented directly against a
// QRegularExpressionMatch rather than translated into Qt's own `\1`-`\9` replacement-string
// syntax: Qt's template engine caps backreferences at a single digit, but TRegExpr's NSUBEXP = 15
// means `$10`-`$14` are valid and must be distinguished from `$1` followed by a literal digit
// (§6.3's "multi-digit is greedy"). Matching `ParseVarName` (RegExpr.pas:3709-3730) exactly:
// `${...}` requires a closing `}` or the whole thing (braces included) is not a variable reference
// at all; `$&`/`${&}` mean group 0; a bare `$` not followed by a digit/`{`/`&` is a literal `$`.
int parseVarName(const QByteArray& tpl, qsizetype& pos) {
    const qsizetype start = pos;
    const qsizetype n = tpl.size();
    qsizetype p = pos;
    const bool delimited = (p < n) && tpl[p] == '{';
    if (delimited) {
        ++p;
    }
    int result = 0;
    if (p < n && tpl[p] == '&') {
        ++p;
    } else {
        while (p < n && tpl[p] >= '0' && tpl[p] <= '9') {
            result = result * 10 + (tpl[p] - '0');
            ++p;
        }
    }
    if (delimited) {
        if (p < n && tpl[p] == '}') {
            ++p;
        } else {
            p = start; // not properly terminated -> the whole thing isn't a variable reference
        }
    }
    if (p == start) {
        return -1;
    }
    pos = p;
    return result;
}

QByteArray substituteTemplate(const QRegularExpressionMatch& match, const QByteArray& tpl) {
    const int captureCount = match.regularExpression().captureCount();
    QByteArray result;
    result.reserve(tpl.size());
    const qsizetype n = tpl.size();
    qsizetype p = 0;
    while (p < n) {
        const char ch = tpl[p];
        ++p;
        if (ch == '$') {
            const int groupNum = parseVarName(tpl, p); // advances p past the reference on success
            if (groupNum >= 0) {
                // NSUBEXP = 15 (0..14); a valid-looking but out-of-range or non-participating group
                // is silently empty (§6.3) - `tRegExprReplace` (via `captureCount`) drops any
                // reference higher than the pattern's own group count itself, exactly as promised.
                if (groupNum < 15 && groupNum <= captureCount) {
                    result += match.captured(groupNum).toLatin1();
                }
                continue;
            }
            result += '$'; // not a valid variable reference: literal '$'
            continue;
        }
        if (ch == '\\' && p < n) { // \X: backslash dropped, X emitted (covers \$ and \\ too)
            result += tpl[p];
            ++p;
            continue;
        }
        result += ch;
    }
    return result;
}

} // namespace

CompiledRegex compileTRegExpr(const QByteArray& pattern) {
    constexpr qsizetype kCapacity = 32;
    static core::Mutex mutex;
    static std::vector<std::pair<QByteArray, CompiledRegex>> cache; // MRU at the back

    {
        core::WriteLock lock(mutex);
        for (std::size_t i = 0; i < cache.size(); ++i) {
            if (cache[i].first == pattern) {
                CompiledRegex hit = cache[i].second;
                auto entry = std::move(cache[i]);
                cache.erase(cache.begin() + static_cast<std::ptrdiff_t>(i));
                cache.push_back(std::move(entry));
                return hit;
            }
        }
    }

    const RewriteResult rr = rewritePattern(pattern);
    CompiledRegex result;
    if (!rr.ok) {
        result.ok = false;
        result.error = rr.error;
    } else {
        // R1: TRegExpr's ModifierS defaults ON and RegExprUtils.pas never clears it - dot always
        // matches \n. R14: UseUnicodePropertiesOption is left off (the default) so \w/\b/\d/\s stay
        // ASCII-only, matching TRegExpr's fixed WordChars/SpaceChars tables.
        result.re = QRegularExpression(QString::fromLatin1(rr.pattern),
                                       QRegularExpression::DotMatchesEverythingOption);
        result.ok = result.re.isValid();
        if (!result.ok) { // R16: surfaced as a runtime error by the caller, never a silent no-match
            result.error = QStringLiteral("invalid regular expression: %1").arg(result.re.errorString());
        }
    }

    core::WriteLock lock(mutex);
    for (std::size_t i = 0; i < cache.size(); ++i) { // a racing thread may have just inserted this
        if (cache[i].first == pattern) {
            cache.erase(cache.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    if (cache.size() >= static_cast<std::size_t>(kCapacity)) {
        cache.erase(cache.begin()); // evict the least-recently-used entry
    }
    cache.emplace_back(pattern, result);
    return result;
}

QByteArray tRegExprMatch(const QByteArray& subject, const QByteArray& pattern, int matchId) {
    const CompiledRegex cre = compileTRegExpr(pattern);
    if (!cre.ok) {
        return QByteArray();
    }
    QByteArray truncated = subject;
    const qsizetype nul = truncated.indexOf('\0'); // R15: TRegExpr matches a NUL-terminated C buffer
    if (nul >= 0) {
        truncated.truncate(nul);
    }
    const QRegularExpressionMatch m = cre.re.match(QString::fromLatin1(truncated));
    if (!m.hasMatch()) {
        return QByteArray();
    }
    if (matchId < 0 || matchId > cre.re.captureCount()) { // RegExprUtils.pas:39-42's own bound check
        return QByteArray();
    }
    return m.captured(matchId).toLatin1();
}

QByteArray tRegExprReplace(const QByteArray& subject, const QByteArray& pattern,
                           const QByteArray& replacement) {
    const CompiledRegex cre = compileTRegExpr(pattern);
    if (!cre.ok) {
        return QByteArray();
    }
    QByteArray truncated = subject;
    const qsizetype nul = truncated.indexOf('\0'); // R15
    if (nul >= 0) {
        truncated.truncate(nul);
    }
    const QString subjectStr = QString::fromLatin1(truncated);
    QRegularExpressionMatchIterator it = cre.re.globalMatch(subjectStr); // F14: global replace
    if (!it.hasNext()) {
        return subject; // no match -> the ORIGINAL (untruncated) subject, unchanged
    }
    QByteArray out;
    qsizetype lastEnd = 0; // Latin-1 round-trips 1:1, so QString offsets are byte offsets here too
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += truncated.mid(lastEnd, m.capturedStart(0) - lastEnd);
        out += substituteTemplate(m, replacement);
        lastEnd = m.capturedEnd(0);
    }
    // R15: TRegExpr.Replace (RegExpr.pas:3845) copies the tail from the ORIGINAL AInputStr, not
    // the NUL-truncated buffer - bytes after an embedded NUL survive when there is a match.
    out += subject.mid(lastEnd);
    return out;
}

} // namespace cg::script
