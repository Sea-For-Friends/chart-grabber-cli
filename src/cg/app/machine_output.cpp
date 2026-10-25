// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). See machine_output.h.
#include "cg/app/machine_output.h"

#include "cg/app/fill_report.h"
#include "cg/app/probe.h"
#include "cg/app/tile_estimate.h"
#include "cg/core/content_type.h"
#include "cg/geo/projection.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/zmp_info.h"
#include "cg/storage/simple_tile_storage_config.h"

namespace cg::app {

namespace {

QString num(qint64 value) {
    return QString::number(value);
}

QString share(double value) {
    return QString::number(value, 'f', 4);
}

QString flag(bool value) {
    return value ? QStringLiteral("1") : QStringLiteral("0");
}

} // namespace

QString tsvLine(QStringView type, const QStringList& fields) {
    QString line = type.toString();
    for (QString field : fields) {
        field.replace(u'\t', u' ').replace(u'\r', u' ').replace(u'\n', u' ');
        line += u'\t' + field;
    }
    return line;
}

QString sourceRecord(const maptype::IMapType& map, const QString& verdict, QStringView origin,
                     QStringView refresh, const QString& license, std::optional<quint8> maxZoom,
                     bool navigationWarning) {
    const core::ContentTypeInfo* type = map.contentType();
    return tsvLine(u"source",
                   {guidText(map), map.zmp()->isLayer() ? QStringLiteral("layer") : QStringLiteral("map"),
                    map.guiConfig()->name()->value(),
                    type != nullptr ? QString::fromLatin1(type->type) : QString(),
                    num(map.projectionSet()->zoom(0)->projectionType()->projectionEpsg()),
                    num(static_cast<int>(map.storageConfig()->cacheType())), verdict, map.zmp()->fileName(),
                    map.guiConfig()->parentSubMenu()->value(), origin.toString(), refresh.toString(), license,
                    maxZoom ? num(*maxZoom) : QString(), flag(navigationWarning)});
}

QStringList estimateRecords(const QString& guid, const std::vector<ZoomEstimate>& estimates) {
    QStringList lines;
    for (const ZoomEstimate& e : estimates) {
        lines.push_back(tsvLine(u"estimate", {guid, num(e.zoom), num(e.tiles), num(e.bboxTiles),
                                              share(e.sharePresent), share(e.shareMarkedMissing),
                                              num(e.bytesToDownload), flag(e.assumed), num(e.bytesOnDisk)}));
    }
    return lines;
}

QStringList fillRecords(const QString& guid, const std::vector<ZoomFill>& fills) {
    QStringList lines;
    for (const ZoomFill& f : fills) {
        lines.push_back(tsvLine(u"fill", {guid, num(f.zoom), num(f.tiles), num(f.present),
                                          num(f.markedMissing), num(f.missing), num(f.bytes)}));
    }
    return lines;
}

QString sessionRecord(const QString& guid, const QDateTime& time, const QString& file) {
    return tsvLine(u"session", {guid, time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), file});
}

QString writtenRecord(const QString& guid, const QString& path, qint64 tiles, qint64 bytes,
                      const QString& snapshotIso) {
    return tsvLine(u"written", {guid, path, num(tiles), num(bytes), snapshotIso});
}

QString snapshotRecord(const QString& guid, const Snapshot& s) {
    return tsvLine(u"snapshot", {guid, snapshotTimeIso(s), s.version});
}

QString phaseRecord(QStringView name, const QString& guid) {
    return tsvLine(u"phase", {name.toString(), guid});
}

QString downloadProgressRecord(const QString& guid, int zoom, qint64 processed, qint64 total,
                               qint64 downloaded, quint64 downloadedBytes, qint64 elapsedSec) {
    return tsvLine(u"progress", {QStringLiteral("download"), guid, num(zoom), num(processed), num(total),
                                 num(downloaded), QString::number(downloadedBytes), num(elapsedSec)});
}

QString exportProgressRecord(const QString& guid, int permille) {
    return tsvLine(u"progress", {QStringLiteral("export"), guid, num(permille)});
}

} // namespace cg::app
