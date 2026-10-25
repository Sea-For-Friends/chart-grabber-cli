// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkFactoryConfig.pas, u_MarkFactoryConfig.pas (MarkFactoryConfig, T-0098)
//
// The four interfaces derive from core::ConfigElementComplex (T-0098): their implementations are
// TConfigDataElementComplexBase descendants (the factory config and the template configs aggregate
// children), as IMarkNameGenerator does.

#include "cg/core/config_element_complex.h"
#include "cg/core/language_manager.h"
#include "cg/marks/mark_name_generator.h"
#include "cg/marks/mark_template.h"
#include "cg/vector/appearance.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/category.h"

#include <memory>

namespace cg::marks {

class IMarkPointTemplateConfig : public core::ConfigElementComplex {
public:
    [[nodiscard]] virtual MarkTemplatePointPtr createTemplate(const vector::AppearancePtr& appearance,
                                                              const vector::CategoryPtr& category) const = 0;
    [[nodiscard]] virtual MarkTemplatePointPtr defaultTemplate() const = 0;
    virtual void setDefaultTemplate(const MarkTemplatePointPtr& value) = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkNameGenerator> nameGenerator() const = 0;
};

class IMarkLineTemplateConfig : public core::ConfigElementComplex {
public:
    [[nodiscard]] virtual MarkTemplateLinePtr createTemplate(const vector::AppearancePtr& appearance,
                                                             const vector::CategoryPtr& category) const = 0;
    [[nodiscard]] virtual MarkTemplateLinePtr defaultTemplate() const = 0;
    virtual void setDefaultTemplate(const MarkTemplateLinePtr& value) = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkNameGenerator> nameGenerator() const = 0;
};

class IMarkPolyTemplateConfig : public core::ConfigElementComplex {
public:
    [[nodiscard]] virtual MarkTemplatePolyPtr createTemplate(const vector::AppearancePtr& appearance,
                                                             const vector::CategoryPtr& category) const = 0;
    [[nodiscard]] virtual MarkTemplatePolyPtr defaultTemplate() const = 0;
    virtual void setDefaultTemplate(const MarkTemplatePolyPtr& value) = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkNameGenerator> nameGenerator() const = 0;
};

class IMarkFactoryConfig : public core::ConfigElementComplex {
public:
    [[nodiscard]] virtual std::shared_ptr<IMarkPointTemplateConfig> pointTemplateConfig() const = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkLineTemplateConfig> lineTemplateConfig() const = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkPolyTemplateConfig> polyTemplateConfig() const = 0;
};

// Ports TMarkFactoryConfig (u_MarkFactoryConfig.pas): owns the three template configs and stores them in
// the sections MarkNewPoint, MarkNewLine, MarkNewPoly.
class MarkFactoryConfig final : public IMarkFactoryConfig {
public:
    // Nil arguments are refused (legacy: a nil appearance factory is an access violation in the template
    // config constructors, u_MarkPointTemplateConfig.pas:91; a nil language manager: access violation in the
    // string element constructor, u_StringConfigDataElementWithDefBase.pas:91, unreachable:
    // u_GlobalConfig.pas:340 passes a created one; D-95).
    MarkFactoryConfig(vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                      const std::shared_ptr<core::ILanguageManager>& languageManager);
    ~MarkFactoryConfig() override;

    [[nodiscard]] std::shared_ptr<IMarkPointTemplateConfig> pointTemplateConfig() const override;
    [[nodiscard]] std::shared_ptr<IMarkLineTemplateConfig> lineTemplateConfig() const override;
    [[nodiscard]] std::shared_ptr<IMarkPolyTemplateConfig> polyTemplateConfig() const override;

private:
    std::shared_ptr<IMarkPointTemplateConfig> point_;
    std::shared_ptr<IMarkLineTemplateConfig> line_;
    std::shared_ptr<IMarkPolyTemplateConfig> poly_;
};

} // namespace cg::marks
