// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_type.h"

#include "cg/core/language_manager.h"
#include "cg/geo/geo_types.h"
#include "cg/maptype/layer_draw_config.h"
#include "cg/maptype/map_abilities_config.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/map_version_factory_changeable.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/map_version_request_config.h"
#include "cg/maptype/tile_download_request_builder_config.h"
#include "cg/maptype/tile_download_subsystem.h"
#include "cg/maptype/zmp_info.h"
#include "cg/net/tile_downloader_config.h"
#include "cg/storage/simple_tile_storage_config.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_storage.h"
#include "cg/storage/tile_storage_of_map_type.h"

#include <QDir>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cg::maptype {

namespace {
// c_InternalBrowser.pas:39: CSASInternalURLPrefix + CMapDataInternalDomain + '/'.
constexpr QLatin1String kMapDataInternalUrl("sas://MapData/");
} // namespace

// u_MapType.pas:298-484. Step numbers are those of docs/specs/maptype.md 2.11.6.
MapType::MapType(std::shared_ptr<core::ILanguageManager> languageManager, std::shared_ptr<const IZmpInfo> zmp,
                 storage::MapVersionFactoryPtr simpleVersionFactory,
                 std::shared_ptr<storage::GlobalCacheConfig> globalCacheConfig,
                 storage::ITileStorageTypeListPtr tileStorageTypeList, core::Notifier& gcNotifier,
                 std::stop_token appClosing, std::shared_ptr<net::InetConfig> inetConfig,
                 std::shared_ptr<const net::IGlobalDownloadConfig> downloadConfig,
                 std::shared_ptr<net::IDownloaderFactory> downloaderFactory,
                 std::shared_ptr<const core::ContentTypeRegistry> contentTypes,
                 std::shared_ptr<const ContentConverters> converters,
                 std::shared_ptr<const script::IProjConverterFactory> projFactory,
                 const core::IConfigProviderPtr& localConfig, int appId, const QString& logsPath) {
    if (!zmp || !contentTypes) {
        throw std::invalid_argument("MapType: null zmp or content type registry");
    }
    zmp_ = std::move(zmp);                                                         // :331
    guiConfig_ = std::make_shared<MapTypeGuiConfig>(languageManager, zmp_->gui()); // :334-338
    layerDrawConfig_ = std::make_shared<LayerDrawConfig>(zmp_->layerZOrder());     // :339
    const auto zmpDownloader = zmp_->tileDownloaderConfig();
    if (!zmpDownloader) {
        throw std::invalid_argument("MapType: the ZMP has no tile downloader settings");
    }
    tileDownloaderConfig_ =
        std::make_shared<net::TileDownloaderConfig>(std::move(inetConfig), *zmpDownloader); // :344
    tileDownloadRequestBuilderConfig_ =
        std::make_shared<TileDownloadRequestBuilderConfig>(zmp_->tileDownloadRequestBuilderConfig()); // :345
    versionFactory_ = std::make_shared<MapVersionFactoryChangeable>(simpleVersionFactory); // :347-351
    versionRequestConfig_ = std::make_shared<MapVersionRequestConfig>(zmp_->version());    // :352-355
    versionRequest_ =
        std::make_shared<MapVersionRequestChangeable>(versionRequestConfig_, versionFactory_); // :356-360
    versionChangeSubscription_ =
        versionRequestConfig_->changed().subscribe([this] { clearMemCache(); }); // :362-363, :832-835
    storageConfig_ = std::make_shared<storage::SimpleTileStorageConfig>(zmp_->storageConfig()); // :365
    abilitiesConfig_ = std::make_shared<MapAbilitiesConfig>(zmp_->abilities());                 // :366
    guiConfig_->readConfig(localConfig);                                                        // :368-374
    layerDrawConfig_->readConfig(localConfig);
    storageConfig_->readConfig(localConfig);
    abilitiesConfig_->readConfig(localConfig);
    versionRequestConfig_->readConfig(localConfig);
    tileDownloaderConfig_->readConfig(localConfig);
    tileDownloadRequestBuilderConfig_->readConfig(localConfig);

    contentTypes_ = std::move(contentTypes); // :376-381
    if (!storageConfig_->mainContentType().isEmpty()) {
        contentType_ = contentTypes_->infoByType(storageConfig_->mainContentType());
    }
    if (contentType_ == nullptr) {
        contentType_ = contentTypes_->infoByExt(storageConfig_->tileFileExt());
    }
    if (contentType_ == nullptr) {
        // Unreachable by D-74 (ZmpInfo rejected it with the same registry and rule); legacy goes on with nil
        // and crashes on first use (:592, L-175).
        throw std::logic_error("MapType: content type not in the registry");
    }

    projectionSet_ = zmp_->projectionSet();         // :383
    viewProjectionSet_ = zmp_->viewProjectionSet(); // :384

    if (storageConfig_->useMemCache()) { // :386-397
        cacheTileInfo_ = std::make_shared<storage::TileInfoMemCache>(storageConfig_->memCacheCapacity(),
                                                                     storageConfig_->memCacheTtl(),
                                                                     storageConfig_->memCacheClearStrategy());
        // The cache shared_ptr is captured, not `this`: a tick already running on the GC thread while the
        // map is destroyed touches a live object.
        cacheTtlSubscription_ = gcNotifier.subscribe([cache = cacheTileInfo_] { cache->clearExpired(); });
    }

    storage_ = std::make_shared<storage::TileStorageOfMapType>( // :399-409
        std::move(globalCacheConfig), projectionSet_, std::move(tileStorageTypeList), versionFactory_,
        storageConfig_, cacheTileInfo_, contentType_);

    // :410-441 -- bitmap branch: TODO(T-0148) (caches, loaders). Vector branch: the url prefix only.
    if (contentType_->kind == core::ContentKind::Vector) {
        mapDataUrlPrefix_ = kMapDataInternalUrl + zmp_->guid().toString(QUuid::WithBraces).toUpper() + u'/';
        // TODO(T-0148): vector cache, KML loader, FVectorDataFactory, FVectorItemSubsetBuilderFactory.
    }

    // :443-473. Not passed (the landed subsystem takes none of them): ADownloaderThreadConfig,
    // AProjectionSetFactory, AInvisibleBrowser, the resamplers, the bitmap factory, the download result
    // factory; the ZMP-derived arguments are read from the ZMP inside the subsystem.
    tileDownloadSubsystem_ = std::make_shared<TileDownloadSubsystem>(
        zmp_, gcNotifier, std::move(appClosing), std::move(languageManager), std::move(downloadConfig),
        std::move(downloaderFactory), std::move(simpleVersionFactory), tileDownloaderConfig_,
        tileDownloadRequestBuilderConfig_, contentTypes_, std::move(converters), *contentType_,
        abilitiesConfig_, std::move(projFactory), storage_, appId, logsPath);

    if (zmp_->isLayer()) { // :475-483
        loadPrevMaxZoomDelta_ = isKmlTiles() ? 8 : 4;
    } else {
        loadPrevMaxZoomDelta_ = 6;
    }
}

// :486-494. Only the version-change subscription goes here; the members die in reverse declaration order.
MapType::~MapType() {
    versionChangeSubscription_.reset();
}

// :496-505 -- note: not the read order.
void MapType::saveConfig(const core::IConfigWriterPtr& localConfig) const {
    guiConfig_->writeConfig(localConfig);
    layerDrawConfig_->writeConfig(localConfig);
    tileDownloadRequestBuilderConfig_->writeConfig(localConfig);
    tileDownloaderConfig_->writeConfig(localConfig);
    versionRequestConfig_->writeConfig(localConfig);
    storageConfig_->writeConfig(localConfig);
    abilitiesConfig_->writeConfig(localConfig);
}

// :285-296. cacheBitmap / cacheVector: TODO(T-0148).
void MapType::clearMemCache() {
    if (cacheTileInfo_) {
        cacheTileInfo_->clear();
    }
}

// :555-558: ExtractFileName(ExtractFileDir(IncludeTrailingPathDelimiter(NameInCache))).
QString MapType::shortFolderName() const {
    QString path = storageConfig_->nameInCache();
    if (path.endsWith(u'/') || path.endsWith(u'\\')) {
        path.chop(1);
    }
    const auto sep = std::max(path.lastIndexOf(u'/'), path.lastIndexOf(u'\\'));
    return path.mid(sep + 1);
}

// :580-597.
QString MapType::getTileShowName(geo::PointI xy, quint8 zoom,
                                 const storage::MapVersionInfoPtr& version) const {
    QString result = storage_->tileFileName(xy, zoom, version);
    const storage::TileStorageTypeAbilities abilities = storage_->typeAbilities();
    const QChar sep = QDir::separator();
    if (abilities.storageClass != storage::StorageClass::InSeparateFiles) {
        result += QStringLiteral(" :: ") + u'z' + QString::number(zoom + 1) + sep + u'x' +
                  QString::number(xy.x()) + sep + u'y' + QString::number(xy.y()) +
                  QString::fromLatin1(contentType_->defaultExt);
    }
    // `and` short-circuits: the version is read only for a multi-version storage (:594). A null version
    // dereferences in legacy; here it is an empty store string.
    if (abilities.version == storage::VersionSupport::MultiVersions && version &&
        !version->storeString().isEmpty()) {
        result += sep + QStringLiteral("v") + version->storeString();
    }
    return result;
}

// :790-830.
void MapType::nextVersion(const geo::LocalCoordConverterPtr& view, int step) {
    if (storage_->typeAbilities().version != storage::VersionSupport::MultiVersions) {
        return;
    }
    if (!view) { // legacy: access violation (callers not traced); nothing to step from
        return;
    }
    const geo::ProjectionPtr mapProjection = projectionSet_->suitableProjection(*view->projection());
    if (!mapProjection) {
        return;
    }
    const geo::LonLat lonLat = view->centerLonLat();
    if (!mapProjection->projectionType()->checkLonLatPos(lonLat)) {
        return;
    }
    const geo::PointI mapTile =
        geo::roundPoint(mapProjection->lonLat2TilePosFloat(lonLat), geo::PointRounding::ToTopLeft);
    // Legacy passes nil: the backends read it as "show all versions, no base"
    // (u_TileStorageBerkeleyDB.pas:625-629).
    const auto list =
        storage_->listVersions(mapTile, mapProjection->zoom(), storage::MapVersionRequest{nullptr, true});
    // Legacy indexes an empty list (-1 + step) and reads outside it; here an empty list or a failed call
    // leaves the version unchanged (D row of T-0140).
    if (!list || !*list || (*list)->empty()) {
        return;
    }
    const auto& versions = **list;
    const auto count = static_cast<int>(versions.size());
    const storage::MapVersionInfoPtr base = versionRequest_->getStatic()->baseVersion;
    int index = -1;
    for (int i = 0; i < count; ++i) {
        if (base && versions[static_cast<std::size_t>(i)] &&
            base->isSame(*versions[static_cast<std::size_t>(i)])) {
            index = i;
            break;
        }
    }
    index += step;
    if (index >= count) {
        index = 0;
    }
    if (index < 0) {
        index = count - 1;
    }
    const auto& chosen = versions[static_cast<std::size_t>(index)];
    versionRequestConfig_->setVersion(chosen ? chosen->storeString() : QString());
}

// :560-683.
QUuid MapType::guid() const {
    return zmp_->guid();
}
std::shared_ptr<const IZmpInfo> MapType::zmp() const {
    return zmp_;
}
geo::ProjectionSetPtr MapType::projectionSet() const {
    return projectionSet_;
}
geo::ProjectionSetPtr MapType::viewProjectionSet() const {
    return viewProjectionSet_;
}
std::shared_ptr<MapVersionFactoryChangeable> MapType::versionFactory() const {
    return versionFactory_;
}
std::shared_ptr<MapVersionRequestConfig> MapType::versionRequestConfig() const {
    return versionRequestConfig_;
}
std::shared_ptr<MapVersionRequestChangeable> MapType::versionRequest() const {
    return versionRequest_;
}
const core::ContentTypeInfo* MapType::contentType() const {
    return contentType_;
}
std::shared_ptr<MapAbilitiesConfig> MapType::abilities() const {
    return abilitiesConfig_;
}
std::shared_ptr<storage::SimpleTileStorageConfig> MapType::storageConfig() const {
    return storageConfig_;
}
// :669-677 tested the loader pointers that :410-422 set from the content type's interface; the registry's
// kind is that interface (a type is Bitmap or Vector, never both, as the `else if` at :421).
bool MapType::isBitmapTiles() const {
    return contentType_->kind == core::ContentKind::Bitmap;
}
bool MapType::isKmlTiles() const {
    return contentType_->kind == core::ContentKind::Vector;
}
std::shared_ptr<ITileDownloadSubsystem> MapType::tileDownloadSubsystem() const {
    return tileDownloadSubsystem_;
}
std::shared_ptr<storage::ITileStorage> MapType::tileStorage() const {
    return storage_;
}
std::shared_ptr<MapTypeGuiConfig> MapType::guiConfig() const {
    return guiConfig_;
}
std::shared_ptr<LayerDrawConfig> MapType::layerDrawConfig() const {
    return layerDrawConfig_;
}
std::shared_ptr<net::TileDownloaderConfig> MapType::tileDownloaderConfig() const {
    return tileDownloaderConfig_;
}
std::shared_ptr<TileDownloadRequestBuilderConfig> MapType::tileDownloadRequestBuilderConfig() const {
    return tileDownloadRequestBuilderConfig_;
}
// Both null until T-0148 (a KML map has no bitmap cache and vice versa, :410-441).
std::shared_ptr<ITileObjCacheBitmap> MapType::cacheBitmap() const {
    return nullptr;
}
std::shared_ptr<ITileObjCacheVector> MapType::cacheVector() const {
    return nullptr;
}
std::shared_ptr<storage::TileInfoMemCache> MapType::cacheTileInfo() const {
    return cacheTileInfo_;
}

// TODO(T-0148): tile loading, u_MapType.pas:507-553, 684-789, 837-1184. nullptr = "no tile" (legacy nil).
BitmapPtr MapType::loadTile(geo::PointI, quint8, const MapVersionRequestPtr&, bool,
                            const std::shared_ptr<ITileObjCacheBitmap>&) const {
    return nullptr;
}
VectorSubsetPtr MapType::loadTileVector(geo::PointI, quint8, const MapVersionRequestPtr&, bool, bool,
                                        const std::shared_ptr<ITileObjCacheVector>&) const {
    return nullptr;
}
BitmapPtr MapType::loadTileUni(geo::PointI, const geo::ProjectionPtr&, const MapVersionRequestPtr&, bool,
                               bool, bool, const std::shared_ptr<ITileObjCacheBitmap>&) const {
    return nullptr;
}
BitmapPtr MapType::loadBitmap(geo::RectI, quint8, const MapVersionRequestPtr&, bool, bool, bool,
                              const std::shared_ptr<ITileObjCacheBitmap>&) const {
    return nullptr;
}
BitmapPtr MapType::loadBitmapUni(geo::RectI, const geo::ProjectionPtr&, const MapVersionRequestPtr&, bool,
                                 bool, bool, const std::shared_ptr<ITileObjCacheBitmap>&) const {
    return nullptr;
}

} // namespace cg::maptype
