// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MarkTemplateConfigBase.pas (TMarkTemplateConfigBase, T-0098)
//
// The legacy base class of the three template configs holds the name generator and the appearance factory
// and adds the generator under the 'Name' sub-item with all four child flags False (:67). C++ cannot
// inherit the config element once per interface, so the base is a class template over the kind's interface
// (IMarkPointTemplateConfig, ...): TMarkTemplateConfigBase -> MarkTemplateConfigBase<Interface>.

#include "cg/core/config_element_complex.h"
#include "cg/core/language_manager.h"
#include "cg/core/string_config_data_element.h"
#include "cg/marks/mark_name_generator.h"
#include "cg/vector/appearance_of_mark_factory.h"

#include <QString>

#include <memory>
#include <stdexcept>
#include <utility>

namespace cg::marks {

// Ports the format-string element that every template config creates (u_MarkPointTemplateConfig.pas:80-87
// and its line and poly twins): TStringConfigDataElementWithDefByStringRec(language manager, store,
// 'FormatString', store default, resource string). A nil language manager is refused (legacy: access
// violation, D-95).
[[nodiscard]] std::shared_ptr<core::IStringConfigDataElement>
makeTemplateFormatString(const std::shared_ptr<core::ILanguageManager>& languageManager,
                         const QString& resString);

template <class Interface> class MarkTemplateConfigBase : public Interface {
public:
    [[nodiscard]] std::shared_ptr<IMarkNameGenerator> nameGenerator() const override {
        return nameGenerator_;
    }

protected:
    // u_MarkTemplateConfigBase.pas:57-68. A nil appearance factory is refused (legacy: access violation in
    // the derived constructor's CreatePointAppearance call, u_MarkPointTemplateConfig.pas:91 and the twins,
    // D-95); a nil format string by MarkNameGenerator.
    MarkTemplateConfigBase(vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                           std::shared_ptr<core::IStringConfigDataElement> formatString)
        : appearanceOfMarkFactory_(std::move(appearanceOfMarkFactory)),
          nameGenerator_(std::make_shared<MarkNameGenerator>(std::move(formatString))) {
        if (appearanceOfMarkFactory_ == nullptr) {
            throw std::invalid_argument("MarkTemplateConfigBase: nil appearance factory");
        }
        this->addChild(nameGenerator_, core::SaveLoadSlot{QStringLiteral("Name")},
                       core::ConfigElementChildFlags{false, false, false, false});
    }

    [[nodiscard]] const vector::IAppearanceOfMarkFactory& appearanceOfMarkFactory() const {
        return *appearanceOfMarkFactory_;
    }
    [[nodiscard]] const std::shared_ptr<MarkNameGenerator>& nameGeneratorImpl() const {
        return nameGenerator_;
    }

private:
    vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory_;
    std::shared_ptr<MarkNameGenerator> nameGenerator_;
};

} // namespace cg::marks
