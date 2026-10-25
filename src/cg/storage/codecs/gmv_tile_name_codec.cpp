// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/codecs/gmv_tile_name_codec.h"

#include "cg/core/platform/qt_compat.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace cg::storage {

namespace {
// u_TileFileNameGMV.pas:57-61: zoomStr = Z+1, zero-padded to 2 digits while Z+1 < 10.
QString gmvZoomStr(quint8 zoom) {
    const int n = int(zoom) + 1;
    return zoom >= 9 ? QString::number(n) : QStringLiteral("0") + QString::number(n);
}
} // namespace

// FEAT-STO-003. u_TileFileNameGMV.pas:56-77 GetTileFileName.
QString GmvTileNameCodec::tilePath(geo::PointI xy, quint8 zoom) const {
    QString path = gmvZoomStr(zoom) + QStringLiteral("/t");
    if (zoom > 0) {
        qint32 mask = 1 << (zoom - 1);
        for (int i = 0; i < int(zoom); ++i) {
            const bool xBit = (xy.x() & mask) != 0;
            const bool yBit = (xy.y() & mask) != 0;
            path += xBit ? (yBit ? QLatin1Char('s') : QLatin1Char('r'))
                         : (yBit ? QLatin1Char('t') : QLatin1Char('q'));
            mask >>= 1;
        }
    }
    return path;
}

// u_TileFileNameGMV.pas:34,79-142 GetTilePoint + nested TSQR2XY. c_GMV_Expr with '\\' -> '/'.
bool GmvTileNameCodec::parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const {
    static const QRegularExpression re(QStringLiteral(R"(^(.+/)?([tT][tsqrTSQR]*)(\..+)?$)"));
    const QRegularExpressionMatch m = core::platform::matchView(re, relativePath);
    if (!m.hasMatch()) {
        return false;
    }
    const QString tsqr = m.captured(2);
    // TSQR2XY: skips the leading 't'/'T' and folds the rest MSB-first. The regex already
    // constrains every character to the t/s/q/r charset, so no separate validation is needed.
    int x = 0;
    int y = 0;
    for (qsizetype i = 1; i < tsqr.size(); ++i) {
        x *= 2;
        y *= 2;
        const QChar c = tsqr.at(i);
        if (c == QLatin1Char('R') || c == QLatin1Char('S') || c == QLatin1Char('r') ||
            c == QLatin1Char('s')) {
            ++x;
        }
        if (c == QLatin1Char('T') || c == QLatin1Char('S') || c == QLatin1Char('t') ||
            c == QLatin1Char('s')) {
            ++y;
        }
    }
    zoom = static_cast<quint8>(tsqr.size() - 1); // zoom derived from string length, not the directory
    xy.setX(x);
    xy.setY(y);
    return true;
}

} // namespace cg::storage
