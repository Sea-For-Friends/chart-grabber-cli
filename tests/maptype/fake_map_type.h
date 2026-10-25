// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Test-only IMapType: carries a GUID and optionally a GUI config and a ZMP, everything else is empty. The
// methods whose types belong to modules that do not exist yet (bitmap, vector, tile caches, download
// subsystem) return null, as the declaration allows. It is also an IMapTypeProxy, initialized from the start
// (reset() makes it uninitialized), so that the list tests can exercise the skip of uninitialized proxies.

#include "cg/maptype/map_type.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/zmp_info.h"

#include <memory>
#include <utility>

namespace cg::maptype::test {

class FakeMapType final : public IMapTypeProxy {
public:
    // gui: the GUI config handed out by guiConfig(); zmp: the object handed out by zmp(). Null unless a test
    // needs one.
    explicit FakeMapType(QUuid guid, std::shared_ptr<MapTypeGuiConfig> gui = nullptr,
                         IZmpInfoPtr zmp = nullptr)
        : guid_(std::move(guid)), gui_(std::move(gui)), zmp_(std::move(zmp)) {}

    void initialize(const core::IConfigProviderPtr&) override { initialized_ = true; }
    void reset() override { initialized_ = false; }
    [[nodiscard]] bool isInitialized() const override { return initialized_; }

    void saveConfig(const core::IConfigWriterPtr&) const override {}
    QUuid guid() const override { return guid_; }
    void clearMemCache() override {}
    QString getTileShowName(geo::PointI, quint8, const storage::MapVersionInfoPtr&) const override {
        return {};
    }
    BitmapPtr loadTile(geo::PointI, quint8, const MapVersionRequestPtr&, bool,
                       const std::shared_ptr<ITileObjCacheBitmap>&) const override {
        return nullptr;
    }
    VectorSubsetPtr loadTileVector(geo::PointI, quint8, const MapVersionRequestPtr&, bool, bool,
                                   const std::shared_ptr<ITileObjCacheVector>&) const override {
        return nullptr;
    }
    BitmapPtr loadTileUni(geo::PointI, const geo::ProjectionPtr&, const MapVersionRequestPtr&, bool, bool,
                          bool, const std::shared_ptr<ITileObjCacheBitmap>&) const override {
        return nullptr;
    }
    BitmapPtr loadBitmap(geo::RectI, quint8, const MapVersionRequestPtr&, bool, bool, bool,
                         const std::shared_ptr<ITileObjCacheBitmap>&) const override {
        return nullptr;
    }
    BitmapPtr loadBitmapUni(geo::RectI, const geo::ProjectionPtr&, const MapVersionRequestPtr&, bool, bool,
                            bool, const std::shared_ptr<ITileObjCacheBitmap>&) const override {
        return nullptr;
    }
    QString shortFolderName() const override { return {}; }
    void nextVersion(const geo::LocalCoordConverterPtr&, int) override {}
    std::shared_ptr<const IZmpInfo> zmp() const override { return zmp_; }
    geo::ProjectionSetPtr projectionSet() const override { return nullptr; }
    geo::ProjectionSetPtr viewProjectionSet() const override { return nullptr; }
    std::shared_ptr<MapVersionFactoryChangeable> versionFactory() const override { return nullptr; }
    std::shared_ptr<MapVersionRequestConfig> versionRequestConfig() const override { return nullptr; }
    std::shared_ptr<MapVersionRequestChangeable> versionRequest() const override { return nullptr; }
    const core::ContentTypeInfo* contentType() const override { return nullptr; }
    std::shared_ptr<MapAbilitiesConfig> abilities() const override { return nullptr; }
    std::shared_ptr<storage::SimpleTileStorageConfig> storageConfig() const override { return nullptr; }
    bool isBitmapTiles() const override { return false; }
    bool isKmlTiles() const override { return false; }
    std::shared_ptr<ITileDownloadSubsystem> tileDownloadSubsystem() const override { return nullptr; }
    std::shared_ptr<storage::ITileStorage> tileStorage() const override { return nullptr; }
    std::shared_ptr<MapTypeGuiConfig> guiConfig() const override { return gui_; }
    std::shared_ptr<LayerDrawConfig> layerDrawConfig() const override { return nullptr; }
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig() const override { return nullptr; }
    std::shared_ptr<TileDownloadRequestBuilderConfig> tileDownloadRequestBuilderConfig() const override {
        return nullptr;
    }
    std::shared_ptr<ITileObjCacheBitmap> cacheBitmap() const override { return nullptr; }
    std::shared_ptr<ITileObjCacheVector> cacheVector() const override { return nullptr; }
    std::shared_ptr<storage::TileInfoMemCache> cacheTileInfo() const override { return nullptr; }

private:
    QUuid guid_;
    std::shared_ptr<MapTypeGuiConfig> gui_;
    IZmpInfoPtr zmp_;
    bool initialized_ = true;
};

inline IMapTypePtr fakeMap(const QUuid& guid) {
    return std::make_shared<FakeMapType>(guid);
}

inline IMapTypePtr fakeMapWithGui(const QUuid& guid, std::shared_ptr<MapTypeGuiConfig> gui) {
    return std::make_shared<FakeMapType>(guid, std::move(gui));
}

} // namespace cg::maptype::test
