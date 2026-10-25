// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: ITileRectInfo in i_TileInfoBasic.pas, u_TileRectInfoShort.pas
//
// A bulk, data-less snapshot of one rectangle at one zoom, used by the "fill map" overlay and by
// the region downloader to decide what to fetch.
//
// T-0021A review (finding D-2) + Tech Lead ruling 2026-09-19 (T-0021A rework): legacy's
// `ITileRectInfo` (Src/i_TileInfoBasic.pas:53-62) exposes only `TileRect`, `Zoom` and `GetEnum` --
// no version/content-type accessors -- so this port stores only what that interface needs.
// `GetEnum` (-> `enumerate()`) needs the concrete `geo::ITileIterator`, which is T-0018 (geo slice
// C) and does not exist in the tree yet; nothing speculative ships, so `enumerate()` and the
// version_/contentType_ fields that existed only to feed it are removed until T-0018 lands. See
// the ticket's Open questions.

#include "cg/geo/geo_types.h"
#include "cg/storage/tile_info.h"

#include <QtGlobal>

#include <memory>
#include <vector>

namespace cg::storage {

struct TileRectInfoEntry {
    TileDate loadDate;
    quint32 size = 0;
    TileInfoType type = TileInfoType::Unknown;
};

class TileRectInfo final {
public:
    TileRectInfo(geo::RectI rect, quint8 zoom, std::vector<TileRectInfoEntry> items);

    [[nodiscard]] const geo::RectI& rect() const noexcept { return rect_; }
    [[nodiscard]] quint8 zoom() const noexcept { return zoom_; }

    // Ports TTileRectInfoShort.TileInRectToIndex: row-major, (x - left) + (y - top) * width;
    // -1 when outside. Half-open rect (geo_types.h).
    [[nodiscard]] qint64 indexOf(geo::PointI t) const noexcept;
    [[nodiscard]] const TileRectInfoEntry* at(geo::PointI t) const noexcept;

private:
    geo::RectI rect_;
    quint8 zoom_ = 0;
    std::vector<TileRectInfoEntry> items_;
};
using TileRectInfoPtr = std::shared_ptr<const TileRectInfo>;

} // namespace cg::storage
