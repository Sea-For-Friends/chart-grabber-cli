// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/codecs/es_tile_name_codec.h"

#include "cg/core/platform/qt_compat.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace cg::storage {

namespace {

// u_TileFileNameES.pas:47-72 FullInt: zero-pad to width `w`, then RightStr -- i.e. truncate from
// the LEFT down to `w` characters when the number is wider than the pad (zoom 6, X = 12345 ->
// "45"). `widthZoom` selects the width and is not necessarily the tile's own zoom: callers pass
// AZoom-5 for the packed subfolder digits and the fixed value 9 for the top-tier shard digits.
QString fullInt(qint64 value, int widthZoom) {
    QString s = QString::number(value);
    if (widthZoom < 4) {
        return s; // width 1: no padding, no truncation
    }
    const int w = widthZoom < 7    ? 2
                  : widthZoom < 10 ? 3
                  : widthZoom < 14 ? 4
                  : widthZoom < 17 ? 5
                  : widthZoom < 20 ? 6
                                   : 7;
    const QString padded = QString(w - 1, QLatin1Char('0')) + s;
    return padded.right(w);
}

// u_TileFileNameES.pas:76-78: zoomStr = Z+1, zero-padded to 2 digits while Z+1 < 10 -- same rule
// as GMV's, ported independently here per its own legacy unit.
QString esZoomStr(quint8 zoom) {
    const int n = int(zoom) + 1;
    return zoom >= 9 ? QString::number(n) : QStringLiteral("0") + QString::number(n);
}

} // namespace

// FEAT-STO-004. u_TileFileNameES.pas:74-91 GetTileFileName.
//
// Deviation: legacy's GetTileFileName opens with an unconditional `inherited;` call, and the
// ancestor TTileFileNameBase.GetTileFileName is `virtual; abstract;` with no body -- in Delphi,
// calling `inherited` on a method the ancestor never implements raises EAbstractError, which
// would make this call always throw before computing anything. Treated as dead/vestigial code
// (the format could not function at all otherwise, and neither the spec's pseudocode nor the
// survey mention it); not reproduced. Flagged in the ticket's Open questions.
QString EsTileNameCodec::tilePath(geo::PointI xy, quint8 zoom) const {
    const QString zoomStr = esZoomStr(zoom);
    const QString fileName =
        zoomStr + QLatin1Char('-') + fullInt(xy.x(), zoom) + QLatin1Char('-') + fullInt(xy.y(), zoom);
    QString directory;
    if (zoom < 6) {
        directory = zoomStr + QLatin1Char('/');
    } else if (zoom < 10) {
        directory = zoomStr + QLatin1Char('/') + QChar(char16_t(60 + zoom)) + fullInt(xy.x() >> 5, zoom - 5) +
                    fullInt(xy.y() >> 5, zoom - 5) + QLatin1Char('/');
    } else {
        directory = QStringLiteral("10-") + fullInt(xy.x() >> (zoom - 9), 9) + QLatin1Char('-') +
                    fullInt(xy.y() >> (zoom - 9), 9) + QLatin1Char('/') + zoomStr + QLatin1Char('/') +
                    QChar(char16_t(60 + zoom)) + fullInt(xy.x() >> 5, zoom - 5) +
                    fullInt(xy.y() >> 5, zoom - 5) + QLatin1Char('/');
    }
    return directory + fileName;
}

// u_TileFileNameES.pas:41,93-113 GetTilePoint. c_ES_Expr with '\\' -> '/'. Only the file name is
// read; the directory structure above is write-only for parsing.
bool EsTileNameCodec::parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const {
    static const QRegularExpression re(QStringLiteral(R"(^(.+/)?(\d+)-(\d+)-(\d+)(\..+)?$)"));
    const QRegularExpressionMatch m = core::platform::matchView(re, relativePath);
    if (!m.hasMatch()) {
        return false;
    }
    zoom = static_cast<quint8>(m.captured(2).toInt() - 1);
    xy.setX(m.captured(3).toInt());
    xy.setY(m.captured(4).toInt());
    return true;
}

} // namespace cg::storage
