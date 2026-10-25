// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/codecs/gm2_tile_name_codec.h"

#include "cg/core/platform/qt_compat.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace cg::storage {

// FEAT-STO-006. u_TileFileNameGM2.pas:54,60-66 GetTileFileName. (!) Y first, X last.
QString Gm2TileNameCodec::tilePath(geo::PointI xy, quint8 zoom) const {
    return QStringLiteral("z%1/%2_%3").arg(int(zoom)).arg(xy.y()).arg(xy.x());
}

// u_TileFileNameGM2.pas:35,70-91 GetTilePoint. c_GM2_Expr with '\\' -> '/'. See gm1's parse()
// comment for the overflow-rejection rationale (shared gap, ported independently per codec).
bool Gm2TileNameCodec::parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const {
    static const QRegularExpression re(QStringLiteral(R"(^(.+/)?[zZ](\d\d?)/(\d+)_(\d+)(\..+)?$)"));
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
