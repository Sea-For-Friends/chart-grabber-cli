// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Network/i_TilePostDownloadCropConfig.pas, Network/u_TilePostDownloadCropConfigStatic.pas
//
// What to do with a downloaded image before it is saved: crop to a rectangle and/or cut into
// several tiles (spec maptype.md §2.6.6). Immutable; the constructor validates the ZMP values.

#include "cg/geo/geo_types.h"

#include <QString>

#include <memory>

namespace cg::maptype {

// FEAT-NET-033
class TilePostDownloadCropConfigStatic final {
public:
    // Crop is kept iff left,top >= 0, right > left, bottom > top and right,bottom < 10000, else
    // the rect is zeroed. Cut is kept iff one of its four triggers holds (count/tile pair valid,
    // a size > 0, a non-empty skip list, a tile offset != 0), else every cut field is zeroed.
    TilePostDownloadCropConfigStatic(const geo::RectI& cropRect, geo::PointI cutCount, geo::PointI cutSize,
                                     geo::PointI cutTile, QString cutToSkip);

    [[nodiscard]] bool isCropOnDownload() const { return isCropOnDownload_; }
    [[nodiscard]] geo::RectI cropRect() const { return cropRect_; }

    // Cut the downloaded image into multiple images and save all (nonexistent) items; crop first.
    [[nodiscard]] bool isCutOnDownload() const { return isCutOnDownload_; }
    [[nodiscard]] geo::PointI cutCount() const { return cutCount_; } // count of parts (X and Y)
    [[nodiscard]] geo::PointI cutSize() const { return cutSize_; }   // or size of every part
    [[nodiscard]] geo::PointI cutTile() const { return cutTile_; }   // position of the requested tile

    // Skip list (for watermarks): true when "(x,y)" or "(x,y-count.y)" occurs in the CutToSkip text.
    [[nodiscard]] bool cutSkipItem(geo::PointI item, geo::PointI count) const;

private:
    bool isCropOnDownload_;
    geo::RectI cropRect_;
    bool isCutOnDownload_;
    geo::PointI cutCount_;
    geo::PointI cutSize_;
    geo::PointI cutTile_;
    QString cutToSkip_;
};
using TilePostDownloadCropConfigStaticPtr = std::shared_ptr<const TilePostDownloadCropConfigStatic>;

} // namespace cg::maptype
