// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_PredicateByTileInfo.pas, u_PredicateByTileInfoBase.pas (7 of its 9 subclasses --
//        see predicates.cpp), i_PredicateByTileRequest.pas
//
// core.md §1.2 rules that the Src/Predicate aliases live in the module owning the argument type.
// `PredicateByBinaryData` stays in `maptype`, as core.md says; tileExistsCheckData below takes the
// data-check callback inline (a bare std::function) rather than naming that type from here.

#include "cg/geo/geo_types.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_date.h"
#include "cg/storage/tile_info.h"

#include <QByteArray>
#include <QtGlobal>

#include <functional>

namespace cg::storage {

// Ports i_PredicateByTileInfo.pas, u_PredicateByTileInfoBase.pas and its subclasses.
using PredicateByTileInfo = std::function<bool(const TileInfo&)>;
// Ports i_PredicateByTileRequest.pas.
using PredicateByTileRequest = std::function<bool(geo::PointI, quint8 zoom, const MapVersionInfoPtr&)>;

// storage.md §2.21 names four of these; Tech Lead ruling 2026-09-19 (T-0021A rework) adds the
// three other LIVE legacy predicates used by fr_DeleteTiles.pas -- legacy wins over the spec list
// (protocol rule 5). The two remaining legacy subclasses (NotExistsTNE, NotExistsTileOrTNE) have
// no call site anywhere in the legacy tree and stay unported.
[[nodiscard]] PredicateByTileInfo tileExists();      // ports TPredicateByTileInfoExistsTile
[[nodiscard]] PredicateByTileInfo tileNotExists();   // ports TPredicateByTileInfoNotExistsTile
[[nodiscard]] PredicateByTileInfo tileExistsTne();   // ports TPredicateByTileInfoExistsTNE
[[nodiscard]] PredicateByTileInfo tileExistsOrTne(); // ports TPredicateByTileInfoExistsTileOrTNE

// Ports TPredicateByTileInfoExistsTileCheckData. `dataCheck` ports IPredicateByBinaryData.Check;
// legacy's GetUseTileData returns True for this class, i.e. the caller must have loaded the
// TileInfo with LoadMode::WithData for `t.data` to be populated.
[[nodiscard]] PredicateByTileInfo tileExistsCheckData(std::function<bool(const QByteArray&)> dataCheck);

// Ports TPredicateByTileInfoNotExistOrBeforDate. AIgnoreTNE is not exposed (kept at legacy's own
// default, False): TPredicateByTileInfoNotExistOrBeforDate is never instantiated anywhere in the
// legacy tree, so no call site contradicts the choice (T-0021A review, D-4).
[[nodiscard]] PredicateByTileInfo tileOlderThan(const TileDate& cutoff);

// Ports TPredicateByTileInfoEqualSize (Check: FSize = ASize, exact legacy semantics -- NOT
// "differs"; see docs/handoffs/T-0021A.md Rework 1). `matchTne` ports ADeleteTNE and is exposed
// because legacy call sites pass both False (fr_DeleteTiles.pas:144,166) and True (:154).
[[nodiscard]] PredicateByTileInfo tileSizeEquals(quint32 size, bool matchTne = false);

} // namespace cg::storage
