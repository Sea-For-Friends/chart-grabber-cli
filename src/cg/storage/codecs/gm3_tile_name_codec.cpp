// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/codecs/gm3_tile_name_codec.h"

#include "cg/core/platform/qt_compat.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace cg::storage {

// FEAT-STO-007. u_TileFileNameGM3.pas:54,60-66 GetTileFileName. No leading 'z' (unlike GM1/GM2).
// (!) Y first, X last.
QString Gm3TileNameCodec::tilePath(geo::PointI xy, quint8 zoom) const {
    return QStringLiteral("%1/%2/%3").arg(int(zoom)).arg(xy.y()).arg(xy.x());
}

// u_TileFileNameGM3.pas:35,70-91 GetTilePoint. c_GM3_Expr with '\\' -> '/' -- identical shape to
// MOBAC/OsmAnd+/TMS's regex (§2.10.1: "regexes 6, 7, 8 and 9 are identical"), only the value
// mapping differs. See gm1's parse() comment for the overflow-rejection rationale.
bool Gm3TileNameCodec::parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const {
    static const QRegularExpression re(QStringLiteral(R"(^(.+/)?(\d\d?)/(\d+)/(\d+)(\..+)?$)"));
    const QRegularExpressionMatch m = core::platform::matchView(re, relativePath);
    if (!m.hasMatch()) {
        return false;
    }
    bool okY = false;
    bool okX = false;
    const int y = m.captured(3).toInt(&okY); // (!) Y - first, X - last
    const int x = m.captured(4).toInt(&okX);
    if (!okY || !okX) {
        return false;
    }
    zoom = static_cast<quint8>(m.captured(2).toInt());
    xy.setX(x);
    xy.setY(y);
    return true;
}

} // namespace cg::storage
