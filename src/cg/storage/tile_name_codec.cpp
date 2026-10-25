// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/tile_name_codec.h"

namespace cg::storage {

QString ITileNameCodec::addExt(const QString& path, QStringView ext) const {
    // u_TileFileNameBase.pas:60-63 AddExt -> SysUtils.ChangeFileExt.
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    const qsizetype dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot > slash) {
        return path.left(dot) + ext.toString();
    }
    return path + ext.toString();
}

} // namespace cg::storage
