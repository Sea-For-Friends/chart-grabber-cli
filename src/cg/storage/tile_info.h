// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileInfoBasic.pas, u_TileInfoBasic.pas
//
// Legacy has a record (TTileInfo) for enumeration and a four-class interface hierarchy
// (ITileInfoBasic, ITileInfoWithData, TTileInfoBasicNotExists/TNE/Exists/ExistsWithTile) for
// lookups. The port keeps one value type; the "with data" distinction becomes "is `data` empty".

#include "cg/core/result.h"
#include "cg/geo/geo_types.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_date.h"

#include <QByteArray>
#include <QtGlobal>

#include <cstdint>
#include <memory>

namespace cg::storage {

// NOTE: legacy's TTileInfoType is (titUnknown=0, titNotExists=1, titExists=2, titTneExists=3).
// The numeric values are NOT persisted by any cache format (verified by grep across Src/), so the
// port reorders them, which kills the `>= titExists` shortcut that would silently include TNE.
// If you find a format that persists the integer, stop and file an open question.
// Unknown is also legacy's nil answer (`Result := nil` of GetTileInfo): "nothing is known", the value of a
// default-constructed TileInfo. A caller ports `Assigned(info)` as `type != Unknown`.
// FEAT-STO-011
enum class TileInfoType : std::uint8_t { Unknown = 0, NotExists = 1, TneExists = 2, Exists = 3 };

// FEAT-STO-011
struct TileInfo {
    TileInfoType type = TileInfoType::Unknown;
    TileDate loadDate;         // invalid unless type is Exists or TneExists
    quint32 size = 0;          // bytes; 0 for TNE
    QByteArray data;           // empty unless loaded with LoadMode::WithData
    MapVersionInfoPtr version; // null when the backend ignores versions
    QByteArray contentType;    // canonical MIME, core::ContentTypeInfo::type

    [[nodiscard]] bool isExists() const noexcept { return type == TileInfoType::Exists; }
    [[nodiscard]] bool isExistsTne() const noexcept { return type == TileInfoType::TneExists; }
};

// Ports TGetTileInfoMode (i_TileStorage.pas): gtimWithData / gtimWithoutData / gtimAsIs.
// `AsIs` means "whatever is cheapest": the filesystem backend treats it as WithoutData, and the
// mem-cache returns whatever it happens to hold.
enum class LoadMode : std::uint8_t { WithData, WithoutData, AsIs };

// Ports IEnumTileInfo. A pull enumerator, not a range: the filesystem implementation walks a
// QDirIterator lazily over caches with millions of files.
//
// Deviation: storage.md §1.4's forward-declaration contract names the tile coordinate type
// `geo::TilePoint`; geo slice A (T-0016) landed it as `geo::PointI` (a plain QPoint, identical
// {x, y} layout and half-open-rect semantics) instead. Used here as the closest 1:1 match to what
// actually shipped; flagged in the ticket's Open questions for the Architect to correct §1.4.
class IEnumTileInfo {
public:
    virtual ~IEnumTileInfo() = default;
    // Fills the outputs and returns true, or returns false when the scan is over -- at the end
    // OR because it failed; status() tells which. `out.type` is never Unknown.
    [[nodiscard]] virtual bool next(TileInfo& out, geo::PointI& tile, quint8& zoom) = 0;
    // Why next() returned false: a value = the scan reached its end (or is still running); an
    // error = the scan failed, with the legacy message text. Sticky: once it holds an error,
    // next() keeps returning false and status() keeps returning that error. The default suits an
    // enumerator that cannot fail. Ports the exception legacy raises out of Next (D-45).
    [[nodiscard]] virtual core::Result<void> status() const { return {}; }
};
using EnumTileInfoPtr = std::unique_ptr<IEnumTileInfo>;

} // namespace cg::storage
