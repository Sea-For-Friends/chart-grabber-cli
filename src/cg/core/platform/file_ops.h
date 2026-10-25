// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing (T-0335, Windows port). Two rename semantics, named for what the caller needs:
// POSIX rename(2) replaces an existing target; the Windows C runtime rename and QFile::rename fail when the
// target exists. A caller must say which behaviour it wants.

#include <QByteArray>
#include <QString>

#include <system_error>

namespace cg::core::platform {

// Moves `from` to `to`; an existing FILE at `to` is replaced (POSIX rename(2); Windows MoveFileExW with
// MOVEFILE_REPLACE_EXISTING, which is not atomic across a crash). The error is empty on success.
[[nodiscard]] std::error_code replaceFile(const QString& from, const QString& to);

// Moves `from` to `to`, a file or a directory; fails (false, errno set) when `to` exists on Windows. On POSIX
// it is std::rename, which would replace a file, so callers check that `to` is absent first.
[[nodiscard]] bool renamePath(const QString& from, const QString& to);

// The path of a `file:///...` URL after "file://" has been cut off (net::filePathFromUrl): on Windows "/C:/x"
// becomes "C:/x" (a drive letter, then end or '/'); everything else, and every path on POSIX, is returned as
// it is.
[[nodiscard]] QByteArray nativeFilePathFromUrlPath(const QByteArray& path);

} // namespace cg::core::platform
