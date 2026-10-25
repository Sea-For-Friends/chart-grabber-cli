// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_category_factory_config.h"

#include <utility>

// Ports u_MarkCategoryFactoryConfig.pas (T-0091). The legacy child is added with Add(item, strategy) = all
// four flags on (u_ConfigDataElementComplexBase.pas:266-272), as maptype/map_type_gui_config.cpp does.

using namespace Qt::StringLiterals;

namespace cg::marks {

MarkCategoryFactoryConfig::MarkCategoryFactoryConfig(std::shared_ptr<core::ILanguageManager> languageManager)
    : defaultName_(std::make_shared<core::StringConfigDataElementWithDefByStringRec>(
          std::move(languageManager), true, u"DefaultName"_s, true, u"New Category"_s)) {
    // Ports SAS_STR_NewCategory (u_ResStrings.pas:186) as the default text; constructor :64-78, Add :75.
    addChild(defaultName_, core::SaveLoadSlot{}, ChildFlags{true, true, true, true});
}

MarkCategoryFactoryConfig::~MarkCategoryFactoryConfig() = default;

std::shared_ptr<core::IStringConfigDataElement> MarkCategoryFactoryConfig::defaultName() const {
    return defaultName_;
}

int MarkCategoryFactoryConfig::afterScale() const {
    const auto guard = readLock();
    return afterScale_;
}

void MarkCategoryFactoryConfig::setAfterScale(int value) {
    const auto guard = writeLock();
    if (afterScale_ != value) {
        afterScale_ = value;
        setChanged();
    }
}

int MarkCategoryFactoryConfig::beforeScale() const {
    const auto guard = readLock();
    return beforeScale_;
}

void MarkCategoryFactoryConfig::setBeforeScale(int value) {
    const auto guard = writeLock();
    if (beforeScale_ != value) {
        beforeScale_ = value;
        setChanged();
    }
}

// u_MarkCategoryFactoryConfig.pas:80-90: the child first (inherited), then the scales (:86-87), and only
// when the provider is not nil (:85); SetChanged (:88) follows even when the values are the same.
void MarkCategoryFactoryConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    ConfigElementComplex::doReadConfig(src);
    if (src) {
        afterScale_ = src->readInt(u"AfterScale"_s, afterScale_);
        beforeScale_ = src->readInt(u"BeforeScale"_s, beforeScale_);
        setChanged();
    }
}

// u_MarkCategoryFactoryConfig.pas:92-99: inherited (the child) first, then the two scales (:97-98).
void MarkCategoryFactoryConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    ConfigElementComplex::doWriteConfig(dst);
    if (dst == nullptr) {
        return; // defensive, as string_config_data_element_with_def_base.cpp; no legacy caller traced
    }
    dst->writeInt(u"AfterScale"_s, afterScale_);
    dst->writeInt(u"BeforeScale"_s, beforeScale_);
}

} // namespace cg::marks
