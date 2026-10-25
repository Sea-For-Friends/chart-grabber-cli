// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_point_template_config.h"

#include "cg/core/color32.h"
#include "cg/marks/mark_template.h"
#include "cg/vector/category.h"
#include "cg/vector/config_provider_helpers.h"

#include <utility>

// Ports u_MarkPointTemplateConfig.pas (T-0098). The format text is SAS_STR_NewMark (u_ResStrings.pas,
// 'FormatString').

using namespace Qt::StringLiterals;

namespace cg::marks {

namespace {
vector::AppearancePtr makeDefaultAppearance(const vector::IAppearanceOfMarkFactory& factory) {
    vector::AppearancePtr appearance =
        factory.createPointAppearance(core::setAlpha(core::clYellow32, 166),
                                      core::setAlpha(core::clBlack32, 166), 11, QString(), nullptr, 32);
    return appearance;
}

} // namespace

MarkPointTemplateConfig::MarkPointTemplateConfig(
    vector::AppearanceOfMarkFactoryPtr appearances,
    const std::shared_ptr<core::ILanguageManager>& languageManager)
    : MarkTemplateConfigBase(std::move(appearances),
                             makeTemplateFormatString(languageManager, u"Placemark %0:d"_s)) {
    defaultTemplate_ = createTemplate(makeDefaultAppearance(appearanceOfMarkFactory()), nullptr);
}

MarkPointTemplateConfig::~MarkPointTemplateConfig() = default;

MarkTemplatePointPtr MarkPointTemplateConfig::createTemplate(const vector::AppearancePtr& appearance,
                                                             const vector::CategoryPtr& category) const {
    vector::CategoryPtr useCategory = category;
    if (useCategory == nullptr) {
        useCategory = std::make_shared<const vector::Category>(QString());
    }
    return std::make_shared<const MarkTemplatePoint>(nameGeneratorImpl(), std::move(useCategory), appearance);
}

void MarkPointTemplateConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    MarkTemplateConfigBase::doReadConfig(src);
    QString categoryName = defaultTemplate_->category()->name();
    if (src != nullptr) {
        categoryName = src->readString(u"CategoryName"_s, categoryName);
    }
    const vector::AppearancePtr appearance =
        vector::readAppearancePoint(src, nullptr, appearanceOfMarkFactory(), defaultTemplate_->appearance());
    setDefaultTemplateLocked(
        createTemplate(appearance, std::make_shared<const vector::Category>(categoryName)));
}

void MarkPointTemplateConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    MarkTemplateConfigBase::doWriteConfig(dst);
    if (dst == nullptr) {
        return; // defensive; the legacy callers dereference the writer first, no legacy caller traced
    }
    dst->writeString(u"CategoryName"_s, defaultTemplate_->category()->name());
    vector::writeAppearancePoint(dst, defaultTemplate_->appearance());
}

MarkTemplatePointPtr MarkPointTemplateConfig::defaultTemplate() const {
    const auto guard = readLock();
    return defaultTemplate_;
}

void MarkPointTemplateConfig::setDefaultTemplate(const MarkTemplatePointPtr& value) {
    if (value != nullptr) {
        const auto guard = writeLock();
        setDefaultTemplateLocked(value);
    }
}

void MarkPointTemplateConfig::setDefaultTemplateLocked(const MarkTemplatePointPtr& value) {
    if (value != nullptr && (defaultTemplate_ == nullptr || !defaultTemplate_->isSame(value.get()))) {
        defaultTemplate_ = value;
        setChanged();
    }
}

} // namespace cg::marks
