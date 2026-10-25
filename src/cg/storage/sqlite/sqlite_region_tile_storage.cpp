// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_region_tile_storage.h"

#include "cg/storage/codecs/region_tile_name_codec.h"
#include "cg/storage/sqlite/sqlite_region_scan.h"

#include <optional>
#include <utility>
#include <vector>

namespace cg::storage::sqlite {
namespace {

// c_Max_Single_Zoom / c_Max_Fixed_Zoom of u_TileStorageSQLiteList.pas:99-100 (zero-based zooms).
constexpr quint8 kMaxSingleZoom = 8;
constexpr quint8 kMaxFixedZoom = 10;

[[nodiscard]] SqliteRegionPool::EntryKind kindOf(quint8 zoom) {
    return zoom <= kMaxFixedZoom ? SqliteRegionPool::EntryKind::Fixed : SqliteRegionPool::EntryKind::Dynamic;
}

[[nodiscard]] TileInfo notExists(const MapVersionInfoPtr& version) {
    TileInfo info;
    info.type = TileInfoType::NotExists;
    info.version = version;
    return info;
}

} // namespace

SqliteRegionTileStorage::SqliteRegionTileStorage(
    TileStorageTypeAbilities typeAbilities, TileStorageAbilities resolvedAbilities,
    MapVersionFactoryPtr mapVersionFactory, geo::ProjectionSetPtr projectionSet,
    TilePyramidNotifierPtr tileNotifier, QString storagePath, core::ContentTypeInfo mainContentType,
    bool isVersioned, std::shared_ptr<TileInfoMemCache> memCache, core::Notifier& gcTick)
    : base_(typeAbilities, resolvedAbilities, mapVersionFactory, std::move(projectionSet),
            std::move(tileNotifier), std::move(storagePath)),
      mainContentType_(std::move(mainContentType)), codec_(std::make_shared<const RegionTileNameCodec>()),
      memCache_(std::move(memCache)) {
    const StorageStatePtr state = base_.state().get();
    const bool isReadOnly = !state->addAccess && !state->deleteAccess && !state->replaceAccess;
    // The holder is always created with logging on (u_TileStorageSQLite.pas:196-202).
    auto schema = std::make_shared<SqliteRegionSchema>(base_.storagePath(), mainContentType_.type,
                                                       std::move(mapVersionFactory), /*logging=*/true);
    pool_ = std::make_shared<SqliteRegionPool>(std::move(schema), isVersioned, isReadOnly);
    // Ports the TListenerTTLCheck of the helper: every tick goes through the pool's 180 s TTL gate
    // (SqliteRegionPool::gcTick).
    gcSub_ = gcTick.subscribe([weak = std::weak_ptr<SqliteRegionPool>(pool_)] {
        if (const auto pool = weak.lock()) {
            pool->gcTick();
        }
    });
}

SqliteRegionTileStorage::~SqliteRegionTileStorage() {
    gcSub_ = core::Subscription{}; // legacy FGCNotifier.Remove first; pool_ is destroyed after
}

TileStorageTypeAbilities SqliteRegionTileStorage::typeAbilities() const {
    return base_.typeAbilities();
}

StorageStatePtr SqliteRegionTileStorage::state() const {
    return base_.state().get();
}

core::Changeable& SqliteRegionTileStorage::stateChangeable() {
    return base_.state();
}

const geo::IProjectionSet& SqliteRegionTileStorage::projectionSet() const {
    return base_.projectionSet();
}

TilePyramidNotifier& SqliteRegionTileStorage::tileNotifier() {
    return base_.tileNotifier();
}

QString SqliteRegionTileStorage::dbPath(geo::PointI xy, quint8 zoom) const {
    return base_.storagePath() + codec_->tilePath(xy, zoom) + QStringLiteral(".sqlitedb");
}

QString SqliteRegionTileStorage::tileFileName(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr&) const {
    return dbPath(xy, zoom);
}

core::Result<MapVersionList>
SqliteRegionTileStorage::listVersions(geo::PointI xy, quint8 zoom,
                                      const MapVersionRequest& /*request*/) const {
    // GetListOfTileVersions + Helper.GetListOfTileVersions (:274-528): nil unless read access and the
    // storage keeps versions.
    if (!base_.state().get()->readAccess || !pool_->useVersionFieldInDb()) {
        return MapVersionList{};
    }
    core::Result<MapVersionList> result = MapVersionList{};
    pool_->run(dbPath(xy, zoom), kindOf(zoom), false, std::stop_token{}, [&](SqliteRegionHandler& handler) {
        result = handler.listTileVersions(xy, std::stop_token{});
    });
    return result;
}

core::Result<TileInfo> SqliteRegionTileStorage::tileInfoInternal(geo::PointI xy, quint8 zoom,
                                                                 const MapVersionInfoPtr& version,
                                                                 LoadMode mode,
                                                                 bool showOtherVersions) const {
    if (memCache_) {
        std::optional<TileInfo> cached = memCache_->get(xy, zoom, version, mode, true);
        if (cached.has_value()) {
            return *cached;
        }
    }

    core::Result<TileInfo> result = notExists(nullptr);
    if (base_.state().get()->readAccess) {
        result = notExists(version);
        // DEVIATION D-160 (T-0276, L-75): a file that exists but cannot be opened is an error (legacy: a nil
        // handler, NotExists, cached -- u_TileStorageSQLiteHelper.pas InternalGetHandler).
        QString openError;
        pool_->run(
            dbPath(xy, zoom), kindOf(zoom), false, std::stop_token{},
            [&](SqliteRegionHandler& handler) {
                core::Result<TileInfo> found = handler.tileInfo(xy, version, showOtherVersions, mode);
                if (!found.has_value() || found->type != TileInfoType::NotExists) {
                    result = std::move(found);
                }
            },
            &openError);
        if (!openError.isEmpty()) {
            result = core::fail(core::ErrorCode::IoError, openError);
        }
    }
    if (result.has_value() && memCache_) {
        memCache_->add(xy, zoom, version, *result);
    }
    return result;
}

// FEAT-STO-018
core::Result<TileInfo> SqliteRegionTileStorage::tileInfo(geo::PointI xy, quint8 zoom,
                                                         const MapVersionInfoPtr& version,
                                                         LoadMode mode) const {
    const bool showOtherVersions = !(version && !version->storeString().isEmpty());
    return tileInfoInternal(xy, zoom, version, mode, showOtherVersions);
}

core::Result<TileInfo> SqliteRegionTileStorage::tileInfoEx(geo::PointI xy, quint8 zoom,
                                                           const MapVersionRequest& request,
                                                           LoadMode mode) const {
    return tileInfoInternal(xy, zoom, request.baseVersion, mode, request.showOtherVersions);
}

core::Result<TileRectInfoPtr> SqliteRegionTileStorage::tileRectInfo(geo::RectI rect, quint8 zoom,
                                                                    const MapVersionRequest& request,
                                                                    std::stop_token stop) const {
    if (!base_.state().get()->readAccess) {
        return TileRectInfoPtr{};
    }

    geo::RectI r = rect;
    base_.projectionSet().zoom(zoom)->validateTileRect(r);
    const int width = r.width();
    const int height = r.height();
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048) {
        return TileRectInfoPtr{}; // legacy leaves Result = nil (u_TileStorageSQLite.pas:339, :366-369)
    }

    std::vector<TileRectInfoEntry> items(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    const auto fill = [&](geo::PointI regionXy) {
        pool_->run(dbPath(regionXy, zoom), kindOf(zoom), false, stop, [&](SqliteRegionHandler& handler) {
            [[maybe_unused]] const bool notCancelled =
                handler.tileRectInfo(r, request.baseVersion, request.showOtherVersions, items, stop);
        });
    };

    // Helper.GetTileRectInfo: zooms up to c_Max_Single_Zoom live in one file, higher ones in the
    // region files the rectangle covers.
    if (zoom <= kMaxSingleZoom) {
        fill(geo::PointI{0, 0});
    } else {
        for (int i = r.left >> 8; i <= (r.right - 1) >> 8; ++i) {
            if (stop.stop_requested()) {
                return TileRectInfoPtr{};
            }
            for (int j = r.top >> 8; j <= (r.bottom - 1) >> 8; ++j) {
                if (stop.stop_requested()) {
                    return TileRectInfoPtr{};
                }
                fill(geo::PointI{i << 8, j << 8});
            }
        }
    }
    if (stop.stop_requested()) {
        return TileRectInfoPtr{}; // cancelled
    }
    return std::make_shared<const TileRectInfo>(r, zoom, std::move(items));
}

core::Result<bool> SqliteRegionTileStorage::saveTile(geo::PointI xy, quint8 zoom,
                                                     const MapVersionInfoPtr& version, TileDate loadDate,
                                                     QByteArrayView contentType, const QByteArray& data,
                                                     bool overwrite) {
    SaveFlags flags = SaveFlag::SkipIfSameAsPrev;
    if (!overwrite) {
        flags |= SaveFlag::KeepExisting;
    }
    return saveTile(xy, zoom, version, loadDate, contentType, data, flags);
}

// FEAT-STO-018
core::Result<bool> SqliteRegionTileStorage::saveTile(geo::PointI xy, quint8 zoom,
                                                     const MapVersionInfoPtr& version, TileDate loadDate,
                                                     QByteArrayView contentType, const QByteArray& data,
                                                     SaveFlags flags) {
    if (!base_.state().get()->addAccess) {
        return false;
    }

    core::Result<bool> saved = false; // no handler (read-only pool, failed open): False
    pool_->run(dbPath(xy, zoom), kindOf(zoom), true, std::stop_token{}, [&](SqliteRegionHandler& handler) {
        saved = handler.saveTile(xy, version, loadDate, contentType, data, flags);
    });
    if (!saved.has_value() || !*saved) {
        return saved;
    }

    if (memCache_) {
        TileInfo info;
        info.loadDate = loadDate;
        info.version = version;
        if (data.isNull()) {
            info.type = TileInfoType::TneExists;
        } else {
            info.type = TileInfoType::Exists;
            info.size = static_cast<quint32>(data.size());
            info.data = data;
            info.contentType = mainContentType_.type;
        }
        memCache_->add(xy, zoom, version, std::move(info));
    }
    base_.notifyTileUpdate(xy, zoom, version); // no lock held -- spec §3.1 rule 3
    return true;
}

// FEAT-STO-018
core::Result<bool> SqliteRegionTileStorage::deleteTile(geo::PointI xy, quint8 zoom,
                                                       const MapVersionInfoPtr& version) {
    if (!base_.state().get()->deleteAccess) {
        return false;
    }

    // Helper.DeleteTile: no region file (or no handler) -> True; the face's try/except -> False.
    bool deleted = true;
    pool_->run(dbPath(xy, zoom), kindOf(zoom), false, std::stop_token{}, [&](SqliteRegionHandler& handler) {
        core::Result<bool> result = handler.deleteTile(xy, version);
        deleted = result.has_value() && *result;
    });
    if (!deleted) {
        return false;
    }

    if (memCache_) {
        memCache_->remove(xy, zoom, version);
    }
    base_.notifyTileUpdate(xy, zoom, version);
    return true;
}

// FEAT-STO-018
core::Result<EnumTileInfoPtr> SqliteRegionTileStorage::scanTiles(bool ignoreTne,
                                                                 bool /*ignoreMultiVersionTiles*/) {
    // Stored and never read by the enumerator (legacy defect L-91).
    if (!base_.state().get()->scanAccess) {
        return EnumTileInfoPtr{};
    }
    return makeSqliteRegionScan(pool_, base_.storagePath(), codec_, ignoreTne);
}

} // namespace cg::storage::sqlite
