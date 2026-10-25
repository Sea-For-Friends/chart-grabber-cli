// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/tile_storage_base.h"

#include <utility>

namespace cg::storage {

TileStorageBase::TileStorageBase(TileStorageTypeAbilities typeAbilities,
                                 TileStorageAbilities resolvedAbilities,
                                 MapVersionFactoryPtr mapVersionFactory, geo::ProjectionSetPtr projectionSet,
                                 TilePyramidNotifierPtr tileNotifier, QString storagePath)
    : typeAbilities_(typeAbilities), state_(resolvedAbilities), projectionSet_(std::move(projectionSet)),
      tileNotifier_(std::move(tileNotifier)), storagePath_(std::move(storagePath)),
      mapVersionFactory_(std::move(mapVersionFactory)) {}

void TileStorageBase::notifyTileUpdate(geo::PointI tile, quint8 zoom, const MapVersionInfoPtr& version) {
    tileNotifier_->notify(TileUpdate{tile, zoom, version});
}

} // namespace cg::storage
