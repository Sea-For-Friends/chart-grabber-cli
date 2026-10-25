// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ZmpInfo.pas (IZmpInfoGUI, IZmpInfo; the proxy interfaces :139-161 are runtime, spec 2.14),
//        u_ZmpInfo.pas (TZmpInfoGUI, TZmpInfo, EZmp*), including LoadVectorAppearanceConfig (:1001-1062).
//
// One map definition loaded from its config provider (the ZMP container). Immutable after
// construction; the constructors throw ZmpError (or std::runtime_error for a bad
// IteratorSubRectSize) exactly where legacy raises, so that ZmpInfoSet can mirror its handler.

#include "cg/core/config_provider.h"
#include "cg/core/content_type.h"
#include "cg/core/content_type_subst.h"
#include "cg/core/language_manager.h"
#include "cg/core/string_by_language.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/map_abilities_config.h"
#include "cg/maptype/map_coverage_info.h"
#include "cg/maptype/tile_download_request_builder_config.h"
#include "cg/maptype/tile_post_download_crop_config.h"
#include "cg/maptype/zmp_config.h"
#include "cg/net/tile_downloader_config.h"
#include "cg/storage/simple_tile_storage_config.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/import_config.h"
#include "cg/vector/mark_picture.h"

#include <QImage>
#include <QString>
#include <QUuid>
#include <QtGlobal>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace cg::maptype {

class IZmpInfoGui {
public:
    virtual ~IZmpInfoGui() = default;
    [[nodiscard]] virtual core::IStringByLanguagePtr name() const = 0;
    [[nodiscard]] virtual int sortIndex() const = 0;  // key pnum, default -1
    [[nodiscard]] virtual quint16 hotKey() const = 0; // Delphi TShortCut (Word)
    [[nodiscard]] virtual bool separator() const = 0;
    [[nodiscard]] virtual core::IStringByLanguagePtr parentSubMenu() const = 0;
    [[nodiscard]] virtual bool enabled() const = 0;
    [[nodiscard]] virtual core::IStringByLanguagePtr infoUrl() const = 0;
    [[nodiscard]] virtual QImage bmp18() const = 0;
    [[nodiscard]] virtual QImage bmp24() const = 0;
};

class IZmpInfo { // i_ZmpInfo.pas:74-137
public:
    virtual ~IZmpInfo() = default;
    [[nodiscard]] virtual QUuid guid() const = 0;
    [[nodiscard]] virtual bool isLayer() const = 0;
    [[nodiscard]] virtual QString fileName() const = 0; // relative name given by the set
    [[nodiscard]] virtual core::IStringByLanguagePtr license() const = 0;
    [[nodiscard]] virtual std::shared_ptr<const IZmpInfoGui> gui() const = 0;
    [[nodiscard]] virtual int layerZOrder() const = 0;
    [[nodiscard]] virtual QString version() const = 0;
    [[nodiscard]] virtual TileDownloadRequestBuilderConfigStaticPtr
    tileDownloadRequestBuilderConfig() const = 0;
    [[nodiscard]] virtual std::shared_ptr<const net::TileDownloaderSettings> tileDownloaderConfig() const = 0;
    [[nodiscard]] virtual TilePostDownloadCropConfigStaticPtr tilePostDownloadCropConfig() const = 0;
    [[nodiscard]] virtual const core::ContentTypeSubst& contentTypeSubst() const = 0;
    [[nodiscard]] virtual geo::ProjectionSetPtr projectionSet() const = 0;
    [[nodiscard]] virtual geo::ProjectionSetPtr viewProjectionSet() const = 0;
    [[nodiscard]] virtual MapAbilitiesConfigStaticPtr abilities() const = 0;
    [[nodiscard]] virtual const std::vector<QByteArray>& emptyTileSamples() const = 0;
    [[nodiscard]] virtual const std::vector<QByteArray>& banTileSamples() const = 0;
    [[nodiscard]] virtual storage::SimpleTileStorageConfigStaticPtr storageConfig() const = 0;
    [[nodiscard]] virtual core::IConfigProviderPtr dataProvider() const = 0; // the container
    [[nodiscard]] virtual MapCoverageInfoPtr coverage() const = 0;
    // i_ZmpInfo.pas:117-124; each is null when the ZMP has no such sub-item under PARAMS/Vector.
    [[nodiscard]] virtual vector::ImportPointParamsPtr pointParams() const = 0;
    [[nodiscard]] virtual vector::ImportLineParamsPtr lineParams() const = 0;
    [[nodiscard]] virtual vector::ImportPolyParamsPtr polyParams() const = 0;
};
using IZmpInfoPtr = std::shared_ptr<const IZmpInfo>;

// EZmpError and its three subclasses (u_ZmpInfo.pas:213-216), plus UnknownContentType: the
// OQ-3 rejection, which legacy does not have (D-74 in the handoff).
class ZmpError : public std::runtime_error {
public:
    enum class Kind : std::uint8_t { IniNotFound, ParamsNotFound, Guid, UnknownContentType };
    ZmpError(Kind kind, const QString& message);
    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] QString message() const { return message_; }

private:
    Kind kind_;
    QString message_;
};

// Mapping note: legacy's AGUID and AConfigIni (u_ZmpInfo.pas:297-311) are dropped, nothing reads them;
// ABitmapFactory became QImage.
class ZmpInfoGui final : public IZmpInfoGui { // TZmpInfoGUI, u_ZmpInfo.pas:53-98
public:
    ZmpInfoGui(const core::ILanguageManager& languageManager, const core::ContentTypeRegistry& contentTypes,
               const core::IConfigProviderPtr& config, const core::IConfigProviderPtr& configIniParams,
               int pnum);

    [[nodiscard]] core::IStringByLanguagePtr name() const override { return name_; }
    [[nodiscard]] int sortIndex() const override { return sortIndex_; }
    [[nodiscard]] quint16 hotKey() const override { return hotKey_; }
    [[nodiscard]] bool separator() const override { return separator_; }
    [[nodiscard]] core::IStringByLanguagePtr parentSubMenu() const override { return parentSubMenu_; }
    [[nodiscard]] bool enabled() const override { return enabled_; }
    [[nodiscard]] core::IStringByLanguagePtr infoUrl() const override { return infoUrl_; }
    [[nodiscard]] QImage bmp18() const override { return bmp18_; }
    [[nodiscard]] QImage bmp24() const override { return bmp24_; }

private:
    void loadUIParams(const core::ILanguageListStatic& langList, const core::IConfigProvider& params,
                      int pnum);
    void loadIcons(const core::ContentTypeRegistry& contentTypes, const core::IConfigProvider& config,
                   const core::IConfigProvider& params, int pnum);
    void loadInfo(const core::ILanguageListStatic& langList, const core::IConfigProvider& config);

    core::IStringByLanguagePtr name_;
    core::IStringByLanguagePtr parentSubMenu_;
    core::IStringByLanguagePtr infoUrl_;
    int sortIndex_ = -1;
    QImage bmp18_;
    QImage bmp24_;
    quint16 hotKey_ = 0;
    bool separator_ = false;
    bool enabled_ = true;
};

// FEAT-NET-002
class ZmpInfo final : public IZmpInfo { // TZmpInfo, u_ZmpInfo.pas:100-207
public:
    // Throws ZmpError (u_ZmpInfo.pas:585-624, 833-839; UnknownContentType is the OQ-3 addition) or
    // std::runtime_error (IteratorSubRectSize, LD-7).
    // `markPictureList` may be null (readAppearancePoint then finds no picture).
    ZmpInfo(const ZmpConfig& zmpConfig, const core::ILanguageManager& languageManager,
            const geo::ProjectionSetFactory& projectionSetFactory,
            const core::ContentTypeRegistry& contentTypes,
            const vector::IAppearanceOfMarkFactory& appearanceOfMarkFactory,
            const vector::MarkPictureListPtr& markPictureList, QString fileName,
            core::IConfigProviderPtr config, int pnum);

    [[nodiscard]] QUuid guid() const override { return guid_; }
    [[nodiscard]] bool isLayer() const override { return isLayer_; }
    [[nodiscard]] QString fileName() const override { return fileName_; }
    [[nodiscard]] core::IStringByLanguagePtr license() const override { return license_; }
    [[nodiscard]] std::shared_ptr<const IZmpInfoGui> gui() const override { return gui_; }
    [[nodiscard]] int layerZOrder() const override { return layerZOrder_; }
    [[nodiscard]] QString version() const override { return version_; }
    [[nodiscard]] TileDownloadRequestBuilderConfigStaticPtr
    tileDownloadRequestBuilderConfig() const override {
        return requestBuilderConfig_;
    }
    [[nodiscard]] std::shared_ptr<const net::TileDownloaderSettings> tileDownloaderConfig() const override {
        return downloaderConfig_;
    }
    [[nodiscard]] TilePostDownloadCropConfigStaticPtr tilePostDownloadCropConfig() const override {
        return cropConfig_;
    }
    [[nodiscard]] const core::ContentTypeSubst& contentTypeSubst() const override {
        return contentTypeSubst_;
    }
    [[nodiscard]] geo::ProjectionSetPtr projectionSet() const override { return projectionSet_; }
    [[nodiscard]] geo::ProjectionSetPtr viewProjectionSet() const override { return viewProjectionSet_; }
    [[nodiscard]] MapAbilitiesConfigStaticPtr abilities() const override { return abilities_; }
    [[nodiscard]] const std::vector<QByteArray>& emptyTileSamples() const override {
        return emptyTileSamples_;
    }
    [[nodiscard]] const std::vector<QByteArray>& banTileSamples() const override { return banTileSamples_; }
    [[nodiscard]] storage::SimpleTileStorageConfigStaticPtr storageConfig() const override {
        return storageConfig_;
    }
    [[nodiscard]] core::IConfigProviderPtr dataProvider() const override { return config_; }
    [[nodiscard]] MapCoverageInfoPtr coverage() const override { return coverage_; }
    [[nodiscard]] vector::ImportPointParamsPtr pointParams() const override { return pointParams_; }
    [[nodiscard]] vector::ImportLineParamsPtr lineParams() const override { return lineParams_; }
    [[nodiscard]] vector::ImportPolyParamsPtr polyParams() const override { return polyParams_; }

private:
    void loadConfig(const ZmpConfig& zmpConfig, const geo::ProjectionSetFactory& projectionSetFactory,
                    const vector::IAppearanceOfMarkFactory& appearanceOfMarkFactory,
                    const vector::MarkPictureListPtr& markPictureList);
    void loadVectorAppearanceConfig(const vector::IAppearanceOfMarkFactory& appearanceOfMarkFactory,
                                    const vector::MarkPictureListPtr& markPictureList,
                                    const core::IConfigProvider& config);
    void loadCropConfig(const core::IConfigProvider& params);
    void loadAbilities(const core::IConfigProvider& params);
    void loadCoverageInfo(const core::IConfigProvider& params);
    void loadStorageConfig(const ZmpConfig& zmpConfig, const core::IConfigProvider& params);
    [[nodiscard]] QUuid loadGuid(const core::IConfigProvider& params) const;
    void loadVersion(const core::IConfigProvider& params);
    void loadSamples(const core::IConfigProvider& config);
    void loadProjectionInfo(const core::IConfigProvider& configIni, const geo::ProjectionSetFactory& factory);
    void loadTileRequestBuilderConfig(const core::IConfigProvider& params);
    void loadTileDownloaderConfig(const ZmpConfig& zmpConfig, const core::IConfigProvider& params);

    QUuid guid_;
    bool isLayer_ = false;
    int layerZOrder_ = 0;
    core::IStringByLanguagePtr license_;
    QString fileName_;
    QString version_;
    TileDownloadRequestBuilderConfigStaticPtr requestBuilderConfig_;
    std::shared_ptr<const net::TileDownloaderSettings> downloaderConfig_;
    TilePostDownloadCropConfigStaticPtr cropConfig_;
    core::ContentTypeSubst contentTypeSubst_;
    geo::ProjectionSetPtr projectionSet_;
    geo::ProjectionSetPtr viewProjectionSet_;
    std::shared_ptr<const IZmpInfoGui> gui_;
    MapAbilitiesConfigStaticPtr abilities_;
    std::vector<QByteArray> emptyTileSamples_;
    std::vector<QByteArray> banTileSamples_;
    storage::SimpleTileStorageConfigStaticPtr storageConfig_;
    MapCoverageInfoPtr coverage_;
    vector::ImportPointParamsPtr pointParams_;
    vector::ImportLineParamsPtr lineParams_;
    vector::ImportPolyParamsPtr polyParams_;
    core::IConfigProviderPtr config_;
    core::IConfigProviderPtr configIni_;
    core::IConfigProviderPtr configIniParams_;
};

} // namespace cg::maptype
