// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/mark_system_impl_factory_orm.h"

#include "cg/marks/mormot/mark_system_impl_orm.h"

#include <stdexcept>
#include <utility>

namespace cg::marks {

// :85-115
MarkSystemImplFactoryORM::MarkSystemImplFactoryORM(
    vector::MarkPictureListPtr markPictureList, vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
    const geo::GeometryLonLatFactory& geometryFactory,
    vector::VectorItemSubsetBuilderFactoryPtr subsetBuilderFactory,
    std::shared_ptr<const IMarkFactory> markFactory, vector::HtmlToHintTextConverterPtr hintConverter)
    : markPictureList_(std::move(markPictureList)),
      appearanceOfMarkFactory_(std::move(appearanceOfMarkFactory)), geometryFactory_(geometryFactory),
      subsetBuilderFactory_(std::move(subsetBuilderFactory)), markFactory_(std::move(markFactory)),
      hintConverter_(std::move(hintConverter)) {
    // The legacy Asserts :97-103 are off in the shipped build; a null collaborator would be an access
    // violation at the first use. Refused here instead (D-95 family).
    if (!markPictureList_) {
        throw std::invalid_argument("MarkSystemImplFactoryORM: markPictureList is null");
    }
    if (!appearanceOfMarkFactory_) {
        throw std::invalid_argument("MarkSystemImplFactoryORM: appearanceOfMarkFactory is null");
    }
    if (!subsetBuilderFactory_) {
        throw std::invalid_argument("MarkSystemImplFactoryORM: subsetBuilderFactory is null");
    }
    if (!markFactory_) {
        throw std::invalid_argument("MarkSystemImplFactoryORM: markFactory is null");
    }
    if (!hintConverter_) {
        throw std::invalid_argument("MarkSystemImplFactoryORM: hintConverter is null");
    }
}

// :117-120
bool MarkSystemImplFactoryORM::isInitializationRequired() const {
    return true;
}

// :122-146
std::shared_ptr<IMarkSystemImpl>
MarkSystemImplFactoryORM::build(std::stop_token stop, const QString& basePath,
                                const MarkSystemImplConfigStaticPtr& implConfig) {
    return std::make_shared<MarkSystemImplORM>(
        std::move(stop), basePath, markPictureList_, appearanceOfMarkFactory_, geometryFactory_,
        subsetBuilderFactory_, markFactory_, hintConverter_, implConfig);
}

} // namespace cg::marks
