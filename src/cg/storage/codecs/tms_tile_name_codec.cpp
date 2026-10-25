// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/codecs/tms_tile_name_codec.h"

#include "cg/core/platform/qt_compat.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace cg::storage {

namespace {
// u_TileFileNameTMS.pas:57-62 GetFlippedY: TMS numbers tiles from the bottom of the pyramid.
int flippedY(quint8 zoom, int y) {
    return (1 << int(zoom)) - y - 1;
}
} // namespace

// FEAT-STO-010. u_TileFileNameTMS.pas:65-75 GetTileFileName. (!) X first, Y last (flipped).
QString TmsTileNameCodec::tilePath(geo::PointI xy, quint8 zoom) const {
    return QStringLiteral("%1/%2/%3").arg(int(zoom)).arg(xy.x()).arg(flippedY(zoom, xy.y()));
}

// u_TileFileNameTMS.pas:35,77-98 GetTilePoint. c_TMS_Expr with '\\' -> '/' -- identical shape to
// GM3/MOBAC/OsmAnd+'s regex; zoom is read first so GetFlippedY can use it, exactly as legacy
// orders the assignments. See gm1's parse() comment for the overflow-rejection rationale.
bool TmsTileNameCodec::parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const {
    static const QRegularExpression re(QStringLiteral(R"(^(.+/)?(\d\d?)/(\d+)/(\d+)(\..+)?$)"));
    const QRegularExpressionMatch m = core::platform::matchView(re, relativePath);
    if (!m.hasMatch()) {
        return false;
    }
    bool okX = false;
    bool okY = false;
    const int x = m.captured(3).toInt(&okX); // (!) X - first, Y - last
    const int yFlipped = m.captured(4).toInt(&okY);
    if (!okX || !okY) {
        return false;
    }
    zoom = static_cast<quint8>(m.captured(2).toInt());
    xy.setX(x);
    xy.setY(flippedY(zoom, yFlipped));
    return true;
}

} // namespace cg::storage
