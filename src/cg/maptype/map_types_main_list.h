// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MapTypesMainList.pas (TMapTypesMainList, EMapTypesNoMaps)
//
// The list of every map of the program: built once from the ZMP set (loadMaps), split into maps and layers,
// and the owner of the menu-order list (MapTypeGuiConfigList). A plain class in legacy (:62), so here.
//
// Constructor arguments of legacy (:84-92) that are not taken: the four resampler configs and
// APerfCounterList (:69-73; the resamplers come with the bitmap ticket).
// loadMaps arguments of legacy (:108-128) that are not declared, at their legacy positions:
// AMainMemCacheConfig (:111), AHashFunction (:114), ADownloaderThreadConfig (:119), ABitmap32StaticFactory
// (:121), AProjectionSetFactory (:123), AInvisibleBrowser (:124) -- MapType takes none of them
// (map_type.h). AMapVersionFactoryList (:110) is the already built simple version factory.

#include "cg/core/config_provider.h"
#include "cg/core/config_writer.h"
#include "cg/core/content_type.h"
#include "cg/core/language_manager.h"
#include "cg/core/notifier.h"
#include "cg/geo/local_coord_converter.h"
#include "cg/maptype/map_type.h"
#include "cg/maptype/map_type_gui_config_list.h"
#include "cg/maptype/map_type_set.h"
#include "cg/maptype/map_type_set_changeable.h"
#include "cg/maptype/map_type_set_changeable_simple.h"
#include "cg/maptype/zmp_info_set.h"

#include <QString>
#include <QUuid>

#include <functional>
#include <memory>
#include <stdexcept>
#include <stop_token>

namespace cg::maptype {

// EMapTypesNoMaps (:60)
class MapTypesNoMapsError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class MapTypesMainList final {
public:
    // :157-181
    MapTypesMainList(std::shared_ptr<const IMapTypeSetBuilderFactory> setBuilderFactory,
                     std::shared_ptr<const IZmpInfoSet> zmpInfoSet);

    // :241-410. `showError` is called once per map that failed to build, with the exception's message.
    // MapTypesNoMapsError: no map was built; std::runtime_error: no non-layer map was built.
    // FEAT-NET-001 (contributes; held open)
    void loadMaps(const std::shared_ptr<core::ILanguageManager>& languageManager,
                  const storage::MapVersionFactoryPtr& simpleVersionFactory,
                  const std::shared_ptr<storage::GlobalCacheConfig>& globalCacheConfig,
                  const storage::ITileStorageTypeListPtr& tileStorageTypeList, core::Notifier& gcNotifier,
                  const std::stop_token& appClosing, const std::shared_ptr<net::InetConfig>& inetConfig,
                  const std::shared_ptr<const net::IGlobalDownloadConfig>& downloadConfig,
                  const std::shared_ptr<net::IDownloaderFactory>& downloaderFactory,
                  const std::shared_ptr<const core::ContentTypeRegistry>& contentTypes,
                  const std::shared_ptr<const ContentConverters>& converters,
                  const std::shared_ptr<const script::IProjConverterFactory>& projFactory, int appId,
                  const QString& logsPath, const core::IConfigProviderPtr& localMapsConfig,
                  const core::IConfigProviderPtr& mapsListConfig,
                  const std::function<void(const QString&)>& showError);

    // :483-507. FEAT-NET-030 (contributes; held open)
    void saveMaps(const core::IConfigWriterPtr& localMapsConfig,
                  const core::IConfigWriterPtr& mapsListConfig) const;

    // :412-481. step 0, a null view: null.
    [[nodiscard]] IMapTypePtr nextMapWithTile(const geo::LocalCoordConverterPtr& view,
                                              const IMapTypePtr& activeMap, int step) const;

    [[nodiscard]] std::shared_ptr<IMapTypeSetChangeable> fullMapsSetChangeable() const {
        return fullMapsSetChangeable_;
    }
    [[nodiscard]] IMapTypeSetPtr fullMapsSet() const { return fullMapsSet_; }
    [[nodiscard]] IMapTypeSetPtr mapsSet() const { return mapsSet_; }     // non-layers
    [[nodiscard]] IMapTypeSetPtr layersSet() const { return layersSet_; } // null when no layer
    // :202-217, :183-200. std::logic_error before loadMaps.
    [[nodiscard]] QUuid firstMainMapGuid() const;
    [[nodiscard]] QUuid defaultMainMapGuid() const;
    [[nodiscard]] std::shared_ptr<MapTypeGuiConfigList> guiConfigList() const { return guiConfigList_; }

private:
    void buildMapsLists(); // :219-239
    void requireLoaded() const;

    std::shared_ptr<const IMapTypeSetBuilderFactory> setBuilderFactory_;
    std::shared_ptr<MapTypeGuiConfigList> guiConfigList_;
    std::shared_ptr<const IZmpInfoSet> zmpInfoSet_;
    IMapTypeSetPtr fullMapsSet_;
    std::shared_ptr<MapTypeSetChangeableSimple> fullMapsSetChangeable_;
    IMapTypeSetPtr mapsSet_;
    IMapTypeSetPtr layersSet_;
};

} // namespace cg::maptype
