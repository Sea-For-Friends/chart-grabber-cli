// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: the TDateTime ALoadDate parameter of i_TileStorage.ITileStorage.SaveTile, and the three
//        different on-disk encodings of it in u_FileSystemTools.pas, u_TileStorageSQLiteHandler.pas
//        and u_BerkeleyDBValue.pas.
//
// A tile's "load date" is the wall-clock moment the tile was downloaded or generated. Legacy
// carries it as a Delphi TDateTime produced by `Now` -- a *naive local* timestamp with no zone --
// and each backend serialises that naive value differently. This type keeps the naive semantics
// explicit so that no conversion is accidental.

#include <QDateTime>

#include <cstdint>

namespace cg::storage {

// A naive local wall clock. timeSpec() is always Qt::LocalTime; never call toUTC() on it except
// through the helpers below. An invalid TileDate (default-constructed) is how §2.4.3's
// "NotExists" / unset load date is represented, and callers that need "infinitely old" semantics
// (predicates.cpp's tileOlderThan) must check isValid() explicitly -- QDateTime's own ordering of
// invalid vs. valid values is not something to rely on: it depends on the Qt version (invalid
// sorts first only from Qt 6.8's three-way comparison; this project's floor is Qt 6.5, where an
// invalid QDateTime instead compares via a raw epoch-ms readout, per T-0021A review finding 3).
using TileDate = QDateTime;

[[nodiscard]] TileDate tileDateNow(); // ports SysUtils.Now

// --- Delphi TDateTime (days since 1899-12-30, fractional day) -- BerkeleyDB stores this raw.
[[nodiscard]] double toDelphiDateTime(const TileDate& d);
[[nodiscard]] TileDate fromDelphiDateTime(double v); // invalid TileDate for NaN

// --- "local wall clock re-read as UTC" -- what cache_sqlite column `d` holds. See 2.4.3.
[[nodiscard]] qint64 toLegacyUnixSeconds(const TileDate& d);
[[nodiscard]] TileDate fromLegacyUnixSeconds(qint64 s);

// --- a true POSIX instant -- what a file mtime holds. See 2.13.3.
[[nodiscard]] qint64 toPosixSeconds(const TileDate& d);
[[nodiscard]] TileDate fromPosixSeconds(qint64 s);

} // namespace cg::storage
