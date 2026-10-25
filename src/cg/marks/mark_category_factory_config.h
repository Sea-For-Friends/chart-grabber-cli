// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkCategoryFactoryConfig.pas  (interface, shared), u_MarkCategoryFactoryConfig.pas
// (MarkCategoryFactoryConfig, T-0091)
//
// The settings of new categories: the default name (a language-dependent string, stored as DefaultName)
// and the zoom range (AfterScale 3, BeforeScale 23). Persisted in the [MarkNewCategory] section. The legacy
// child is added with all four child flags on, as here (mark_category_factory_config.cpp).

#include "cg/core/config_element_complex.h"
#include "cg/core/language_manager.h"
#include "cg/core/string_config_data_element.h"
#include "cg/core/string_config_data_element_with_def_by_string_rec.h"

#include <memory>

namespace cg::marks {

class IMarkCategoryFactoryConfig : public core::ConfigElementComplex {
public:
    [[nodiscard]] virtual std::shared_ptr<core::IStringConfigDataElement> defaultName() const = 0;
    [[nodiscard]] virtual int afterScale() const = 0;
    virtual void setAfterScale(int value) = 0;
    [[nodiscard]] virtual int beforeScale() const = 0;
    virtual void setBeforeScale(int value) = 0;
};

// Ports TMarkCategoryFactoryConfig (a TConfigDataElementComplexBase; the interface derives from
// core::ConfigElementComplex for that reason).
class MarkCategoryFactoryConfig final : public IMarkCategoryFactoryConfig {
public:
    explicit MarkCategoryFactoryConfig(std::shared_ptr<core::ILanguageManager> languageManager);
    ~MarkCategoryFactoryConfig() override;

    [[nodiscard]] std::shared_ptr<core::IStringConfigDataElement> defaultName() const override;
    [[nodiscard]] int afterScale() const override;
    void setAfterScale(int value) override;
    [[nodiscard]] int beforeScale() const override;
    void setBeforeScale(int value) override;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;

private:
    std::shared_ptr<core::StringConfigDataElementWithDefByStringRec> defaultName_;
    int afterScale_ = 3;
    int beforeScale_ = 23;
};

} // namespace cg::marks
