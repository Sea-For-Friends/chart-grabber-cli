// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Test-only IMapType for the region module tests (T-0164, reused by T-0165, T-0166, T-0168).
// Why a second fake: tests/maptype/fake_map_type.h is final, lives in the maptype test directory and answers
// null for the projection set and the version objects, which the region tests need. Everything answers
// null / empty / false except the public members below, which a test fills. Do not edit in later tickets.

#include "cg/maptype/map_type.h"

#include <QString>
#include <QUuid>

#include <functional>
#include <memory>

namespace cg::region::test {

class FakeMapType final : public maptype::IMapType {
public:
    QUuid guidValue;
    geo::ProjectionSetPtr projectionSetValue;
    std::shared_ptr<maptype::MapVersionFactoryChangeable> versionFactoryValue;
    std::shared_ptr<maptype::MapVersionRequestChangeable> versionRequestValue;
    std::shared_ptr<maptype::MapAbilitiesConfig> abilitiesValue;
    std::shared_ptr<maptype::MapTypeGuiConfig> guiConfigValue;
    std::shared_ptr<maptype::ITileDownloadSubsystem> tileDownloadSubsystemValue;
    std::shared_ptr<storage::ITileStorage> tileStorageValue;
    std::shared_ptr<storage::SimpleTileStorageConfig> storageConfigValue; // T-0181, additive
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfigValue; // T-0181, additive
    const core::ContentTypeInfo* contentTypeValue = nullptr;              // T-0183: answered by contentType()
    bool isBitmapTilesValue = false; // T-0183: answered by isBitmapTiles()
    std::function<QString(geo::PointI, quint8, const storage::MapVersionInfoPtr&)> getTileShowNameFn =
        [](geo::PointI, quint8, const storage::MapVersionInfoPtr&) { return QString(); };

    void saveConfig(const core::IConfigWriterPtr&) const override {}
    QUuid guid() const override { return guidValue; }
    void clearMemCache() override {}
    QString getTileShowName(geo::PointI xy, quint8 zoom, const storage::MapVersionInfoPtr& v) const override {
        return getTileShowNameFn(xy, zoom, v);
    }
    maptype::BitmapPtr loadTile(geo::PointI, quint8, const maptype::MapVersionRequestPtr&, bool,
                                const std::shared_ptr<maptype::ITileObjCacheBitmap>&) const override {
        return nullptr;
    }
    maptype::VectorSubsetPtr
    loadTileVector(geo::PointI, quint8, const maptype::MapVersionRequestPtr&, bool, bool,
                   const std::shared_ptr<maptype::ITileObjCacheVector>&) const override {
        return nullptr;
    }
    maptype::BitmapPtr loadTileUni(geo::PointI, const geo::ProjectionPtr&,
                                   const maptype::MapVersionRequestPtr&, bool, bool, bool,
                                   const std::shared_ptr<maptype::ITileObjCacheBitmap>&) const override {
        return nullptr;
    }
    maptype::BitmapPtr loadBitmap(geo::RectI, quint8, const maptype::MapVersionRequestPtr&, bool, bool, bool,
                                  const std::shared_ptr<maptype::ITileObjCacheBitmap>&) const override {
        return nullptr;
    }
    maptype::BitmapPtr loadBitmapUni(geo::RectI, const geo::ProjectionPtr&,
                                     const maptype::MapVersionRequestPtr&, bool, bool, bool,
                                     const std::shared_ptr<maptype::ITileObjCacheBitmap>&) const override {
        return nullptr;
    }
    QString shortFolderName() const override { return {}; }
    void nextVersion(const geo::LocalCoordConverterPtr&, int) override {}
    std::shared_ptr<const maptype::IZmpInfo> zmp() const override { return nullptr; }
    geo::ProjectionSetPtr projectionSet() const override { return projectionSetValue; }
    geo::ProjectionSetPtr viewProjectionSet() const override { return nullptr; }
    std::shared_ptr<maptype::MapVersionFactoryChangeable> versionFactory() const override {
        return versionFactoryValue;
    }
    std::shared_ptr<maptype::MapVersionRequestConfig> versionRequestConfig() const override {
        return nullptr;
    }
    std::shared_ptr<maptype::MapVersionRequestChangeable> versionRequest() const override {
        return versionRequestValue;
    }
    const core::ContentTypeInfo* contentType() const override { return contentTypeValue; }
    std::shared_ptr<maptype::MapAbilitiesConfig> abilities() const override { return abilitiesValue; }
    std::shared_ptr<storage::SimpleTileStorageConfig> storageConfig() const override {
        return storageConfigValue;
    }
    bool isBitmapTiles() const override { return isBitmapTilesValue; }
    bool isKmlTiles() const override { return false; }
    std::shared_ptr<maptype::ITileDownloadSubsystem> tileDownloadSubsystem() const override {
        return tileDownloadSubsystemValue;
    }
    std::shared_ptr<storage::ITileStorage> tileStorage() const override { return tileStorageValue; }
    std::shared_ptr<maptype::MapTypeGuiConfig> guiConfig() const override { return guiConfigValue; }
    std::shared_ptr<maptype::LayerDrawConfig> layerDrawConfig() const override { return nullptr; }
    std::shared_ptr<net::TileDownloaderConfig> tileDownloaderConfig() const override {
        return tileDownloaderConfigValue;
    }
    std::shared_ptr<maptype::TileDownloadRequestBuilderConfig>
    tileDownloadRequestBuilderConfig() const override {
        return nullptr;
    }
    std::shared_ptr<maptype::ITileObjCacheBitmap> cacheBitmap() const override { return nullptr; }
    std::shared_ptr<maptype::ITileObjCacheVector> cacheVector() const override { return nullptr; }
    std::shared_ptr<storage::TileInfoMemCache> cacheTileInfo() const override { return nullptr; }
};

} // namespace cg::region::test
