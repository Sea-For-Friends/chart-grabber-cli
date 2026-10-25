// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: the date half of Src/TileStorage/FileSystem/u_FileSystemTools.pas (GetFileDate /
// SetFileDate) as re-derived for POSIX by docs/specs/storage.md §2.13.3 (FEAT-STO-013).
//
// The ONLY place `utimensat`/`stat` are called for tile mtimes -- coding standard: platform code
// lives under core/platform/, never scattered with `#ifdef _WIN32` elsewhere. Callers convert
// between these raw POSIX seconds and a storage::TileDate themselves (storage::toPosixSeconds /
// storage::fromPosixSeconds, already landed by T-0021A) -- this header knows nothing about
// TileDate so core stays independent of the storage module.

#include "cg/core/result.h"

#include <QString>
#include <QtGlobal>

namespace cg::core::platform {

// Sets the file's mtime to `secs` (POSIX seconds since the epoch); atime is left untouched
// (`UTIME_OMIT`). Called AFTER the data is written and the file closed, never on an open handle
// -- unlike legacy, which sets the date on the handle before writing (`FileSetDate` before the
// write, safe only because `SetFileTime` wins over Windows' implicit update; on Linux the
// subsequent write would clobber it). No 2-second DOS-grid quantisation (deviation D-5): whole
// POSIX seconds, strictly more precise than legacy.
[[nodiscard]] Result<void> setFileMTime(const QString& path, qint64 secs);

// Reads the file's mtime as POSIX seconds since the epoch, truncated -- never rounded -- to whole
// seconds (a rounded-up mtime would make a freshly written tile look newer than `Now`). Uses
// `::stat` directly, not `QFileInfo::lastModified()`: the latter costs a `QDateTime` construction
// per tile in the `tileRectInfo` hot loop (T-0021F2). Symmetric with `setFileMTime` (deviation
// D-3): the port does not reproduce legacy's "read as UTC, write as local" bug, and there is no
// POSIX analogue of the `ftCreationTime` fallback (mtime-only, already an approved ADR-0005 row).
[[nodiscard]] Result<qint64> fileMTime(const QString& path);

} // namespace cg::core::platform
