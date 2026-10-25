// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MarkPolyTemplateConfig.pas (TMarkPolyTemplateConfig, T-0098)

#include "cg/marks/mark_factory_config.h"
#include "cg/marks/mark_template_config_base.h"

#include <memory>

namespace cg::marks {

class MarkPolyTemplateConfig final : public MarkTemplateConfigBase<IMarkPolyTemplateConfig> {
public:
    MarkPolyTemplateConfig(vector::AppearanceOfMarkFactoryPtr appearances,
                           const std::shared_ptr<core::ILanguageManager>& languageManager);
    ~MarkPolyTemplateConfig() override;

    // Ports CreateTemplate: a nil category becomes the category with the empty name.
    [[nodiscard]] MarkTemplatePolyPtr createTemplate(const vector::AppearancePtr& appearance,
                                                     const vector::CategoryPtr& category) const override;
    [[nodiscard]] MarkTemplatePolyPtr defaultTemplate() const override;
    // A nil value is ignored; an IsSame value is not stored and not notified.
    void setDefaultTemplate(const MarkTemplatePolyPtr& value) override;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;

private:
    // SetDefaultTemplate without the lock: doReadConfig runs under the element's own write lock, which is
    // not recursive (legacy's is).
    void setDefaultTemplateLocked(const MarkTemplatePolyPtr& value);

    MarkTemplatePolyPtr defaultTemplate_;
};

} // namespace cg::marks
