// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_coverage_info.h"

#include "cg/core/delphi_text.h"

#include <QStringList>

namespace cg::maptype {

MapCoverageInfo::MapCoverageInfo(const QString& center) {
    if (!parseCenterValue(center)) {
        centerPos_ = geo::emptyDoublePoint();
        centerZoom_ = 0;
    }
}

bool MapCoverageInfo::parseCenterValue(const QString& value) {
    if (value.isEmpty()) {
        return false;
    }
    const QStringList items = value.split(u',', Qt::KeepEmptyParts); // Delphi SplitString
    if (items.size() != 3) {
        return false;
    }
    double x = 0.0;
    double y = 0.0;
    int zoom = 0;
    if (!core::delphiTryStrToFloat(core::delphiTrim(items[0]), x) ||
        !core::delphiTryStrToFloat(core::delphiTrim(items[1]), y) ||
        !core::delphiTryStrToInt(core::delphiTrim(items[2]), zoom)) {
        return false;
    }
    centerPos_ = geo::DoublePoint(x, y);
    centerZoom_ = zoom;
    return true;
}

} // namespace cg::maptype
