// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Basic/u_NetworkStrFunc.pas:69-403; Src/PascalScript/u_PascalScriptUtils.pas:88-119.
// FEAT-NET-011 (this file backs the raw ResponseHead/RequestHead contract that content-type
// detection and the ZMP script header functions both sit on top of). T-0028: moved from
// src/cg/net/http_headers.cpp (T-0012B) so `cg::net` and `cg::script` can share it via `core`.
#include "cg/core/header_text.h"

#include "cg/core/log.h"

#include <optional>

namespace cg::core {

namespace {

[[nodiscard]] constexpr char toUpperAscii7(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
}

[[nodiscard]] bool isControl(char c) noexcept {
    return static_cast<unsigned char>(c) < static_cast<unsigned char>(' ');
}

[[nodiscard]] QByteArray upperAscii7(QByteArrayView s) {
    QByteArray out(s.size(), Qt::Uninitialized);
    for (qsizetype i = 0; i < s.size(); ++i) {
        out[i] = toUpperAscii7(s[i]);
    }
    return out;
}

struct HeaderMatch {
    qsizetype lineStart;     // start of the matched line (the name position)
    qsizetype valueStart;    // start of the value, after the name and any spaces/colons
    qsizetype lineEnd;       // end of line content: first control char, or end of blob
    qsizetype terminatorEnd; // lineEnd plus the entire following run of control chars
};

// Shared line-scan skeleton (docs/specs/net.md §2.11 "The line scan"): a line ends at the first
// control char; only the first matching line is ever acted on.
[[nodiscard]] std::optional<HeaderMatch> findHeaderMatch(QByteArrayView headers, QByteArrayView upName) {
    const qsizetype n = headers.size();
    qsizetype i = 0;
    while (true) {
        qsizetype lineEnd = n;
        for (qsizetype j = i; j < n; ++j) {
            if (isControl(headers[j])) {
                lineEnd = j;
                break;
            }
        }
        if (idemPChar(headers, i, upName)) {
            qsizetype valueStart = i + upName.size();
            while (valueStart < lineEnd && (headers[valueStart] == ' ' || headers[valueStart] == ':')) {
                ++valueStart;
            }
            // Trailing control-char run consumed by deleteHeaderValueUp's cut range: legacy stops
            // extending the run -- without aborting the match -- at a literal '\0', leaving it in
            // place (u_NetworkStrFunc.pas:227-231: `if (AHeaders[K] = #0) or (AHeaders[K] >= ' ')
            // then Break`). tech-lead-rulings.md 2026-09-17 "\0 header-scan stop".
            qsizetype terminatorEnd = lineEnd;
            while (terminatorEnd < n && headers[terminatorEnd] != '\0' && isControl(headers[terminatorEnd])) {
                ++terminatorEnd;
            }
            return HeaderMatch{i, valueStart, lineEnd, terminatorEnd};
        }
        if (lineEnd >= n) {
            return std::nullopt;
        }
        // Advance past this (non-matching) line's control-char terminator to reach the next
        // candidate line. Legacy aborts the whole scan (line not found) the instant it hits a
        // literal '\0' while skipping, instead of treating it like any other control byte
        // (u_NetworkStrFunc.pas:157-163: `if AHeaders[I] = #0 then Exit`). tech-lead-rulings.md
        // 2026-09-17 "\0 header-scan stop" -- fixed here per T-0012B.
        qsizetype next = lineEnd;
        while (next < n && isControl(headers[next])) {
            if (headers[next] == '\0') {
                return std::nullopt;
            }
            ++next;
        }
        if (next >= n) {
            return std::nullopt;
        }
        i = next;
    }
}

} // namespace

bool idemPChar(QByteArrayView headers, qsizetype start, QByteArrayView upName) {
    const qsizetype n = headers.size();
    for (qsizetype idx = 0; idx < upName.size(); ++idx) {
        const qsizetype pos = start + idx;
        if (pos >= n || toUpperAscii7(headers[pos]) != upName[idx]) {
            return false;
        }
    }
    return true;
}

QByteArray getHeaderValueUp(QByteArrayView headers, QByteArrayView upName) {
    if (headers.isEmpty() || upName.isEmpty()) {
        return {};
    }
    const auto match = findHeaderMatch(headers, upName);
    if (!match) {
        return {};
    }
    if (match->valueStart > match->lineEnd) {
        // D-24: when the NAME carries a control byte also present in the header blob, the
        // full-byte name compare (idemPChar, above) can match past the line's own end, so
        // valueStart lands after lineEnd. Legacy's equivalent, GetHeaderValueUp, has the same
        // shape: Result := Copy(AHeaders, I, K-I) at u_NetworkStrFunc.pas:156, and Delphi's
        // Copy with a negative Count returns '' -- reproduce that here instead of slicing with a
        // negative length (QByteArrayView::sliced asserts n >= 0; T-0034).
        return {};
    }
    return headers.sliced(match->valueStart, match->lineEnd - match->valueStart).toByteArray();
}

bool replaceHeaderValueUp(QByteArray& headers, QByteArrayView upName, QByteArrayView value) {
    if (headers.isEmpty()) {
        return false;
    }
    if (upName.isEmpty() || value.isEmpty()) {
        // T-0028: legacy asserts here (a no-op in a release build, SASPlanet.dproj
        // DCC_AssertionsAtRuntime=False); the only observable effect is this diagnostic, logged
        // through core (net and script both link core) instead of duplicated per caller.
        qCWarning(logCore) << "replaceHeaderValueUp: rejecting empty name/value";
        return false;
    }
    const auto match = findHeaderMatch(QByteArrayView(headers), upName);
    if (!match) {
        return false;
    }
    headers.replace(match->valueStart, match->lineEnd - match->valueStart, value.toByteArray());
    return true;
}

void deleteHeaderValueUp(QByteArray& headers, QByteArrayView upName) {
    if (headers.isEmpty() || upName.isEmpty()) {
        return;
    }
    const auto match = findHeaderMatch(QByteArrayView(headers), upName);
    if (!match) {
        return;
    }
    headers.remove(match->lineStart, match->terminatorEnd - match->lineStart);
}

QByteArray scriptGetHeaderValue(QByteArrayView headers, QByteArrayView name) {
    return getHeaderValueUp(headers, upperAscii7(name));
}

QByteArray scriptSetHeaderValue(QByteArrayView headers, QByteArrayView name, QByteArrayView value) {
    QByteArray copy = headers.toByteArray();
    if (replaceHeaderValueUp(copy, upperAscii7(name), value)) {
        return copy;
    }
    return name.toByteArray() + ": " + value.toByteArray() + "\r\n" + headers.toByteArray();
}

QByteArray scriptDeleteHeaderEntry(QByteArrayView headers, QByteArrayView name) {
    QByteArray copy = headers.toByteArray();
    deleteHeaderValueUp(copy, upperAscii7(name));
    return copy;
}

} // namespace cg::core
