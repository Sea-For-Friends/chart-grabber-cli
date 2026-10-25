// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Basic/u_NetworkStrFunc.pas:69-403 (the raw-blob helpers); the script-facing wrappers
// port Src/PascalScript/u_PascalScriptUtils.pas:88-119. T-0028: moved here from
// src/cg/net/http_headers.{h,cpp} (T-0012B) so both `cg::net` and `cg::script` -- which the
// module DAG forbids from linking each other -- can share the single implementation via `core`.
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

namespace cg::core {

// Case-insensitive (7-bit ASCII) prefix match of `upName` (already upper-case) against `headers`
// starting at `start`. Ports IdemPChar. Bounds-checked instead of relying on the always-null-
// terminated Delphi RawByteString: a mismatch is guaranteed the moment `upName` runs past the end
// of `headers`, so the observable behavior is identical. Exposed (not file-local) because
// `cg::net`'s status-line check is also a prefix match against a fixed literal.
[[nodiscard]] bool idemPChar(QByteArrayView headers, qsizetype start, QByteArrayView upName);

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

// The three script-facing wrappers behind GetHeaderValue/SetHeaderValue/DeleteHeaderEntry
// (u_PascalScriptUtils.pas:88-119), used by `cg::script`'s GetHeaderValue/SetHeaderValue/
// DeleteHeaderEntry builtins (F29-F31).

// = getHeaderValueUp(headers, name.toUpper()).
[[nodiscard]] QByteArray scriptGetHeaderValue(QByteArrayView headers, QByteArrayView name);

// Tries replaceHeaderValueUp; on failure (including an empty value, which always takes this
// branch) PREPENDS "name: value\r\n" to the original blob, using the caller's original casing of
// `name`. Never appends.
[[nodiscard]] QByteArray scriptSetHeaderValue(QByteArrayView headers, QByteArrayView name,
                                              QByteArrayView value);

// Copies, deletes, and returns the copy.
[[nodiscard]] QByteArray scriptDeleteHeaderEntry(QByteArrayView headers, QByteArrayView name);

} // namespace cg::core
