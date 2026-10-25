// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/tile_storage_of_map_type.h"

#include "cg/core/hash.h"
#include "cg/core/log.h"

#include <exception>

namespace cg::storage {

TileStorageOfMapType::TileStorageOfMapType(
    std::shared_ptr<GlobalCacheConfig> globalCacheConfig, geo::ProjectionSetPtr projectionSet,
    ITileStorageTypeListPtr tileStorageTypeList,
    std::shared_ptr<IMapVersionFactoryChangeableInternal> versionFactory,
    std::shared_ptr<SimpleTileStorageConfig> config, std::shared_ptr<TileInfoMemCache> cacheTileInfo,
    const core::ContentTypeInfo* mainContentType)
    : globalCacheConfig_(std::move(globalCacheConfig)), projectionSet_(std::move(projectionSet)),
      tileStorageTypeList_(std::move(tileStorageTypeList)), versionFactory_(std::move(versionFactory)),
      config_(std::move(config)), mainContentType_(mainContentType), cacheTileInfo_(std::move(cacheTileInfo)),
      actualPath_(core::PathConfig::create({}, {}, nullptr)),
      tileNotifier_(std::make_shared<TilePyramidNotifier>()) {
    // :230-231 before any subscription: the first build is not driven by a notification.
    updateActualPath();
    doUpdateStorage();

    configChanged_ = config_->changed().subscribe([this] { onConfigChange(); });
    globalConfigChanged_ = globalCacheConfig_->changed().subscribe([this] { onGlobalConfigChange(); });
    pathChanged_ = actualPath_->changed().subscribe([this] { onPathConfigChange(); });
}

TileStorageOfMapType::~TileStorageOfMapType() {
    configChanged_.reset();
    globalConfigChanged_.reset();
    pathChanged_.reset();
}

namespace {
QString buildErrorText(const QString& path, CacheTypeId typeCode, const QString& what) {
    return QStringLiteral("%1 (cache type %2): %3").arg(path).arg(static_cast<int>(typeCode)).arg(what);
}
} // namespace

void TileStorageOfMapType::buildStorage(const SimpleTileStorageConfigStaticPtr& config, CacheTypeId typeCode,
                                        const QString& path) {
    storage_.reset(); // B1: the old backend goes first, before the new one is built
    // DEVIATION D-165 (T-0275, L-44): the cause is kept for the CLI; the build itself and the "remembered"
    // state stay legacy (L-246)
    lastBuildError_.reset();
    try {
        currentTypeCode_ = typeCode;
        currentPath_ = path;
        if (mainContentType_ != nullptr) {
            const ITileStorageTypeListItemPtr item =
                tileStorageTypeList_->itemByCode(static_cast<int>(typeCode));
            if (item) {
                if (cacheTileInfo_) {
                    cacheTileInfo_->clear();
                    cacheTileInfo_->setOnTileInfoUpdate(nullptr);
                }
                const ITileStorageTypePtr type = item->storageType();
                auto built = type->build(config->abilities, projectionSet_, *mainContentType_, tileNotifier_,
                                         path, cacheTileInfo_);
                if (built) {
                    storage_ = *built;
                    versionFactory_->setFactory(type->mapVersionFactory());
                } else {
                    // legacy is silent (D-c2); an error Result stands for legacy's raised exception
                    qCWarning(core::logStorage) << "cannot build the tile storage" << path << "cache type"
                                                << static_cast<int>(typeCode) << built.error().what();
                    lastBuildError_ = buildErrorText(path, typeCode, built.error().what());
                }
            } else if (typeCode == CacheTypeId::Bdb || typeCode == CacheTypeId::BdbVersioned) {
                // T-0333 (owner 2026-10-21): the BerkeleyDB backend is removed; legacy would build it. The
                // other unsupported types (Ge, GeoCacher, Dbms) stay silent, as before.
                lastBuildError_ = buildErrorText(
                    path, typeCode,
                    QStringLiteral("BerkeleyDB caches (cache types 6 and 61) are no longer supported"));
            }
        }
    } catch (const std::exception& e) {
        storage_.reset();
        qCWarning(core::logStorage) << "cannot build the tile storage" << path << "cache type"
                                    << static_cast<int>(typeCode) << e.what();
        lastBuildError_ = buildErrorText(path, typeCode, QString::fromUtf8(e.what()));
    } catch (...) {
        storage_.reset();
        qCWarning(core::logStorage) << "cannot build the tile storage" << path << "cache type"
                                    << static_cast<int>(typeCode) << "unknown exception";
        lastBuildError_ = buildErrorText(path, typeCode, QStringLiteral("unknown exception"));
    }
    storageState_.setTarget(storage_); // B5: may notify inside storageMutex_ (L-c3)
}

void TileStorageOfMapType::doUpdateStorage() {
    bool needNotify = false;
    {
        const core::WriteLock lock(storageMutex_);
        const SimpleTileStorageConfigStaticPtr cfg = config_->snapshot();
        CacheTypeId code = cfg->cacheType;
        if (code == CacheTypeId::Default) {
            code = globalCacheConfig_->defCache();
        }
        QString path = actualPath_->fullPath();
        if (!path.endsWith(QLatin1Char('/'))) { // IncludeTrailingPathDelimiter
            path += QLatin1Char('/');
        }
        if (currentTypeCode_ != code || currentPath_ != path) {
            buildStorage(cfg, code, path);
            needNotify = true;
        }
    }
    if (needNotify) {
        tileNotifier_->notifyFull();
    }
}

std::optional<QString> TileStorageOfMapType::buildError() const {
    const core::ReadLock lock(storageMutex_);
    return lastBuildError_;
}

QString TileStorageOfMapType::path() const {
    const core::ReadLock lock(storageMutex_);
    return currentPath_;
}

ITileStoragePtr TileStorageOfMapType::getStorage() const {
    const core::ReadLock lock(storageMutex_);
    return storage_;
}

TileStorageTypeAbilities TileStorageOfMapType::typeAbilities() const {
    const ITileStoragePtr s = getStorage();
    return s ? s->typeAbilities() : TileStorageTypeAbilities{};
}

void TileStorageOfMapType::onConfigChange() {
    storageLock();
    try {
        updateActualPath();
        storageNeedUpdate_.store(true);
    } catch (...) {
        storageUnlock();
        throw;
    }
    storageUnlock();
}

void TileStorageOfMapType::onGlobalConfigChange() {
    storageLock();
    try {
        updateActualPath();
        storageNeedUpdate_.store(true);
    } catch (...) {
        storageUnlock();
        throw;
    }
    storageUnlock();
}

void TileStorageOfMapType::onPathConfigChange() {
    storageLock();
    storageNeedUpdate_.store(true);
    storageUnlock();
}

void TileStorageOfMapType::updateActualPath() {
    const SimpleTileStorageConfigStaticPtr cfg = config_->snapshot();
    CacheTypeId code = cfg->cacheType;
    if (code == CacheTypeId::Default) {
        code = globalCacheConfig_->defCache();
    }
    core::PathConfigPtr base;
    const ITileStorageTypeListItemPtr item = tileStorageTypeList_->itemByCode(static_cast<int>(code));
    if (item) {
        base = item->storageType()->basePath();
    }
    // legacy holds the path config's write lock around both setters (:402-408); the port defers the
    // notification with a batch instead (D-c1).
    const core::ConfigElement::BatchToken batch(*actualPath_);
    actualPath_->setBasePathConfig(base);
    actualPath_->setPath(cfg->nameInCache);
}

void TileStorageOfMapType::storageLock() {
    storageLockCount_.fetch_add(1);
}

void TileStorageOfMapType::storageUnlock() {
    if (storageLockCount_.fetch_sub(1) - 1 == 0 &&
        storageNeedUpdate_.exchange(false, std::memory_order_acq_rel)) {
        doUpdateStorage();
    }
}

core::Result<bool> TileStorageOfMapType::deleteTile(geo::PointI xy, quint8 zoom,
                                                    const MapVersionInfoPtr& version) {
    const ITileStoragePtr s = getStorage();
    return s ? s->deleteTile(xy, zoom, version) : core::Result<bool>(false);
}

const geo::IProjectionSet& TileStorageOfMapType::projectionSet() const {
    return *projectionSet_;
}

core::Result<MapVersionList> TileStorageOfMapType::listVersions(geo::PointI xy, quint8 zoom,
                                                                const MapVersionRequest& request) const {
    const ITileStoragePtr s = getStorage();
    return s ? s->listVersions(xy, zoom, request) : core::Result<MapVersionList>(MapVersionList{});
}

TilePyramidNotifier& TileStorageOfMapType::tileNotifier() {
    return *tileNotifier_;
}

StorageStatePtr TileStorageOfMapType::state() const {
    return storageState_.getStatic();
}

core::Changeable& TileStorageOfMapType::stateChangeable() {
    return storageState_;
}

QString TileStorageOfMapType::tileFileName(geo::PointI xy, quint8 zoom,
                                           const MapVersionInfoPtr& version) const {
    const ITileStoragePtr s = getStorage();
    return s ? s->tileFileName(xy, zoom, version) : QString();
}

core::Result<TileInfo> TileStorageOfMapType::tileInfo(geo::PointI xy, quint8 zoom,
                                                      const MapVersionInfoPtr& version, LoadMode mode) const {
    const ITileStoragePtr s = getStorage();
    return s ? s->tileInfo(xy, zoom, version, mode) : core::Result<TileInfo>(TileInfo{}); // D-106: Unknown
}

core::Result<TileInfo> TileStorageOfMapType::tileInfoEx(geo::PointI xy, quint8 zoom,
                                                        const MapVersionRequest& request,
                                                        LoadMode mode) const {
    const ITileStoragePtr s = getStorage();
    return s ? s->tileInfoEx(xy, zoom, request, mode) : core::Result<TileInfo>(TileInfo{}); // D-106: Unknown
}

core::Result<TileRectInfoPtr> TileStorageOfMapType::tileRectInfo(geo::RectI rect, quint8 zoom,
                                                                 const MapVersionRequest& request,
                                                                 std::stop_token stop) const {
    const ITileStoragePtr s = getStorage();
    return s ? s->tileRectInfo(rect, zoom, request, std::move(stop)) : core::Result<TileRectInfoPtr>(nullptr);
}

core::Result<bool> TileStorageOfMapType::saveTile(geo::PointI xy, quint8 zoom,
                                                  const MapVersionInfoPtr& version, TileDate loadDate,
                                                  QByteArrayView contentType, const QByteArray& data,
                                                  bool overwrite) {
    const ITileStoragePtr s = getStorage();
    if (!s) {
        return false;
    }
    auto saved = s->saveTile(xy, zoom, version, loadDate, contentType, data, overwrite);
    // T-0351: a refused tile with data (a TNE is never skipped). T-0352: only while the run has switched
    // the record on (a snapshot source); a static map, even with a non-empty Version=, records nothing.
    if (saved && !*saved && !data.isEmpty() && recordSkipped_.load()) {
        const std::lock_guard lock(skippedMutex_);
        skipped_.emplace_back(zoom, xy.x(), xy.y(), version,
                              SkippedSave{data.size(), core::crc32(0, data.constData(),
                                                                   static_cast<std::size_t>(data.size()))});
    }
    return saved;
}

void TileStorageOfMapType::setRecordSkippedSaves(bool on) {
    recordSkipped_.store(on);
}

std::map<std::tuple<quint8, int, int>, ITileStorage::SkippedSave>
TileStorageOfMapType::takeSkippedSaves(const MapVersionInfoPtr& version) {
    std::map<std::tuple<quint8, int, int>, SkippedSave> taken;
    const std::lock_guard lock(skippedMutex_);
    std::erase_if(skipped_, [&](const auto& entry) {
        const MapVersionInfoPtr& v = std::get<3>(entry);
        const bool same = (!v && !version) || (v && version && v->isSame(*version));
        if (same) {
            taken[{std::get<0>(entry), std::get<1>(entry), std::get<2>(entry)}] = std::get<4>(entry);
        }
        return same;
    });
    return taken;
}

core::Result<EnumTileInfoPtr> TileStorageOfMapType::scanTiles(bool ignoreTne, bool ignoreMultiVersionTiles) {
    const ITileStoragePtr s = getStorage();
    return s ? s->scanTiles(ignoreTne, ignoreMultiVersionTiles) : core::Result<EnumTileInfoPtr>(nullptr);
}

} // namespace cg::storage
