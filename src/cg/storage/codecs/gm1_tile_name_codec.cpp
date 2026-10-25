// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/codecs/gm1_tile_name_codec.h"

#include "cg/core/platform/qt_compat.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace cg::storage {

// FEAT-STO-005. u_TileFileNameGM1.pas:54,60-68 GetTileFileName. (!) Y first, X last.
QString Gm1TileNameCodec::tilePath(geo::PointI xy, quint8 zoom) const {
    return QStringLiteral("z%1/%2/%3").arg(int(zoom)).arg(xy.y()).arg(xy.x());
}

// u_TileFileNameGM1.pas:35,70-91 GetTilePoint. c_GM1_Expr with '\\' -> '/'.
//
// Deviation (gap flagged for T-0021C): legacy's StrToIntA raises EConvertError on a numeric
// capture that overflows a 32-bit Integer; Qt's QString::toInt() with no `ok` argument instead
// silently returns 0. Every capture used as a coordinate here is converted with the checked
// overload and rejected (parse() returns false) on overflow, matching legacy's "this tile name
// cannot be used" outcome without adopting exception-based control flow the ITileNameCodec
// interface does not have.
bool Gm1TileNameCodec::parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const {
    static const QRegularExpression re(QStringLiteral(R"(^(.+/)?[zZ](\d\d?)/(\d+)/(\d+)(\..+)?$)"));
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
