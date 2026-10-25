// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapType.pas (IMapType, IMapTypeProxy, IMapTypeChangeable)
//
// One method per legacy method, in legacy order (i_MapType.pas:55-172). The interface is
// declared only: types that belong to modules that do not exist yet (bitmap, tile object
// caches, the download subsystem, IZmpInfo of the ZmpInfo ticket) are forward-declared; the
// header does not link them. Legacy "= nil" defaults of the cache arguments become explicit
// arguments at the call site (no default arguments on virtual functions).

#include "cg/core/changeable.h"
#include "cg/core/config_provider.h"
#include "cg/core/config_writer.h"
#include "cg/core/content_type.h"
#include "cg/core/notifier.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/local_coord_converter.h"
#include "cg/geo/projection.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/content_converters.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_storage_type_list.h"

#include <QString>
#include <QUuid>
#include <QtGlobal>

#include <memory>
#include <stop_token>

namespace cg::bitmap {
class IBitmap32Static; // module `bitmap` (spec maptype.md section 2.11.0)
}
namespace cg::vector {
class IVectorItemSubset;
}
namespace cg::core {
class ILanguageManager;
}
namespace cg::net {
class TileDownloaderConfig;
class InetConfig;
class IGlobalDownloadConfig;
class IDownloaderFactory;
} // namespace cg::net
namespace cg::script {
class IProjConverterFactory;
}
namespace cg::storage {
class ITileStorage;
class SimpleTileStorageConfig;
class TileInfoMemCache;
class GlobalCacheConfig;
} // namespace cg::storage

namespace cg::maptype {

class IZmpInfo;            // zmp_info.h, ZmpInfo ticket
class ITileObjCacheBitmap; // i_TileObjCache.pas, mem_tile_cache.h (after `bitmap`)
class ITileObjCacheVector;
class ITileDownloadSubsystem; // tile_download_subsystem.h (spec section 2.13)
class MapVersionFactoryChangeable;
class MapVersionRequestConfig;
class MapVersionRequestChangeable;
class MapAbilitiesConfig;
class MapTypeGuiConfig;
class LayerDrawConfig;
class TileDownloadRequestBuilderConfig;
class TileDownloadSubsystem; // tile_download_subsystem.h

using BitmapPtr = std::shared_ptr<const bitmap::IBitmap32Static>;
using VectorSubsetPtr = std::shared_ptr<const vector::IVectorItemSubset>;
// storage/map_version.h has no alias for the request; this is what
// MapVersionRequestChangeable::getStatic() returns.
using MapVersionRequestPtr = std::shared_ptr<const storage::MapVersionRequest>;

class IMapType {
public:
    virtual ~IMapType() = default;

    virtual void saveConfig(const core::IConfigWriterPtr& localConfig) const = 0;
    [[nodiscard]] virtual QUuid guid() const = 0;
    virtual void clearMemCache() = 0;
    [[nodiscard]] virtual QString getTileShowName(geo::PointI xy, quint8 zoom,
                                                  const storage::MapVersionInfoPtr& version) const = 0;
    [[nodiscard]] virtual BitmapPtr loadTile(geo::PointI xy, quint8 zoom, const MapVersionRequestPtr& version,
                                             bool ignoreError,
                                             const std::shared_ptr<ITileObjCacheBitmap>& cache) const = 0;
    [[nodiscard]] virtual VectorSubsetPtr
    loadTileVector(geo::PointI xy, quint8 zoom, const MapVersionRequestPtr& version, bool usePre,
                   bool ignoreError, const std::shared_ptr<ITileObjCacheVector>& cache) const = 0;
    [[nodiscard]] virtual BitmapPtr loadTileUni(geo::PointI xy, const geo::ProjectionPtr& projection,
                                                const MapVersionRequestPtr& version, bool usePre,
                                                bool allowPartial, bool ignoreError,
                                                const std::shared_ptr<ITileObjCacheBitmap>& cache) const = 0;
    [[nodiscard]] virtual BitmapPtr loadBitmap(geo::RectI pixelRectTarget, quint8 zoom,
                                               const MapVersionRequestPtr& version, bool usePre,
                                               bool allowPartial, bool ignoreError,
                                               const std::shared_ptr<ITileObjCacheBitmap>& cache) const = 0;
    [[nodiscard]] virtual BitmapPtr
    loadBitmapUni(geo::RectI pixelRectTarget, const geo::ProjectionPtr& projection,
                  const MapVersionRequestPtr& version, bool usePre, bool allowPartial, bool ignoreError,
                  const std::shared_ptr<ITileObjCacheBitmap>& cache) const = 0;
    [[nodiscard]] virtual QString shortFolderName() const = 0;
    virtual void nextVersion(const geo::LocalCoordConverterPtr& view, int step) = 0;
    [[nodiscard]] virtual std::shared_ptr<const IZmpInfo> zmp() const = 0;
    [[nodiscard]] virtual geo::ProjectionSetPtr projectionSet() const = 0;
    [[nodiscard]] virtual geo::ProjectionSetPtr viewProjectionSet() const = 0;
    [[nodiscard]] virtual std::shared_ptr<MapVersionFactoryChangeable> versionFactory() const = 0;
    [[nodiscard]] virtual std::shared_ptr<MapVersionRequestConfig> versionRequestConfig() const = 0;
    [[nodiscard]] virtual std::shared_ptr<MapVersionRequestChangeable> versionRequest() const = 0;
    // Entry of the content-type registry (owned by it, immutable); null when the type is unknown.
    // Lifetime: the pointer belongs to the content-type registry, which must outlive the map.
    // The spec's `core::ContentTypeInfoPtr` does not exist in core; the landed type is used.
    [[nodiscard]] virtual const core::ContentTypeInfo* contentType() const = 0;
    [[nodiscard]] virtual std::shared_ptr<MapAbilitiesConfig> abilities() const = 0;
    [[nodiscard]] virtual std::shared_ptr<storage::SimpleTileStorageConfig> storageConfig() const = 0;
    [[nodiscard]] virtual bool isBitmapTiles() const = 0;
    [[nodiscard]] virtual bool isKmlTiles() const = 0;
    [[nodiscard]] virtual std::shared_ptr<ITileDownloadSubsystem> tileDownloadSubsystem() const = 0;
    [[nodiscard]] virtual std::shared_ptr<storage::ITileStorage> tileStorage() const = 0;
    [[nodiscard]] virtual std::shared_ptr<MapTypeGuiConfig> guiConfig() const = 0;
    [[nodiscard]] virtual std::shared_ptr<LayerDrawConfig> layerDrawConfig() const = 0;
    [[nodiscard]] virtual std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig() const = 0;
    [[nodiscard]] virtual std::shared_ptr<TileDownloadRequestBuilderConfig>
    tileDownloadRequestBuilderConfig() const = 0;
    [[nodiscard]] virtual std::shared_ptr<ITileObjCacheBitmap> cacheBitmap() const = 0;
    [[nodiscard]] virtual std::shared_ptr<ITileObjCacheVector> cacheVector() const = 0;
    [[nodiscard]] virtual std::shared_ptr<storage::TileInfoMemCache> cacheTileInfo() const = 0;
};
using IMapTypePtr = std::shared_ptr<IMapType>; // not const: maps are mutable service objects

// Declared here because MapTypeGuiConfigList and MapTypesMainList test for it
// (dynamic_cast<const IMapTypeProxy*>).
class IMapTypeProxy : public IMapType {
public:
    virtual void initialize(const core::IConfigProviderPtr& zmpMapConfig) = 0;
    virtual void reset() = 0;
    [[nodiscard]] virtual bool isInitialized() const = 0;
};

class IMapTypeChangeable : public core::Changeable {
public:
    [[nodiscard]] virtual IMapTypePtr getStatic() const = 0;
};

// Ports u_MapType.pas:1-506, :555-683, :790-835 -- the non-bitmap half of TMapType (docs/specs/maptype.md
// 2.11.6, ticket T-0140). The bitmap half (the two memory caches, the loaders from storage, the seven load...
// methods, :507-553, :684-789, :837-1184) is the tile-loading ticket's: those methods answer nullptr.
//
// Constructor arguments of the legacy signature (u_MapType.pas:298-326) that are NOT taken, at their legacy
// positions: AMapVersionFactoryList (ruled deleted: the simple factory is passed directly),
// AMainMemCacheConfig, AResamplerLoad / AResamplerGetPrev / AResamplerChangeProjection / AResamplerDownload /
// ABitmap32StaticFactory (bitmap half), AHashFunction (free functions), ADownloaderThreadConfig (:450, :455),
// AProjectionSetFactory
// (:453), AInvisibleBrowser (:461, replaced by the anti-ban through the downloader factory, D-14),
// APerfCounterList (core.md 1.3) -- the three named in the middle are consumed by nothing the landed
// TileDownloadSubsystem takes; R-12 (MapTypesMainList) must not pass them.
//
// Members are declared in legacy field order (u_MapType.pas:86-114); C++ destroys them in reverse, so the
// download subsystem (declared last) is destroyed first: its destructor joins the workers while the storage,
// the configs and the content-type registry it uses are alive. Every cross-reference between the members is a
// shared_ptr, so the order is not what keeps them alive (the subsystem holds the storage, the storage holds
// the configs, the factory and the mem-cache).
//
// A MapType is destroyed on a thread that is not a download worker, after the app-closing token has fired
// (spec 3.1 R1). The notifier callbacks of this class (version change, GC tick) capture `this` / a shared
// cache and are removed first in the destructor; a callback already running on another thread is not waited
// for (core/notifier.cpp), as in legacy.
class MapType final : public IMapType {
public:
    // `contentTypes` must be the registry the ZMP was built with (D-74): an unresolvable content type is a
    // programmer error (std::logic_error), not a user-facing rejection. `localConfig` (the Maps.ini section
    // of the map) may be null. Null `zmp` / `contentTypes` are std::invalid_argument.
    MapType(std::shared_ptr<core::ILanguageManager> languageManager, std::shared_ptr<const IZmpInfo> zmp,
            storage::MapVersionFactoryPtr simpleVersionFactory,
            std::shared_ptr<storage::GlobalCacheConfig> globalCacheConfig,
            storage::ITileStorageTypeListPtr tileStorageTypeList, core::Notifier& gcNotifier,
            std::stop_token appClosing, std::shared_ptr<net::InetConfig> inetConfig,
            std::shared_ptr<const net::IGlobalDownloadConfig> downloadConfig,
            std::shared_ptr<net::IDownloaderFactory> downloaderFactory,
            std::shared_ptr<const core::ContentTypeRegistry> contentTypes,
            std::shared_ptr<const ContentConverters> converters,
            std::shared_ptr<const script::IProjConverterFactory> projFactory,
            const core::IConfigProviderPtr& localConfig, int appId, const QString& logsPath);
    ~MapType() override;
    MapType(const MapType&) = delete;
    MapType& operator=(const MapType&) = delete;

    void saveConfig(const core::IConfigWriterPtr& localConfig) const override;
    [[nodiscard]] QUuid guid() const override;
    void clearMemCache() override;
    [[nodiscard]] QString getTileShowName(geo::PointI xy, quint8 zoom,
                                          const storage::MapVersionInfoPtr& version) const override;
    // TODO(T-0148): tile loading (u_MapType.pas:507-553, 684-789, 837-1184); nullptr is the legacy "no tile".
    [[nodiscard]] BitmapPtr loadTile(geo::PointI xy, quint8 zoom, const MapVersionRequestPtr& version,
                                     bool ignoreError,
                                     const std::shared_ptr<ITileObjCacheBitmap>& cache) const override;
    [[nodiscard]] VectorSubsetPtr
    loadTileVector(geo::PointI xy, quint8 zoom, const MapVersionRequestPtr& version, bool usePre,
                   bool ignoreError, const std::shared_ptr<ITileObjCacheVector>& cache) const override;
    [[nodiscard]] BitmapPtr loadTileUni(geo::PointI xy, const geo::ProjectionPtr& projection,
                                        const MapVersionRequestPtr& version, bool usePre, bool allowPartial,
                                        bool ignoreError,
                                        const std::shared_ptr<ITileObjCacheBitmap>& cache) const override;
    [[nodiscard]] BitmapPtr loadBitmap(geo::RectI pixelRectTarget, quint8 zoom,
                                       const MapVersionRequestPtr& version, bool usePre, bool allowPartial,
                                       bool ignoreError,
                                       const std::shared_ptr<ITileObjCacheBitmap>& cache) const override;
    [[nodiscard]] BitmapPtr loadBitmapUni(geo::RectI pixelRectTarget, const geo::ProjectionPtr& projection,
                                          const MapVersionRequestPtr& version, bool usePre, bool allowPartial,
                                          bool ignoreError,
                                          const std::shared_ptr<ITileObjCacheBitmap>& cache) const override;
    [[nodiscard]] QString shortFolderName() const override;
    void nextVersion(const geo::LocalCoordConverterPtr& view, int step) override;
    [[nodiscard]] std::shared_ptr<const IZmpInfo> zmp() const override;
    [[nodiscard]] geo::ProjectionSetPtr projectionSet() const override;
    [[nodiscard]] geo::ProjectionSetPtr viewProjectionSet() const override;
    [[nodiscard]] std::shared_ptr<MapVersionFactoryChangeable> versionFactory() const override;
    [[nodiscard]] std::shared_ptr<MapVersionRequestConfig> versionRequestConfig() const override;
    [[nodiscard]] std::shared_ptr<MapVersionRequestChangeable> versionRequest() const override;
    [[nodiscard]] const core::ContentTypeInfo* contentType() const override;
    [[nodiscard]] std::shared_ptr<MapAbilitiesConfig> abilities() const override;
    [[nodiscard]] std::shared_ptr<storage::SimpleTileStorageConfig> storageConfig() const override;
    [[nodiscard]] bool isBitmapTiles() const override;
    [[nodiscard]] bool isKmlTiles() const override;
    [[nodiscard]] std::shared_ptr<ITileDownloadSubsystem> tileDownloadSubsystem() const override;
    [[nodiscard]] std::shared_ptr<storage::ITileStorage> tileStorage() const override;
    [[nodiscard]] std::shared_ptr<MapTypeGuiConfig> guiConfig() const override;
    [[nodiscard]] std::shared_ptr<LayerDrawConfig> layerDrawConfig() const override;
    [[nodiscard]] std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig() const override;
    [[nodiscard]] std::shared_ptr<TileDownloadRequestBuilderConfig>
    tileDownloadRequestBuilderConfig() const override;
    [[nodiscard]] std::shared_ptr<ITileObjCacheBitmap> cacheBitmap() const override;
    [[nodiscard]] std::shared_ptr<ITileObjCacheVector> cacheVector() const override;
    [[nodiscard]] std::shared_ptr<storage::TileInfoMemCache> cacheTileInfo() const override;

private:
    // Legacy field order, u_MapType.pas:86-114 (fields of the bitmap half are added by its ticket).
    std::shared_ptr<const IZmpInfo> zmp_;
    QString mapDataUrlPrefix_; // KML maps only (:431); read by the tile-loading half
    std::shared_ptr<storage::TileInfoMemCache> cacheTileInfo_;
    // cacheBitmap_ / cacheVector_: bitmap half (TODO(T-0148)); null until then, as for a map of the other
    // kind.
    std::shared_ptr<storage::ITileStorage> storage_;
    // Not ported until T-0148: FBitmapLoaderFromStorage, FBitmapSaverToStorage, FKmlLoaderFromStorage (their
    // only non-load users are the two predicates, which the registry's content kind answers),
    // FVectorDataFactory, FVectorItemSubsetBuilderFactory (:432-440; classes landed in T-0129 / T-0057).
    geo::ProjectionSetPtr projectionSet_;
    geo::ProjectionSetPtr viewProjectionSet_;
    int loadPrevMaxZoomDelta_ = 0;                                  // :475-483; read by the tile-loading half
    std::shared_ptr<const core::ContentTypeRegistry> contentTypes_; // keeps contentType_ alive
    const core::ContentTypeInfo* contentType_ = nullptr;
    std::shared_ptr<MapVersionFactoryChangeable> versionFactory_;
    std::shared_ptr<MapVersionRequestConfig> versionRequestConfig_;
    std::shared_ptr<MapVersionRequestChangeable> versionRequest_;
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig_;
    std::shared_ptr<TileDownloadRequestBuilderConfig> tileDownloadRequestBuilderConfig_;
    std::shared_ptr<MapTypeGuiConfig> guiConfig_;
    std::shared_ptr<LayerDrawConfig> layerDrawConfig_;
    std::shared_ptr<MapAbilitiesConfig> abilitiesConfig_;
    std::shared_ptr<storage::SimpleTileStorageConfig> storageConfig_;
    std::shared_ptr<TileDownloadSubsystem> tileDownloadSubsystem_;
    // FVersionChangeListener (:112): removed first in the destructor body (:488-492).
    core::Subscription versionChangeSubscription_;
    // The TTL sweep of the tile-info cache. Legacy's cache subscribes itself to the GC notifier
    // (u_TileInfoBasicMemCache.pas:180-183); the landed cache has no timer (its header), so the owner does.
    core::Subscription cacheTtlSubscription_;
};

} // namespace cg::maptype
