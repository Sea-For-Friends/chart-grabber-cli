// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/import_config.h"

#include <stdexcept>
#include <utility>

namespace cg::vector {

namespace {

// Delphi `AAppearance as IFoo`: nil stays nil, a wrong kind raises (EIntfCastError).
template <class Facet> std::shared_ptr<const Facet> facetOf(const AppearancePtr& appearance) {
    if (!appearance) {
        return nullptr;
    }
    auto facet = std::dynamic_pointer_cast<const Facet>(appearance);
    if (!facet) {
        throw std::invalid_argument("import params: appearance of the wrong kind");
    }
    return facet;
}

} // namespace

// FEAT-GEO-037
ImportPointParams::ImportPointParams(AppearancePtr appearance, bool isForceTextColor, bool isForceTextBgColor,
                                     bool isForceFontSize, bool isForceMarkerSize, bool isForcePicName)
    : appearance_(std::move(appearance)), captionAppearance_(facetOf<IAppearancePointCaption>(appearance_)),
      iconAppearance_(facetOf<IAppearancePointIcon>(appearance_)), isForceTextColor_(isForceTextColor),
      isForceTextBgColor_(isForceTextBgColor), isForceFontSize_(isForceFontSize),
      isForceMarkerSize_(isForceMarkerSize), isForcePicName_(isForcePicName) {}

AppearancePtr ImportPointParams::appearance() const {
    return appearance_;
}
AppearancePointCaptionPtr ImportPointParams::captionAppearance() const {
    return captionAppearance_;
}
AppearancePointIconPtr ImportPointParams::iconAppearance() const {
    return iconAppearance_;
}
bool ImportPointParams::isForceTextColor() const {
    return isForceTextColor_;
}
bool ImportPointParams::isForceTextBgColor() const {
    return isForceTextBgColor_;
}
bool ImportPointParams::isForceFontSize() const {
    return isForceFontSize_;
}
bool ImportPointParams::isForceMarkerSize() const {
    return isForceMarkerSize_;
}
bool ImportPointParams::isForcePicName() const {
    return isForcePicName_;
}

// isForceLineColor is ignored: legacy stores isForceLineWidth into both fields (L-130).
ImportLineParams::ImportLineParams(AppearancePtr appearance, bool /*isForceLineColor*/, bool isForceLineWidth)
    : appearance_(std::move(appearance)), lineAppearance_(facetOf<IAppearanceLine>(appearance_)),
      isForceLineColor_(isForceLineWidth), isForceLineWidth_(isForceLineWidth) {}

AppearancePtr ImportLineParams::appearance() const {
    return appearance_;
}
AppearanceLinePtr ImportLineParams::lineAppearance() const {
    return lineAppearance_;
}
bool ImportLineParams::isForceLineColor() const {
    return isForceLineColor_;
}
bool ImportLineParams::isForceLineWidth() const {
    return isForceLineWidth_;
}

// Same slip as the line params (L-130).
ImportPolyParams::ImportPolyParams(AppearancePtr appearance, bool /*isForceLineColor*/, bool isForceLineWidth,
                                   bool isForceFillColor)
    : appearance_(std::move(appearance)), borderAppearance_(facetOf<IAppearancePolygonBorder>(appearance_)),
      fillAppearance_(facetOf<IAppearancePolygonFill>(appearance_)), isForceLineColor_(isForceLineWidth),
      isForceLineWidth_(isForceLineWidth), isForceFillColor_(isForceFillColor) {}

AppearancePtr ImportPolyParams::appearance() const {
    return appearance_;
}
AppearancePolygonBorderPtr ImportPolyParams::borderAppearance() const {
    return borderAppearance_;
}
AppearancePolygonFillPtr ImportPolyParams::fillAppearance() const {
    return fillAppearance_;
}
bool ImportPolyParams::isForceLineColor() const {
    return isForceLineColor_;
}
bool ImportPolyParams::isForceLineWidth() const {
    return isForceLineWidth_;
}
bool ImportPolyParams::isForceFillColor() const {
    return isForceFillColor_;
}

ImportCategoryParams::ImportCategoryParams(bool isAddAllInRootCategory,
                                           bool isIgnoreMarkIfSubCategotyNotEixts, bool isCreateSubCategory,
                                           bool isIgnoreMarkIfExistsWithSameNameInCategory)
    : isAddAllInRootCategory_(isAddAllInRootCategory),
      isIgnoreMarkIfSubCategotyNotEixts_(isIgnoreMarkIfSubCategotyNotEixts),
      isCreateSubCategory_(isCreateSubCategory),
      isIgnoreMarkIfExistsWithSameNameInCategory_(isIgnoreMarkIfExistsWithSameNameInCategory) {}

bool ImportCategoryParams::isAddAllInRootCategory() const {
    return isAddAllInRootCategory_;
}
bool ImportCategoryParams::isIgnoreMarkIfSubCategotyNotEixts() const {
    return isIgnoreMarkIfSubCategotyNotEixts_;
}
bool ImportCategoryParams::isCreateSubCategory() const {
    return isCreateSubCategory_;
}
bool ImportCategoryParams::isIgnoreMarkIfExistsWithSameNameInCategory() const {
    return isIgnoreMarkIfExistsWithSameNameInCategory_;
}

ImportConfig::ImportConfig(CategoryPtr rootCategory, ImportCategoryParamsPtr categoryParams,
                           ImportPointParamsPtr pointParams, ImportLineParamsPtr lineParams,
                           ImportPolyParamsPtr polyParams)
    : rootCategory_(std::move(rootCategory)), categoryParams_(std::move(categoryParams)),
      pointParams_(std::move(pointParams)), lineParams_(std::move(lineParams)),
      polyParams_(std::move(polyParams)) {}

CategoryPtr ImportConfig::rootCategory() const {
    return rootCategory_;
}
ImportCategoryParamsPtr ImportConfig::categoryParams() const {
    return categoryParams_;
}
ImportPointParamsPtr ImportConfig::pointParams() const {
    return pointParams_;
}
ImportLineParamsPtr ImportConfig::lineParams() const {
    return lineParams_;
}
ImportPolyParamsPtr ImportConfig::polyParams() const {
    return polyParams_;
}

} // namespace cg::vector
