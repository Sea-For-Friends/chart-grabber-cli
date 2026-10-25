// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_ImportConfig.pas, u_ImportConfig.pas
//
// FEAT-GEO-037 (enabler). Plain value holders; no hash and no IsEqual exist in legacy.
// The params constructors cross-cast the appearance once. A null appearance gives null facets;
// an appearance of the wrong kind throws std::invalid_argument (legacy: Assert in debug,
// EIntfCastError from `as` in release; L-131).
// ImportLineParams and ImportPolyParams store isForceLineWidth into isForceLineColor too (L-130).

#include "cg/vector/appearance.h"
#include "cg/vector/appearance_of_vector_item.h"
#include "cg/vector/category.h"

#include <memory>

namespace cg::vector {

class IImportMarkParams {
public:
    virtual ~IImportMarkParams() = default;
    [[nodiscard]] virtual AppearancePtr appearance() const = 0;
};

class IImportPointParams : public IImportMarkParams {
public:
    [[nodiscard]] virtual AppearancePointCaptionPtr captionAppearance() const = 0;
    [[nodiscard]] virtual AppearancePointIconPtr iconAppearance() const = 0;
    [[nodiscard]] virtual bool isForceTextColor() const = 0;
    [[nodiscard]] virtual bool isForceTextBgColor() const = 0;
    [[nodiscard]] virtual bool isForceFontSize() const = 0;
    [[nodiscard]] virtual bool isForceMarkerSize() const = 0;
    [[nodiscard]] virtual bool isForcePicName() const = 0;
};
using ImportPointParamsPtr = std::shared_ptr<const IImportPointParams>;

class IImportLineParams : public IImportMarkParams {
public:
    [[nodiscard]] virtual AppearanceLinePtr lineAppearance() const = 0;
    [[nodiscard]] virtual bool isForceLineColor() const = 0;
    [[nodiscard]] virtual bool isForceLineWidth() const = 0;
};
using ImportLineParamsPtr = std::shared_ptr<const IImportLineParams>;

class IImportPolyParams : public IImportMarkParams {
public:
    [[nodiscard]] virtual AppearancePolygonBorderPtr borderAppearance() const = 0;
    [[nodiscard]] virtual AppearancePolygonFillPtr fillAppearance() const = 0;
    [[nodiscard]] virtual bool isForceLineColor() const = 0;
    [[nodiscard]] virtual bool isForceLineWidth() const = 0;
    [[nodiscard]] virtual bool isForceFillColor() const = 0;
};
using ImportPolyParamsPtr = std::shared_ptr<const IImportPolyParams>;

class IImportCategoryParams {
public:
    virtual ~IImportCategoryParams() = default;
    [[nodiscard]] virtual bool isAddAllInRootCategory() const = 0;
    [[nodiscard]] virtual bool isIgnoreMarkIfSubCategotyNotEixts() const = 0; // legacy spelling kept
    [[nodiscard]] virtual bool isCreateSubCategory() const = 0;
    [[nodiscard]] virtual bool isIgnoreMarkIfExistsWithSameNameInCategory() const = 0;
};
using ImportCategoryParamsPtr = std::shared_ptr<const IImportCategoryParams>;

class IImportConfig {
public:
    virtual ~IImportConfig() = default;
    [[nodiscard]] virtual CategoryPtr rootCategory() const = 0;
    [[nodiscard]] virtual ImportCategoryParamsPtr categoryParams() const = 0;
    [[nodiscard]] virtual ImportPointParamsPtr pointParams() const = 0; // each may be null
    [[nodiscard]] virtual ImportLineParamsPtr lineParams() const = 0;
    [[nodiscard]] virtual ImportPolyParamsPtr polyParams() const = 0;
};
using ImportConfigPtr = std::shared_ptr<const IImportConfig>;

class ImportPointParams final : public IImportPointParams { // TImportPointParams
public:
    ImportPointParams(AppearancePtr appearance, bool isForceTextColor, bool isForceTextBgColor,
                      bool isForceFontSize, bool isForceMarkerSize, bool isForcePicName);

    [[nodiscard]] AppearancePtr appearance() const override;
    [[nodiscard]] AppearancePointCaptionPtr captionAppearance() const override;
    [[nodiscard]] AppearancePointIconPtr iconAppearance() const override;
    [[nodiscard]] bool isForceTextColor() const override;
    [[nodiscard]] bool isForceTextBgColor() const override;
    [[nodiscard]] bool isForceFontSize() const override;
    [[nodiscard]] bool isForceMarkerSize() const override;
    [[nodiscard]] bool isForcePicName() const override;

private:
    AppearancePtr appearance_;
    AppearancePointCaptionPtr captionAppearance_;
    AppearancePointIconPtr iconAppearance_;
    bool isForceTextColor_;
    bool isForceTextBgColor_;
    bool isForceFontSize_;
    bool isForceMarkerSize_;
    bool isForcePicName_;
};

class ImportLineParams final : public IImportLineParams { // TImportLineParams
public:
    ImportLineParams(AppearancePtr appearance, bool isForceLineColor, bool isForceLineWidth);

    [[nodiscard]] AppearancePtr appearance() const override;
    [[nodiscard]] AppearanceLinePtr lineAppearance() const override;
    [[nodiscard]] bool isForceLineColor() const override;
    [[nodiscard]] bool isForceLineWidth() const override;

private:
    AppearancePtr appearance_;
    AppearanceLinePtr lineAppearance_;
    bool isForceLineColor_;
    bool isForceLineWidth_;
};

class ImportPolyParams final : public IImportPolyParams { // TImportPolyParams
public:
    ImportPolyParams(AppearancePtr appearance, bool isForceLineColor, bool isForceLineWidth,
                     bool isForceFillColor);

    [[nodiscard]] AppearancePtr appearance() const override;
    [[nodiscard]] AppearancePolygonBorderPtr borderAppearance() const override;
    [[nodiscard]] AppearancePolygonFillPtr fillAppearance() const override;
    [[nodiscard]] bool isForceLineColor() const override;
    [[nodiscard]] bool isForceLineWidth() const override;
    [[nodiscard]] bool isForceFillColor() const override;

private:
    AppearancePtr appearance_;
    AppearancePolygonBorderPtr borderAppearance_;
    AppearancePolygonFillPtr fillAppearance_;
    bool isForceLineColor_;
    bool isForceLineWidth_;
    bool isForceFillColor_;
};

class ImportCategoryParams final : public IImportCategoryParams { // TImportCategoryParams
public:
    ImportCategoryParams(bool isAddAllInRootCategory, bool isIgnoreMarkIfSubCategotyNotEixts,
                         bool isCreateSubCategory, bool isIgnoreMarkIfExistsWithSameNameInCategory);

    [[nodiscard]] bool isAddAllInRootCategory() const override;
    [[nodiscard]] bool isIgnoreMarkIfSubCategotyNotEixts() const override;
    [[nodiscard]] bool isCreateSubCategory() const override;
    [[nodiscard]] bool isIgnoreMarkIfExistsWithSameNameInCategory() const override;

private:
    bool isAddAllInRootCategory_;
    bool isIgnoreMarkIfSubCategotyNotEixts_;
    bool isCreateSubCategory_;
    bool isIgnoreMarkIfExistsWithSameNameInCategory_;
};

class ImportConfig
    : public IImportConfig { // TImportConfig; not final: JpegWithExifImportConfig derives (§2.12)
public:
    ImportConfig(CategoryPtr rootCategory, ImportCategoryParamsPtr categoryParams,
                 ImportPointParamsPtr pointParams, ImportLineParamsPtr lineParams,
                 ImportPolyParamsPtr polyParams);

    [[nodiscard]] CategoryPtr rootCategory() const override;
    [[nodiscard]] ImportCategoryParamsPtr categoryParams() const override;
    [[nodiscard]] ImportPointParamsPtr pointParams() const override;
    [[nodiscard]] ImportLineParamsPtr lineParams() const override;
    [[nodiscard]] ImportPolyParamsPtr polyParams() const override;

private:
    CategoryPtr rootCategory_;
    ImportCategoryParamsPtr categoryParams_;
    ImportPointParamsPtr pointParams_;
    ImportLineParamsPtr lineParams_;
    ImportPolyParamsPtr polyParams_;
};

} // namespace cg::vector
