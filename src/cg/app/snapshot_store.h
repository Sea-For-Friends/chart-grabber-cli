// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: nothing. docs/specs/weather-sources.md section 4.2. Ports: nothing.
//
// The store file Snapshots/<GUID>.conf: the latest snapshot that download / run took for a source.

#include "cg/app/snapshot.h"
#include "cg/core/result.h"

#include <QDateTime>
#include <QString>

#include <optional>

namespace cg::app {

// mkpath(dir), then <dir>/<guid>.conf through QSaveFile (UTF-8, LF). IoError on failure; the text names the
// file.
[[nodiscard]] core::Result<void> saveSnapshot(const QString& dir, const QString& guid, const Snapshot& s,
                                              const QDateTime& resolvedUtc);

// T-0357: a <guid>.ini of an older version is renamed to <guid>.conf once (stderr note); with both, .conf
// wins. nullopt: no file, no [Snapshot] section, or an empty Version. Time / DateOnly are read back with
// snapshotFromText (an unreadable time stays invalid = unknown).
[[nodiscard]] std::optional<Snapshot> loadSnapshot(const QString& dir, const QString& guid);

} // namespace cg::app
