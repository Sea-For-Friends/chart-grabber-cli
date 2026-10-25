// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/codecs/sas_tile_name_codec.h"

#include "cg/core/platform/qt_compat.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace cg::storage {

// FEAT-STO-002. u_TileFileNameSAS.pas:57-67 GetTileFileName.
QString SasTileNameCodec::tilePath(geo::PointI xy, quint8 zoom) const {
    return QStringLiteral("z%1/%2/x%3/%4/y%5")
        .arg(int(zoom) + 1)
        .arg(xy.x() >> 10)
        .arg(xy.x())
        .arg(xy.y() >> 10)
        .arg(xy.y());
}

// u_TileFileNameSAS.pas:35,69-89 GetTilePoint. c_SAS_Expr with '\\' rewritten to '/'.
bool SasTileNameCodec::parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const {
    static const QRegularExpression re(
        QStringLiteral(R"(^(.+/)?[zZ](\d\d?)/\d+/[xX](\d+)/\d+/[yY](\d+)(\..+)?$)"));
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
