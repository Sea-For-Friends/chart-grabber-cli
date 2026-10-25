// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Basic/u_NetworkStrFunc.pas:69-403 (the raw-blob helpers). The header-text functions
// (ports of Src/PascalScript/u_PascalScriptUtils.pas:88-119) live in cg::core
// (src/cg/core/header_text.h); net keeps thin forwards to them plus its own helpers below.
//
// All operations here work on a raw CRLF header blob that MAY start with an HTTP status line and
// MAY end with a blank line. A "line" ends at the first byte < ' ' (any control char, not just
// CR/LF); the whole run of control chars after a line is skipped before scanning the next one.
// Only the first matching line is ever acted on. `upName` must already be UPPER-CASE ASCII;
// matching is 7-bit-ASCII case-insensitive and a PREFIX match with no delimiter required, so
// "CONTENT-TYPE" also matches a "Content-Type-Options: ..." line -- legacy behavior, not a bug,
// and the 48-script ZMP corpus depends on it (docs/specs/net.md §2.11).

#include <QByteArray>
#include <QByteArrayView>

namespace cg::net {

// T-0028: getHeaderValueUp/replaceHeaderValueUp/deleteHeaderValueUp are thin forwards to
// cg::core::header_text (moved there so cg::script, which the module DAG forbids from linking
// net, can share the one implementation via core). Kept here, under these names, because net's
// own downloader/proxy code (curl_downloader.cpp, proxy_resolver.cpp) calls them unqualified.

// Returns "" both for "absent" and for "present but empty" -- indistinguishable, and the
// Content-Length gate (docs/specs/net.md §2.12.1) relies on that. Trailing whitespace is not
// trimmed; leading whitespace/colons are skipped.
[[nodiscard]] QByteArray getHeaderValueUp(QByteArrayView headers, QByteArrayView upName);

// Replaces the value text in place, preserving the name, separator and the line's CRLF. Rejects
// an empty `value` (legacy asserts; the port logs and returns false). Returns false, headers
// untouched, when there is no match.
bool replaceHeaderValueUp(QByteArray& headers, QByteArrayView upName, QByteArrayView value);

// Deletes name + separator + value + the entire trailing run of control characters in one
// operation (so a CRLFCRLF run at the end is swallowed whole). No-op when there is no match.
void deleteHeaderValueUp(QByteArray& headers, QByteArrayView upName);

// Never checks for an existing header of that name -- can create duplicates. Splices the new
// line before a trailing control-char run so a trailing blank line stays last; appends its own
// CRLF pair when the blob has no trailing terminator at all; prepends with no CRLF when the blob
// is entirely control characters.
void addHeaderValue(QByteArray& headers, QByteArrayView name, QByteArrayView value);

// Ports GetResponseCode. Requires the "HTTP/" prefix, then takes the digits between the first and
// second space (or end of blob, whichever comes first). 0 on any failure.
[[nodiscard]] quint32 parseStatusLine(QByteArrayView headers);

// Keeps 0-9 A-Z a-z _ * - ., maps space to '+', everything else to %XX with upper-case hex. Used
// for CURLOPT_PROXYUSERPWD.
[[nodiscard]] QByteArray urlEncode(QByteArrayView s);

// Ports the UrlUnescape behavior; QByteArray::fromPercentEncoding is equivalent for the inputs
// that reach it.
[[nodiscard]] QByteArray urlDecode(QByteArrayView s);

} // namespace cg::net
