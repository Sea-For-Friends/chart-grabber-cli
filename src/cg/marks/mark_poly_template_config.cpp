// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_poly_template_config.h"

#include "cg/core/color32.h"
#include "cg/marks/mark_template.h"
#include "cg/vector/category.h"
#include "cg/vector/config_provider_helpers.h"

#include <utility>

// Ports u_MarkPolyTemplateConfig.pas (T-0098). The format text is SAS_STR_NewPoly (u_ResStrings.pas,
// 'FormatString').

using namespace Qt::StringLiterals;

namespace cg::marks {

namespace {
vector::AppearancePtr makeDefaultAppearance(const vector::IAppearanceOfMarkFactory& factory) {
    vector::AppearancePtr appearance = factory.createPolygonAppearance(
        core::setAlpha(core::clBlack32, 166), 2, core::setAlpha(core::clWhite32, 51));
    return appearance;
}

} // namespace

MarkPolyTemplateConfig::MarkPolyTemplateConfig(vector::AppearanceOfMarkFactoryPtr appearances,
                                               const std::shared_ptr<core::ILanguageManager>& languageManager)
    : MarkTemplateConfigBase(std::move(appearances),
                             makeTemplateFormatString(languageManager, u"Polygon %0:d"_s)) {
    defaultTemplate_ = createTemplate(makeDefaultAppearance(appearanceOfMarkFactory()), nullptr);
}

MarkPolyTemplateConfig::~MarkPolyTemplateConfig() = default;

MarkTemplatePolyPtr MarkPolyTemplateConfig::createTemplate(const vector::AppearancePtr& appearance,
                                                           const vector::CategoryPtr& category) const {
    vector::CategoryPtr useCategory = category;
    if (useCategory == nullptr) {
        useCategory = std::make_shared<const vector::Category>(QString());
    }
    return std::make_shared<const MarkTemplatePoly>(nameGeneratorImpl(), std::move(useCategory), appearance);
}

void MarkPolyTemplateConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    MarkTemplateConfigBase::doReadConfig(src);
    QString categoryName = defaultTemplate_->category()->name();
    if (src != nullptr) {
        categoryName = src->readString(u"CategoryName"_s, categoryName);
    }
    const vector::AppearancePtr appearance =
        vector::readAppearancePolygon(src, appearanceOfMarkFactory(), defaultTemplate_->appearance());
    setDefaultTemplateLocked(
        createTemplate(appearance, std::make_shared<const vector::Category>(categoryName)));
}

void MarkPolyTemplateConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    MarkTemplateConfigBase::doWriteConfig(dst);
    if (dst == nullptr) {
        return; // defensive; the legacy callers dereference the writer first, no legacy caller traced
    }
    dst->writeString(u"CategoryName"_s, defaultTemplate_->category()->name());
    vector::writeAppearancePolygon(dst, defaultTemplate_->appearance());
}

MarkTemplatePolyPtr MarkPolyTemplateConfig::defaultTemplate() const {
    const auto guard = readLock();
    return defaultTemplate_;
}

void MarkPolyTemplateConfig::setDefaultTemplate(const MarkTemplatePolyPtr& value) {
    if (value != nullptr) {
        const auto guard = writeLock();
        setDefaultTemplateLocked(value);
    }
}

void MarkPolyTemplateConfig::setDefaultTemplateLocked(const MarkTemplatePolyPtr& value) {
    if (value != nullptr && (defaultTemplate_ == nullptr || !defaultTemplate_->isSame(value.get()))) {
        defaultTemplate_ = value;
        setChanged();
    }
}

} // namespace cg::marks
