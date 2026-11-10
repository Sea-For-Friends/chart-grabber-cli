// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: nothing; the record layouts are a contract with the programs that
// read them. Ports: nothing.
//
// The tab-separated records of `--format=tsv`. Every function returns whole lines without the LF.

#include "cg/app/snapshot.h"
#include "cg/maptype/map_type.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QtGlobal>

#include <optional>
#include <vector>

namespace cg::app {

struct ZoomEstimate;
struct ZoomFill;

// type, then the fields joined by TAB; a TAB, CR or LF inside a field becomes one space.
[[nodiscard]] QString tsvLine(QStringView type, const QStringList& fields);

// refresh: field 11 (`snapshot` / `static`), license: field 12, maxZoom: field 13 (empty when absent),
// navigationWarning: field 14.
[[nodiscard]] QString sourceRecord(const maptype::IMapType& map, const QString& verdict, QStringView origin,
                                   QStringView refresh, const QString& license, std::optional<quint8> maxZoom,
                                   bool navigationWarning);
[[nodiscard]] QStringList estimateRecords(const QString& guid, const std::vector<ZoomEstimate>& estimates);
[[nodiscard]] QStringList fillRecords(const QString& guid, const std::vector<ZoomFill>& fills);
[[nodiscard]] QString sessionRecord(const QString& guid, const QDateTime& time, const QString& file);
// --progress=lines.
[[nodiscard]] QString phaseRecord(QStringView name, const QString& guid);
[[nodiscard]] QString downloadProgressRecord(const QString& guid, int zoom, qint64 processed, qint64 total,
                                             qint64 downloaded, quint64 downloadedBytes, qint64 elapsedSec);
[[nodiscard]] QString exportProgressRecord(const QString& guid, int permille);
// snapshotIso: field 5, the snapshot time of a snapshot source (snapshotTimeIso), else empty.
[[nodiscard]] QString writtenRecord(const QString& guid, const QString& path, qint64 tiles, qint64 bytes,
                                    const QString& snapshotIso);
// snapshot<TAB>guid<TAB>time ISO<TAB>version.
[[nodiscard]] QString snapshotRecord(const QString& guid, const Snapshot& s);

} // namespace cg::app
