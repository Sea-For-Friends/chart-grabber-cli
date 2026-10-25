// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/tile_rect_info.h"

namespace cg::storage {

TileRectInfo::TileRectInfo(geo::RectI rect, quint8 zoom, std::vector<TileRectInfoEntry> items)
    : rect_(rect), zoom_(zoom), items_(std::move(items)) {
    Q_ASSERT(items_.size() >=
             static_cast<std::size_t>(rect_.width()) * static_cast<std::size_t>(rect_.height()));
}

qint64 TileRectInfo::indexOf(geo::PointI t) const noexcept {
    if (t.x() < rect_.left || t.x() >= rect_.right) {
        return -1;
    }
    if (t.y() < rect_.top || t.y() >= rect_.bottom) {
        return -1;
    }
    return static_cast<qint64>(t.x() - rect_.left) +
           static_cast<qint64>(t.y() - rect_.top) * static_cast<qint64>(rect_.width());
}

const TileRectInfoEntry* TileRectInfo::at(geo::PointI t) const noexcept {
    const qint64 idx = indexOf(t);
    if (idx < 0 || static_cast<std::size_t>(idx) >= items_.size()) {
        return nullptr;
    }
    return &items_[static_cast<std::size_t>(idx)];
}

} // namespace cg::storage
