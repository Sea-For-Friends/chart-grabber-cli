// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_post_download_crop_config.h"

#include <utility>

namespace cg::maptype {

TilePostDownloadCropConfigStatic::TilePostDownloadCropConfigStatic(const geo::RectI& cropRect,
                                                                   geo::PointI cutCount, geo::PointI cutSize,
                                                                   geo::PointI cutTile, QString cutToSkip) {
    // crop params
    if (cropRect.left >= 0 && cropRect.top >= 0 && cropRect.right > cropRect.left &&
        cropRect.bottom > cropRect.top && cropRect.right < 10000 && cropRect.bottom < 10000) {
        isCropOnDownload_ = true;
        cropRect_ = cropRect;
    } else {
        isCropOnDownload_ = false;
        cropRect_ = geo::RectI{0, 0, 0, 0};
    }
    // cut params (by size or by count)
    const bool byCount = cutCount.x() > 0 && cutCount.y() > 0 &&
                         ((cutTile.x() >= 0 && cutTile.x() < cutCount.x()) || cutTile.x() < 0) &&
                         ((cutTile.y() >= 0 && cutTile.y() < cutCount.y()) || cutTile.y() < 0);
    const bool bySize = cutSize.x() > 0 || cutSize.y() > 0;
    const bool hasSkip = !cutToSkip.isEmpty();
    const bool hasTile = cutTile.x() != 0 || cutTile.y() != 0;
    if (byCount || bySize || hasSkip || hasTile) {
        isCutOnDownload_ = true;
        cutCount_ = cutCount;
        cutSize_ = cutSize;
        cutTile_ = cutTile;
        cutToSkip_ = std::move(cutToSkip);
    } else {
        isCutOnDownload_ = false;
        cutCount_ = geo::PointI(0, 0);
        cutSize_ = cutCount_;
        cutTile_ = cutCount_;
        cutToSkip_.clear();
    }
}

bool TilePostDownloadCropConfigStatic::cutSkipItem(geo::PointI item, geo::PointI count) const {
    if (cutToSkip_.isEmpty()) {
        return false;
    }
    // Plain substring search, as legacy System.Pos; the second pattern subtracts count.y from item.y.
    const QString first = QStringLiteral("(%1,%2)").arg(item.x()).arg(item.y());
    if (cutToSkip_.contains(first)) {
        return true;
    }
    const QString second = QStringLiteral("(%1,%2)").arg(item.x()).arg(item.y() - count.y());
    return cutToSkip_.contains(second);
}

} // namespace cg::maptype
