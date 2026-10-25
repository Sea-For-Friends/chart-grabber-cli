// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_factory.h"

#include "cg/core/hash.h"
#include "cg/marks/mark_full_base.h"
#include "cg/vector/appearance_of_vector_item.h"
#include "cg/vector/vector_data_item_base.h"

#include <memory>
#include <utility>

namespace cg::marks {

MarkFactory::MarkFactory(std::shared_ptr<IMarkFactoryConfig> config,
                         vector::MarkPictureListPtr markPictureList,
                         vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                         vector::HtmlToHintTextConverterPtr hintConverter)
    : config_(std::move(config)), appearanceOfMarkFactory_(std::move(appearanceOfMarkFactory)),
      hintConverter_(std::move(hintConverter)), markPictureList_(std::move(markPictureList)) {}

vector::VectorDataItemPtr MarkFactory::createNewMark(const geo::GeometryLonLatPtr& geometry,
                                                     const QString& name, const QString& desc,
                                                     const MarkTemplatePtr& tmpl) const {
    MarkTemplatePtr useTemplate = tmpl;
    if (useTemplate == nullptr && geometry != nullptr) {
        if (dynamic_cast<const geo::IGeometryLonLatPoint*>(geometry.get()) != nullptr) {
            useTemplate = config_->pointTemplateConfig()->defaultTemplate();
        } else if (dynamic_cast<const geo::IGeometryLonLatLine*>(geometry.get()) != nullptr) {
            useTemplate = config_->lineTemplateConfig()->defaultTemplate();
        } else if (dynamic_cast<const geo::IGeometryLonLatPolygon*>(geometry.get()) != nullptr) {
            useTemplate = config_->polyTemplateConfig()->defaultTemplate();
        }
    }
    if (useTemplate == nullptr) {
        return nullptr; // legacy: access violation on VTemplate.GetNewName / .Category
    }
    QString useName = name;
    if (useName.isEmpty()) {
        useName = useTemplate->newName();
    }
    return createMark(geometry, useName, desc, useTemplate->category(), useTemplate->appearance());
}

vector::VectorDataItemPtr MarkFactory::createMark(const geo::GeometryLonLatPtr& geometry, const QString& name,
                                                  const QString& desc, const vector::CategoryPtr& category,
                                                  const vector::AppearancePtr& appearance) const {
    if (geometry == nullptr || appearance == nullptr) {
        return nullptr; // legacy: Assert in debug, access violation in release
    }
    core::HashValue hash = core::hashOf(QStringView(name));
    core::hashUpdate(hash, QStringView(desc));
    auto mainInfo = std::make_shared<const MarkMainInfo>(hash, hintConverter_, name, category, desc);

    hash = geometry->hash();
    core::hashUpdate(hash, mainInfo->hash());
    core::hashUpdate(hash, appearance->hash());
    return std::make_shared<const vector::VectorDataItem>(hash, appearance, std::move(mainInfo), geometry);
}

namespace {

vector::CategoryPtr categoryOf(const vector::IVectorDataItem& item) {
    const auto mainInfo = item.mainInfo();
    const auto* withCategory = dynamic_cast<const vector::IVectorDataItemWithCategory*>(mainInfo.get());
    return withCategory != nullptr ? withCategory->category() : nullptr;
}

} // namespace

vector::VectorDataItemPtr MarkFactory::modifyGeometry(const vector::VectorDataItemPtr& source,
                                                      const geo::GeometryLonLatPtr& geometry) const {
    if (source == nullptr) {
        return nullptr; // legacy: access violation
    }
    return createMark(geometry, source->name(), source->desc(), categoryOf(*source), source->appearance());
}

vector::VectorDataItemPtr MarkFactory::modifyName(const vector::VectorDataItemPtr& source,
                                                  const QString& name, const QString& desc) const {
    if (source == nullptr) {
        return nullptr; // legacy: access violation
    }
    return createMark(source->geometry(), name.isEmpty() ? source->name() : name,
                      desc.isEmpty() ? source->desc() : desc, categoryOf(*source), source->appearance());
}

vector::AppearancePtr MarkFactory::prepareAppearance(const vector::AppearancePtr& source,
                                                     const vector::IImportMarkParams& params) const {
    if (const auto* point = dynamic_cast<const vector::IImportPointParams*>(&params)) {
        return preparePointAppearance(source, *point);
    }
    if (const auto* line = dynamic_cast<const vector::IImportLineParams*>(&params)) {
        return prepareLineAppearance(source, *line);
    }
    if (const auto* poly = dynamic_cast<const vector::IImportPolyParams*>(&params)) {
        return preparePolyAppearance(source, *poly);
    }
    return nullptr;
}

vector::AppearancePtr MarkFactory::preparePointAppearance(const vector::AppearancePtr& source,
                                                          const vector::IImportPointParams& params) const {
    const auto paramsIcon = params.iconAppearance();
    const auto paramsCaption = params.captionAppearance();
    if (paramsIcon == nullptr || paramsCaption == nullptr) {
        return nullptr; // legacy: access violation on a params object built without an appearance
    }
    vector::MarkPicturePtr pic = paramsIcon->pic();
    QString picName = paramsIcon->picName();
    int fontSize = paramsCaption->fontSize();
    int markerSize = paramsIcon->markerSize();
    core::Color32 textColor = paramsCaption->textColor();
    core::Color32 textBgColor = paramsCaption->textBgColor();
    if (const auto* icon = dynamic_cast<const vector::IAppearancePointIcon*>(source.get())) {
        if (!params.isForcePicName()) {
            pic = icon->pic();
            picName = icon->picName();
        }
        if (!params.isForceMarkerSize()) {
            markerSize = icon->markerSize();
        }
    }
    if (const auto* caption = dynamic_cast<const vector::IAppearancePointCaption*>(source.get())) {
        if (!params.isForceTextColor()) {
            textColor = caption->textColor();
        }
        if (!params.isForceTextBgColor()) {
            textBgColor = caption->textBgColor();
        }
        if (!params.isForceFontSize()) {
            fontSize = caption->fontSize();
        }
    }
    return appearanceOfMarkFactory_->createPointAppearance(textColor, textBgColor, fontSize, picName, pic,
                                                           markerSize);
}

vector::AppearancePtr MarkFactory::prepareLineAppearance(const vector::AppearancePtr& source,
                                                         const vector::IImportLineParams& params) const {
    const auto paramsLine = params.lineAppearance();
    if (paramsLine == nullptr) {
        return nullptr; // legacy: access violation
    }
    core::Color32 lineColor = paramsLine->lineColor();
    int lineWidth = paramsLine->lineWidth();
    if (const auto* line = dynamic_cast<const vector::IAppearanceLine*>(source.get())) {
        if (!params.isForceLineColor()) {
            lineColor = line->lineColor();
        }
        if (!params.isForceLineWidth()) {
            lineWidth = line->lineWidth();
        }
    }
    return appearanceOfMarkFactory_->createLineAppearance(lineColor, lineWidth);
}

vector::AppearancePtr MarkFactory::preparePolyAppearance(const vector::AppearancePtr& source,
                                                         const vector::IImportPolyParams& params) const {
    const auto paramsBorder = params.borderAppearance();
    const auto paramsFill = params.fillAppearance();
    if (paramsBorder == nullptr || paramsFill == nullptr) {
        return nullptr; // legacy: access violation
    }
    core::Color32 lineColor = paramsBorder->lineColor();
    int lineWidth = paramsBorder->lineWidth();
    core::Color32 fillColor = paramsFill->fillColor();
    if (const auto* border = dynamic_cast<const vector::IAppearancePolygonBorder*>(source.get())) {
        if (!params.isForceLineColor()) {
            lineColor = border->lineColor();
        }
        if (!params.isForceLineWidth()) {
            lineWidth = border->lineWidth();
        }
    }
    if (const auto* fill = dynamic_cast<const vector::IAppearancePolygonFill*>(source.get())) {
        if (!params.isForceFillColor()) {
            fillColor = fill->fillColor();
        }
    }
    return appearanceOfMarkFactory_->createPolygonAppearance(lineColor, lineWidth, fillColor);
}

vector::VectorDataItemPtr
MarkFactory::prepareMark(const vector::VectorDataItemPtr& item, const QString& name,
                         const std::shared_ptr<const vector::IImportMarkParams>& params,
                         const vector::CategoryPtr& category) const {
    if (params == nullptr || item == nullptr) { // null params: nil, as legacy (:379-400); null item: AV
        return nullptr;
    }
    return createMark(item->geometry(), name, item->desc(), category,
                      prepareAppearance(item->appearance(), *params));
}

vector::VectorDataItemPtr MarkFactory::replaceCategory(const vector::VectorDataItemPtr& mark,
                                                       const vector::CategoryPtr& category) const {
    if (mark == nullptr) {
        return nullptr;
    }
    return createMark(mark->geometry(), mark->name(), mark->desc(), category, mark->appearance());
}

vector::MarkPictureListPtr MarkFactory::markPictureList() const {
    return markPictureList_;
}

std::shared_ptr<IMarkFactoryConfig> MarkFactory::config() const {
    return config_;
}

} // namespace cg::marks
