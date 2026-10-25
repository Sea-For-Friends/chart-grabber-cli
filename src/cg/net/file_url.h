// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_DownloaderHttpBase.pas:270-353 (ProcessFileSystemRequest) -- the URL-recognition half
// only. docs/specs/net.md:805-822, deviation [D-11]. File I/O and the ContentTypeRegistry lookup
// stay in processFileSystemRequest() itself, which T-0010B implements and which calls this
// function first, before doing either.

#include <QByteArray>

#include <optional>

namespace cg::net {

// Recognizes `file:///...` URLs (case-insensitive scheme, exactly three slashes) and strips the
// 7-byte `file://` scheme+authority, keeping the 3rd '/' as the path's leading separator (RFC
// 8089: file:// + empty host + "/" path): `file:///tmp/example/a.png` -> `/tmp/example/a.png`. Legacy
// instead strips all 8 bytes of `file:///` and replaces every remaining '/' with '\', which is
// how it reconstructs `C:\...` and `\\server\...` forms; there is no such reinstatement needed on
// Linux, so the 8th byte is kept here instead of discarded. Returns
// std::nullopt for:
//   - anything shorter than 4 bytes, or whose 2nd byte is 't'/'T' (fast-rejects http/https/ftp);
//   - `\\server\share\...` (UNC) -- accepted verbatim in legacy, rejected here: no UNC on Linux;
//   - `C:\dir\...` (drive letter) -- accepted verbatim in legacy, rejected here;
//   - a `file:///` prefix shorter than or equal to 10 bytes total, or not matching case-
//     insensitively;
//   - anything else.
// No percent-decoding, matching legacy.
[[nodiscard]] std::optional<QByteArray> filePathFromUrl(const QByteArray& url);

} // namespace cg::net
