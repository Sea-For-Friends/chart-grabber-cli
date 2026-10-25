// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_types_main_list.h"

#include "cg/geo/geo_types.h"
#include "cg/maptype/log_maptype.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/zmp_info.h"
#include "cg/storage/tile_storage.h"

#include <array>
#include <utility>

namespace cg::maptype {

namespace {
// u_ResStrings.pas:83, :108
constexpr const char* kErrNoMaps = "Unable to load map(s)! Aborting...";
constexpr const char* kErrMainMapNotExists = "You need at least one MAP among your ZMP files";

// GUIDToString: the braced upper-case form (the expression of map_type.cpp for mapDataUrlPrefix_).
QString guidString(const QUuid& guid) {
    return guid.toString(QUuid::WithBraces).toUpper();
}
} // namespace

// :157-181
MapTypesMainList::MapTypesMainList(std::shared_ptr<const IMapTypeSetBuilderFactory> setBuilderFactory,
                                   std::shared_ptr<const IZmpInfoSet> zmpInfoSet)
    : setBuilderFactory_(std::move(setBuilderFactory)), zmpInfoSet_(std::move(zmpInfoSet)),
      fullMapsSetChangeable_(std::make_shared<MapTypeSetChangeableSimple>(setBuilderFactory_, nullptr)) {}

// Legacy: access violation (no handler) when read before LoadMaps (:194, :209, :437, :495); unreachable in
// legacy (`SASPlanet.dpr:1705-1712` exits before the form exists); no register row.
void MapTypesMainList::requireLoaded() const {
    if (!guiConfigList_ || !mapsSet_) {
        throw std::logic_error("MapTypesMainList: loadMaps has not run");
    }
}

// :183-200
QUuid MapTypesMainList::defaultMainMapGuid() const {
    requireLoaded();
    const std::array<QUuid, 3> kDefaults = {
        QUuid(QStringLiteral("{CBA03063-23D9-4FA4-931A-9182B98644B1}")), // Google Maps
        QUuid(QStringLiteral("{D643C5A1-4331-4387-9BE2-DDDF185C117E}")), // OpenStreetMap
        QUuid(QStringLiteral("{8238C84A-D37E-45E1-A735-FBCFBCD4168C}"))  // Yandex Maps
    };
    for (const QUuid& guid : kDefaults) {
        if (mapsSet_->mapTypeByGuid(guid)) {
            return guid;
        }
    }
    return firstMainMapGuid();
}

// :202-217
QUuid MapTypesMainList::firstMainMapGuid() const {
    requireLoaded();
    const MapGuidListPtr list = guiConfigList_->orderedMapGuidList();
    if (list) {
        for (const QUuid& guid : *list) {
            if (mapsSet_->mapTypeByGuid(guid)) {
                return guid;
            }
        }
    }
    return QUuid();
}

// :219-239
void MapTypesMainList::buildMapsLists() {
    const auto mapsList = setBuilderFactory_->build(false);
    const auto layersList = setBuilderFactory_->build(false);
    for (int i = 0; i < fullMapsSet_->count(); ++i) {
        const IMapTypePtr map = fullMapsSet_->item(i);
        if (map->zmp()->isLayer()) {
            layersList->add(map);
        } else {
            mapsList->add(map);
        }
    }
    mapsSet_ = mapsList->makeAndClear();
    layersSet_ = layersList->makeAndClear();
    fullMapsSetChangeable_->setStatic(fullMapsSet_);
}

// :241-410. FEAT-NET-001
void MapTypesMainList::loadMaps(const std::shared_ptr<core::ILanguageManager>& languageManager,
                                const storage::MapVersionFactoryPtr& simpleVersionFactory,
                                const std::shared_ptr<storage::GlobalCacheConfig>& globalCacheConfig,
                                const storage::ITileStorageTypeListPtr& tileStorageTypeList,
                                core::Notifier& gcNotifier, const std::stop_token& appClosing,
                                const std::shared_ptr<net::InetConfig>& inetConfig,
                                const std::shared_ptr<const net::IGlobalDownloadConfig>& downloadConfig,
                                const std::shared_ptr<net::IDownloaderFactory>& downloaderFactory,
                                const std::shared_ptr<const core::ContentTypeRegistry>& contentTypes,
                                const std::shared_ptr<const ContentConverters>& converters,
                                const std::shared_ptr<const script::IProjConverterFactory>& projFactory,
                                int appId, const QString& logsPath,
                                const core::IConfigProviderPtr& localMapsConfig,
                                const core::IConfigProviderPtr& mapsListConfig,
                                const std::function<void(const QString&)>& showError) {
    int mapOnlyCount = 0;
    int mapTypeCount = 0;

    const std::vector<QUuid>& guids = zmpInfoSet_->guids(); // GetIterator
    for (const QUuid& guid : guids) {                       // :278-285
        const IZmpInfoPtr zmp = zmpInfoSet_->zmpByGuid(guid);
        if (!zmp) { // legacy: access violation; unreachable (the guids come from the same set)
            continue;
        }
        if (!zmp->isLayer()) {
            ++mapOnlyCount;
        }
        ++mapTypeCount;
    }

    if (mapTypeCount == 0) { // :287-289
        throw MapTypesNoMapsError(kErrNoMaps);
    }
    if (mapOnlyCount == 0) { // :291-293
        throw std::runtime_error(kErrMainMapNotExists);
    }

    const auto fullMapsList = setBuilderFactory_->build(false); // :296
    fullMapsList->setCapacity(mapTypeCount);

    mapOnlyCount = 0;
    mapTypeCount = 0;

    for (const QUuid& guid : guids) { // :302-376
        try {
            const IZmpInfoPtr zmp = zmpInfoSet_->zmpByGuid(guid);
            // Legacy dereferences a nil ALocalMapsConfig here: an access violation inside the guarded block.
            if (!localMapsConfig) {
                throw std::logic_error("MapTypesMainList: localMapsConfig is null");
            }
            const core::IConfigProviderPtr localMapConfig =
                localMapsConfig->subItem(guidString(guid)); // :305

            // not ported: G-7 proxies (maptype.md §2.14)
            const IMapTypePtr map = std::make_shared<MapType>(
                languageManager, zmp, simpleVersionFactory, globalCacheConfig, tileStorageTypeList,
                gcNotifier, appClosing, inetConfig, downloadConfig, downloaderFactory, contentTypes,
                converters, projFactory, localMapConfig, appId, logsPath);

            if (!map->zmp()->isLayer()) { // :336-339, before Add as in legacy
                ++mapOnlyCount;
            }
            ++mapTypeCount;

            fullMapsList->add(map); // :370
        } catch (const std::exception& e) {
            const QString message = QString::fromUtf8(e.what());
            qCWarning(logMapType) << "map not loaded:" << message;
            if (showError) {
                showError(message); // :373
            }
        }
    }

    if (mapTypeCount == 0) { // :378-380
        throw MapTypesNoMapsError(kErrNoMaps);
    }
    if (mapOnlyCount == 0) { // :382-384
        throw std::runtime_error(kErrMainMapNotExists);
    }

    fullMapsSet_ = fullMapsList->makeAndClear();

    buildMapsLists();

    guiConfigList_ = std::make_shared<MapTypeGuiConfigList>(languageManager, fullMapsSet_); // :390
    guiConfigList_->readConfig(mapsListConfig);                                             // :395

    // FEAT-NET-030: renumbering, 1..N in the menu order
    const MapGuidListPtr guidList = guiConfigList_->orderedMapGuidList(); // :397
    {
        const auto lock = guiConfigList_->writeLock(); // :400
        if (guidList) {
            int i = 0;
            for (const QUuid& guid : *guidList) {
                const IMapTypePtr map = fullMapsSet_->mapTypeByGuid(guid);
                if (map) { // legacy: no nil test; unreachable, the list is built from the same set
                    map->guiConfig()->setSortIndex(i + 1);
                }
                ++i;
            }
        }
    } // :408 unlock, then the batched notification
}

// :412-481
IMapTypePtr MapTypesMainList::nextMapWithTile(const geo::LocalCoordConverterPtr& view,
                                              const IMapTypePtr& activeMap, int step) const {
    if (step == 0) { // :432-435, no assert (shipped legacy has them off)
        return nullptr;
    }
    requireLoaded();
    const MapGuidListPtr guidList = guiConfigList_->orderedMapGuidList(); // :437
    if (!guidList || guidList->empty()) {
        return nullptr;
    }
    const int count = static_cast<int>(guidList->size());

    int mapIndex = -1; // :442
    if (activeMap) {   // legacy: access violation for a null active map (:444)
        const QUuid activeGuid = activeMap->guid();
        for (int i = 0; i < count; ++i) {
            if (activeGuid == (*guidList)[static_cast<std::size_t>(i)]) {
                mapIndex = i;
                break;
            }
        }
    }

    if (!view || !view->projection()) { // legacy: access violation (:450)
        return nullptr;
    }
    const geo::ProjectionPtr projection = view->projection();
    const geo::LonLat lonLat = view->centerLonLat();

    for (int i = 0; i < count; ++i) { // :453
        mapIndex += step;
        if (mapIndex < 0) {
            mapIndex = count - 1;
        } else if (mapIndex > count - 1) {
            mapIndex = 0;
        }

        const IMapTypePtr map =
            mapsSet_->mapTypeByGuid((*guidList)[static_cast<std::size_t>(mapIndex)]); // :464
        if (map && !map->zmp()->isLayer() && map->guiConfig()->enabled()) {           // :466
            const geo::ProjectionPtr mapProjection = map->projectionSet()->suitableProjection(*projection);
            if (!mapProjection) {
                continue;
            }
            const geo::PointI mapTile =
                geo::roundPoint(mapProjection->lonLat2TilePosFloat(lonLat), geo::PointRounding::ToTopLeft);
            const auto request = map->versionRequest()->getStatic();
            if (!request) {
                continue;
            }
            const auto tileInfo = map->tileStorage()->tileInfoEx(mapTile, mapProjection->zoom(), *request,
                                                                 storage::LoadMode::AsIs);
            if (tileInfo && tileInfo->isExists()) { // :475
                return map;
            }
        }
    }
    return nullptr;
}

// :483-507. FEAT-NET-030
void MapTypesMainList::saveMaps(const core::IConfigWriterPtr& localMapsConfig,
                                const core::IConfigWriterPtr& mapsListConfig) const {
    requireLoaded();
    const MapGuidListPtr guidList = guiConfigList_->orderedMapGuidList(); // :495
    if (guidList) {
        for (const QUuid& guid : *guidList) {
            const IMapTypePtr map = fullMapsSet_->mapTypeByGuid(guid); // :498
            if (!map) { // legacy: access violation; unreachable, the list is built from the same set
                continue;
            }
            // not ported: G-7 proxies (maptype.md §2.14)
            const core::IConfigWriterPtr subItem = localMapsConfig->orCreateSubItem(guidString(guid)); // :502
            map->saveConfig(subItem);
        }
    }
    guiConfigList_->writeConfig(mapsListConfig); // :506
}

} // namespace cg::maptype
