// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_factory_config.h"

#include "cg/marks/mark_line_template_config.h"
#include "cg/marks/mark_point_template_config.h"
#include "cg/marks/mark_poly_template_config.h"

#include <utility>

// Ports u_MarkFactoryConfig.pas (T-0098). The three configs are added with the two-argument Add (all four
// flags on, u_ConfigDataElementComplexBase.pas:266-272), each under its own sub-item.

namespace cg::marks {

MarkFactoryConfig::MarkFactoryConfig(vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                                     const std::shared_ptr<core::ILanguageManager>& languageManager)
    : point_(std::make_shared<MarkPointTemplateConfig>(appearanceOfMarkFactory, languageManager)),
      line_(std::make_shared<MarkLineTemplateConfig>(appearanceOfMarkFactory, languageManager)),
      poly_(std::make_shared<MarkPolyTemplateConfig>(std::move(appearanceOfMarkFactory), languageManager)) {
    const ChildFlags all{true, true, true, true};
    addChild(point_, core::SaveLoadSlot{QStringLiteral("MarkNewPoint")}, all);
    addChild(line_, core::SaveLoadSlot{QStringLiteral("MarkNewLine")}, all);
    addChild(poly_, core::SaveLoadSlot{QStringLiteral("MarkNewPoly")}, all);
}

MarkFactoryConfig::~MarkFactoryConfig() = default;

std::shared_ptr<IMarkPointTemplateConfig> MarkFactoryConfig::pointTemplateConfig() const {
    return point_;
}

std::shared_ptr<IMarkLineTemplateConfig> MarkFactoryConfig::lineTemplateConfig() const {
    return line_;
}

std::shared_ptr<IMarkPolyTemplateConfig> MarkFactoryConfig::polyTemplateConfig() const {
    return poly_;
}

} // namespace cg::marks
