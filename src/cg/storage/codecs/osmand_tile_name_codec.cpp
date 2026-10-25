// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/codecs/osmand_tile_name_codec.h"

#include "cg/core/platform/qt_compat.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace cg::storage {

// FEAT-STO-009. u_TileFileNameOsmAnd.pas:54,60-66 GetTileFileName. (!) X first, Y last.
QString OsmandTileNameCodec::tilePath(geo::PointI xy, quint8 zoom) const {
    return QStringLiteral("%1/%2/%3").arg(int(zoom)).arg(xy.x()).arg(xy.y());
}

// u_TileFileNameOsmAnd.pas:35,70-91 GetTilePoint. c_OsmAnd_Expr with '\\' -> '/' -- identical
// shape to GM3/MOBAC/TMS's regex. See gm1's parse() comment for the overflow-rejection rationale.
bool OsmandTileNameCodec::parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const {
    static const QRegularExpression re(QStringLiteral(R"(^(.+/)?(\d\d?)/(\d+)/(\d+)(\..+)?$)"));
    const QRegularExpressionMatch m = core::platform::matchView(re, relativePath);
    if (!m.hasMatch()) {
        return false;
    }
    bool okX = false;
    bool okY = false;
    const int x = m.captured(3).toInt(&okX); // (!) X - first, Y - last
    const int y = m.captured(4).toInt(&okY);
    if (!okX || !okY) {
        return false;
    }
    zoom = static_cast<quint8>(m.captured(2).toInt());
    xy.setX(x);
    xy.setY(y);
    return true;
}

// u_TileFileNameOsmAnd.pas:98-110 AddExt. Case-insensitive compare ports SameFileName.
QString OsmandTileNameCodec::addExt(const QString& path, QStringView ext) const {
    const QString e = ext.compare(u".tne", Qt::CaseInsensitive) == 0
                          ? QStringLiteral(".tne")
                          : ext.toString() + QStringLiteral(".tile");
    return ITileNameCodec::addExt(path, e);
}

} // namespace cg::storage
