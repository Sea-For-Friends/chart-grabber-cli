// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileFileNameGenerator.pas, i_TileFileNameParser.pas, u_TileFileNameBase.pas
//
// docs/specs/storage.md §2.10. Legacy splits generation (ITileFileNameGenerator) and parsing
// (ITileFileNameParser) into two interfaces, both implemented by one class per folder cache
// format (docs/survey/03-tile-storage.md line 159); the port merges them into one strategy
// object per format, matching how every call site uses generation and parsing together.
//
// Deviation: storage.md §2.10's interface names the tile coordinate type `geo::TilePoint`; per
// T-0021A (tile_info.h), the type that actually shipped is `geo::PointI` (a plain QPoint, same
// {x, y} layout) -- the same already-flagged deviation, not re-derived here.

#include "cg/geo/geo_types.h"

#include <QString>
#include <QStringView>
#include <QtGlobal>

#include <memory>

namespace cg::storage {

// Ports ITileFileNameGenerator + ITileFileNameParser + TTileFileNameBase.
class ITileNameCodec {
public:
    virtual ~ITileNameCodec() = default;

    // The tile's relative path WITHOUT extension, '/'-separated, no leading slash.
    // Ports GetTileFileName.
    [[nodiscard]] virtual QString tilePath(geo::PointI xy, quint8 zoom) const = 0;

    // Ports GetTilePoint. `relativePath` is '/'-separated and may carry any extension and any
    // number of leading directories. Returns false when it does not match.
    [[nodiscard]] virtual bool parse(QStringView relativePath, geo::PointI& xy, quint8& zoom) const = 0;

    // Ports TTileFileNameBase.AddExt -- Delphi ChangeFileExt: find the last '.' that comes after
    // the last '/'; if there is one, truncate the path there and append `ext`, otherwise just
    // append `ext`. Every in-scope folder codec's base name contains no dot, so this degenerates
    // to a plain append here -- kept as the general rule because tools/gen_cache_fixtures.py and
    // the tests exercise it directly, and the OsmAnd codec (T-0021C) overrides it.
    [[nodiscard]] virtual QString addExt(const QString& path, QStringView ext) const;
};
using ITileNameCodecPtr = std::shared_ptr<const ITileNameCodec>;

} // namespace cg::storage
