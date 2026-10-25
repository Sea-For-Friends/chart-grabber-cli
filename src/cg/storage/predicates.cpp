// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/predicates.h"

namespace cg::storage {

PredicateByTileInfo tileExists() {
    return [](const TileInfo& t) { return t.type == TileInfoType::Exists; };
}

PredicateByTileInfo tileNotExists() {
    // Ports TPredicateByTileInfoNotExistsTile: FInfoType <> titExists. True for both NotExists
    // and TneExists (a TNE record is not "an existing tile").
    return [](const TileInfo& t) { return t.type != TileInfoType::Exists; };
}

PredicateByTileInfo tileExistsTne() {
    // Ports TPredicateByTileInfoExistsTNE: FInfoType = titTneExists. Used by fr_DeleteTiles.pas:150
    // ("Delete tne").
    return [](const TileInfo& t) { return t.type == TileInfoType::TneExists; };
}

PredicateByTileInfo tileExistsOrTne() {
    // Ports TPredicateByTileInfoExistsTileOrTNE: titTneExists OR titExists. Used by
    // fr_DeleteTiles.pas:156 ("Delete both").
    return
        [](const TileInfo& t) { return t.type == TileInfoType::TneExists || t.type == TileInfoType::Exists; };
}

PredicateByTileInfo tileExistsCheckData(std::function<bool(const QByteArray&)> dataCheck) {
    // Ports TPredicateByTileInfoExistsTileCheckData.Check: titExists AND FDataCheck.Check(FData).
    // Used by fr_DeleteTiles.pas:164 ("Delete empty tiles" via the ZMP EmptyTileSamples check).
    return [check = std::move(dataCheck)](const TileInfo& t) {
        return t.type == TileInfoType::Exists && check(t.data);
    };
}

PredicateByTileInfo tileOlderThan(const TileDate& cutoff) {
    // Ports TPredicateByTileInfoNotExistOrBeforDate with AIgnoreTNE=False (the legacy class takes
    // that flag; the spec's single-argument factory does not expose it, so the conservative
    // default is used -- a TNE record is never "older", only a missing or stale Exists tile is).
    return [cutoff](const TileInfo& t) {
        switch (t.type) {
        case TileInfoType::NotExists:
            return true;
        case TileInfoType::Exists:
            // storage.md §2.4.3: an invalid loadDate is "infinitely old" and must not depend on
            // QDateTime's own ordering of invalid vs. valid values (that ordering only became
            // invalid-sorts-first in Qt 6.8; the project's floor is Qt 6.5 -- T-0021A review
            // finding 3). Check isValid() explicitly instead of just `t.loadDate < cutoff`.
            return !t.loadDate.isValid() || t.loadDate < cutoff;
        case TileInfoType::TneExists:
        case TileInfoType::Unknown:
            return false;
        }
        return false;
    };
}

PredicateByTileInfo tileSizeEquals(quint32 size, bool matchTne) {
    // Ports TPredicateByTileInfoEqualSize.Check exactly (docs/handoffs/T-0021A.md Rework 1 fixes
    // an earlier inversion that shipped as `tileSizeDiffers`):
    //   titExists    -> FSize = ASize
    //   titTneExists -> FDeleteTNE (matchTne)
    //   otherwise    -> False
    return [size, matchTne](const TileInfo& t) {
        if (t.type == TileInfoType::Exists) {
            return t.size == size;
        }
        if (t.type == TileInfoType::TneExists) {
            return matchTne;
        }
        return false;
    };
}

} // namespace cg::storage
