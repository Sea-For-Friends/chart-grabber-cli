// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/codecs/region_tile_name_codec.h"

#include "cg/core/platform/qt_compat.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace cg::storage {

// u_TileFileNameSQLite.pas:44-62 / u_TileFileNameBerkeleyDB.pas:44-62 GetTileFileName
// (byte-identical). Directory shards by 1024 tiles; the file name is the region's top-left tile
// coordinate, each shifted by 8 (a 256x256 block).
QString RegionTileNameCodec::tilePath(geo::PointI xy, quint8 zoom) const {
    return QStringLiteral("z%1/%2/%3/%4.%5")
        .arg(int(zoom) + 1)
        .arg(xy.x() >> 10)
        .arg(xy.y() >> 10)
        .arg(xy.x() >> 8)
        .arg(xy.y() >> 8);
}

// GetTilePoint (byte-identical between the two units). cSQLiteRegExpr/cBerkeleyDBRegExpr with
// '\\' -> '/'. The two directory-shard `\d+` groups are matched but not captured -- they carry no
// information parse() needs, exactly like ES's write-only directory structure. See gm1's parse()
// comment for the overflow-rejection rationale (StrToIntA vs QString::toInt()).
bool RegionTileNameCodec::parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const {
    static const QRegularExpression re(QStringLiteral(R"(^(.+/)?[zZ](\d\d?)/\d+/\d+/(\d+)\.(\d+)(\..+)?$)"));
    const QRegularExpressionMatch m = core::platform::matchView(re, relativePath);
    if (!m.hasMatch()) {
        return false;
    }
    bool okX = false;
    bool okY = false;
    const int x = m.captured(3).toInt(&okX);
    const int y = m.captured(4).toInt(&okY);
    if (!okX || !okY) {
        return false;
    }
    zoom = static_cast<quint8>(m.captured(2).toInt() - 1);
    xy.setX(x << 8);
    xy.setY(y << 8);
    return true;
}

} // namespace cg::storage
