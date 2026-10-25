// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/ram_tile_storage.h"

#include "cg/storage/content_type_compat.h"

#include <optional>
#include <utility>

namespace cg::storage {

RamTileStorage::RamTileStorage(TileStorageTypeAbilities typeAbilities, TileStorageAbilities resolvedAbilities,
                               MapVersionFactoryPtr mapVersionFactory, geo::ProjectionSetPtr projectionSet,
                               TilePyramidNotifierPtr tileNotifier, core::ContentTypeInfo mainContentType,
                               std::shared_ptr<TileInfoMemCache> memCache)
    : base_(typeAbilities, resolvedAbilities, std::move(mapVersionFactory), std::move(projectionSet),
            std::move(tileNotifier), QString()),
      mainContentType_(std::move(mainContentType)), memCache_(std::move(memCache)) {
    if (!memCache_) {
        throw RamTileStorageMemCacheRequired{};
    }
    // Ports `FTileInfoMemCache.OnTileInfoUpdate := Self.NotifyTileUpdate`
    // (u_TileStorageInRAM.pas:152): every add()/evict() the mem-cache itself performs (including
    // ones triggered by saveTile/deleteTile below) notifies through the SAME injected
    // TilePyramidNotifier every other backend uses -- ports "notifications through the injected
    // notifier" (ticket). Runs while the mem-cache's own write lock is held (tile_info_mem_cache.h
    // doc); TilePyramidNotifier::notify() does not re-enter the mem-cache, so that is safe.
    memCache_->setOnTileInfoUpdate([this](const TileUpdate& update) {
        base_.notifyTileUpdate(update.tile, update.zoom, update.version);
    });
}

RamTileStorage::~RamTileStorage() {
    // u_TileStorageInRAM.pas:158-162: `FTileInfoMemCache.OnTileInfoUpdate := nil;` -- the mem cache
    // outlives this storage, so the `[this]` callback installed above must be cleared before this
    // object goes away.
    memCache_->setOnTileInfoUpdate({});
}

TileStorageTypeAbilities RamTileStorage::typeAbilities() const {
    return base_.typeAbilities();
}

StorageStatePtr RamTileStorage::state() const {
    return base_.state().get();
}

core::Changeable& RamTileStorage::stateChangeable() {
    return base_.state();
}

const geo::IProjectionSet& RamTileStorage::projectionSet() const {
    return base_.projectionSet();
}

TilePyramidNotifier& RamTileStorage::tileNotifier() {
    return base_.tileNotifier();
}

QString RamTileStorage::tileFileName(geo::PointI, quint8, const MapVersionInfoPtr&) const {
    return QStringLiteral(":: RAM");
}

core::Result<TileInfo> RamTileStorage::tileInfo(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version,
                                                LoadMode mode) const {
    // u_TileStorageInRAM.pas:173-184 -- no access check, `AUpdateTTL = False`.
    std::optional<TileInfo> cached = memCache_->get(xy, zoom, version, mode, /*updateTtl=*/false);
    if (cached.has_value()) {
        return *cached;
    }
    TileInfo info;
    info.type = TileInfoType::NotExists;
    return info;
}

core::Result<TileInfo> RamTileStorage::tileInfoEx(geo::PointI xy, quint8 zoom,
                                                  const MapVersionRequest& request, LoadMode mode) const {
    // u_TileStorageInRAM.pas:186-203: `VVersionInfo := AVersionInfo.BaseVersion`.
    return tileInfo(xy, zoom, request.baseVersion, mode);
}

core::Result<bool> RamTileStorage::deleteTile(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version) {
    if (!base_.state().get()->deleteAccess) {
        return false;
    }
    return memCache_->remove(xy, zoom, version);
}

core::Result<bool> RamTileStorage::saveTile(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version,
                                            TileDate loadDate, QByteArrayView contentType,
                                            const QByteArray& data, bool overwrite) {
    if (!base_.state().get()->addAccess) {
        return false;
    }

    if (!overwrite) {
        // Ports the reentrant `FTileInfoMemCache.Get(AXY, AZoom, AVersionInfo, gtimAsIs, False)`
        // probe (u_TileStorageInRAM.pas:292-296) -- no separate lock to worry about, TileInfoMemCache
        // already guards itself.
        std::optional<TileInfo> existing = memCache_->get(xy, zoom, version, LoadMode::AsIs, false);
        if (existing.has_value() && (existing->isExists() || existing->isExistsTne())) {
            return false;
        }
    }

    TileInfo newInfo;
    // "Assigned(AContentType) and Assigned(AData)" (u_TileStorageInRAM.pas:299) -- narrower than
    // FileSystemTileStorage's own gate (content type checked whenever `contentType` is non-empty,
    // regardless of `data`); each backend ports its own legacy order.
    if (!contentType.isEmpty() && !data.isNull()) {
        if (!contentTypeCompatibleForSave(mainContentType_, contentType)) {
            return core::fail(core::ErrorCode::Unsupported,
                              QStringLiteral("Bad content type for this tile storage"));
        }
        newInfo.type = TileInfoType::Exists;
        newInfo.loadDate = loadDate;
        newInfo.data = data;
        newInfo.size = static_cast<quint32>(data.size());
        newInfo.contentType = mainContentType_.type;
        newInfo.version = version;
    } else {
        newInfo.type = TileInfoType::TneExists;
        newInfo.loadDate = loadDate;
        newInfo.version = version;
    }

    memCache_->add(xy, zoom, version, newInfo); // fires the notifier via setOnTileInfoUpdate above
    return true;
}

core::Result<MapVersionList> RamTileStorage::listVersions(geo::PointI, quint8,
                                                          const MapVersionRequest&) const {
    return MapVersionList{}; // FEAT-STO-032 default (u_TileStorageAbstract.pas): empty list
}

core::Result<TileRectInfoPtr> RamTileStorage::tileRectInfo(geo::RectI rect, quint8 zoom,
                                                           const MapVersionRequest& request,
                                                           std::stop_token stop) const {
    // u_TileStorageInRAM.pas:222-223: `if StorageStateInternal.ReadAccess then ... else Result := nil`.
    if (!base_.state().get()->readAccess) {
        return TileRectInfoPtr{};
    }

    geo::RectI r = rect;
    base_.projectionSet().zoom(zoom)->validateTileRect(r);
    const int width = r.width();
    const int height = r.height();
    if (width <= 0 || height <= 0) {
        // u_TileStorageInRAM.pas:229: `(VCount.X > 0) and (VCount.Y > 0)` failing -- Result stays nil.
        return TileRectInfoPtr{};
    }
    if (width > 2048 || height > 2048) {
        // Legacy returns nil for a rect over the 2048x2048 cap (u_TileStorageInRAM.pas:229, the
        // `(VCount.X <= 2048) and (VCount.Y <= 2048)` guard); a null rect info is a legal result (spec §2.6
        // note 6).
        return TileRectInfoPtr{};
    }

    std::vector<TileRectInfoEntry> items(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    for (int y = r.top; y < r.bottom; ++y) {
        for (int x = r.left; x < r.right; ++x) {
            if (stop.stop_requested()) {
                // u_TileStorageInRAM.pas:233-236 sets Result := nil and exits on cancellation; the
                // port uses the project-wide std::stop_token idiom instead (F2 precedent), so
                // "cancelled" stays distinguishable from "access denied"/"degenerate rect".
                return core::fail(core::ErrorCode::Cancelled, QStringLiteral("tileRectInfo cancelled"));
            }

            const geo::PointI tile{x, y};
            // u_TileStorageInRAM.pas:240: `GetTileInfoEx(VTile, VZoom, AVersionInfo, gtimWithoutData)`.
            core::Result<TileInfo> info = tileInfoEx(tile, zoom, request, LoadMode::WithoutData);
            if (!info.has_value()) {
                return std::unexpected(info.error());
            }

            const qint64 idx =
                static_cast<qint64>(x - r.left) + static_cast<qint64>(y - r.top) * static_cast<qint64>(width);
            TileRectInfoEntry& entry = items[static_cast<std::size_t>(idx)];
            entry.type = info->type;
            entry.loadDate = info->loadDate;
            entry.size = info->size;
        }
    }

    return std::make_shared<const TileRectInfo>(r, zoom, std::move(items));
}

core::Result<EnumTileInfoPtr> RamTileStorage::scanTiles(bool ignoreTne, bool ignoreMultiVersionTiles) {
    // u_TileStorageInRAM.pas:335-340: `Result := FTileInfoMemCache.GetEnum(...)`, unconditional, no
    // access gate.
    return memCache_->enumerate(ignoreTne, ignoreMultiVersionTiles);
}

} // namespace cg::storage
