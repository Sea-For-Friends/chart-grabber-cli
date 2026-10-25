// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/config_provider_helpers.h"

#include "cg/core/config_color.h"

#include <memory>

namespace cg::vector {

namespace {
constexpr Color32 kWhite = core::clWhite32;
constexpr Color32 kTextColor = core::setAlpha(core::clWhite32, 166);
constexpr Color32 kTextBgColor = core::setAlpha(core::clBlack32, 166);
constexpr Color32 kFillColor = core::setAlpha(core::clWhite32, 51);

template <class Facet> std::shared_ptr<const Facet> facetOf(const AppearancePtr& appearance) {
    return std::dynamic_pointer_cast<const Facet>(appearance); // Supports(); null stays null
}
} // namespace

// FEAT-GEO-037
AppearancePtr readAppearancePoint(const core::IConfigProviderPtr& config,
                                  const MarkPictureListPtr& markPictureList,
                                  const IAppearanceOfMarkFactory& factory,
                                  const AppearancePtr& defaultAppearance) {
    Color32 textColor = kTextColor;
    Color32 textBgColor = kTextBgColor;
    int fontSize = 11;
    if (const auto caption = facetOf<IAppearancePointCaption>(defaultAppearance)) {
        textColor = caption->textColor();
        textBgColor = caption->textBgColor();
        fontSize = caption->fontSize();
    }
    int markerSize = 32;
    QString picName;
    MarkPicturePtr pic;
    if (const auto icon = facetOf<IAppearancePointIcon>(defaultAppearance)) {
        markerSize = icon->markerSize();
        picName = icon->picName();
        pic = icon->pic();
    }
    if (config) {
        picName = config->readString(u"IconName", picName);
        if (!pic || pic->name() != picName) {
            pic = markPictureList ? markPictureList->findByName(picName) : nullptr;
        }
        textColor = core::readColor32(config, u"TextColor", textColor);
        textBgColor = core::readColor32(config, u"ShadowColor", textBgColor);
        fontSize = config->readInt(u"FontSize", fontSize);
        markerSize = config->readInt(u"IconSize", markerSize);
    }
    return factory.createPointAppearance(textColor, textBgColor, fontSize, picName, pic, markerSize);
}

void writeAppearancePoint(const core::IConfigWriterPtr& config, const AppearancePtr& appearance) {
    if (!config) {
        return;
    }
    if (const auto caption = facetOf<IAppearancePointCaption>(appearance)) {
        core::writeColor32(*config, u"TextColor", caption->textColor());
        core::writeColor32(*config, u"ShadowColor", caption->textBgColor());
        config->writeInt(u"FontSize", caption->fontSize());
    }
    if (const auto icon = facetOf<IAppearancePointIcon>(appearance)) {
        config->writeString(u"IconName", icon->picName());
        config->writeInt(u"IconSize", icon->markerSize());
    }
}

AppearancePtr readAppearanceLine(const core::IConfigProviderPtr& config,
                                 const IAppearanceOfMarkFactory& factory,
                                 const AppearancePtr& defaultAppearance) {
    Color32 lineColor = kWhite;
    int lineWidth = 1;
    if (const auto line = facetOf<IAppearanceLine>(defaultAppearance)) {
        lineColor = line->lineColor();
        lineWidth = line->lineWidth();
    }
    if (config) {
        lineColor = core::readColor32(config, u"LineColor", lineColor);
        lineWidth = config->readInt(u"LineWidth", lineWidth);
    }
    return factory.createLineAppearance(lineColor, lineWidth);
}

void writeAppearanceLine(const core::IConfigWriterPtr& config, const AppearancePtr& appearance) {
    if (!config) {
        return;
    }
    if (const auto line = facetOf<IAppearanceLine>(appearance)) {
        core::writeColor32(*config, u"LineColor", line->lineColor());
        config->writeInt(u"LineWidth", line->lineWidth());
    }
}

AppearancePtr readAppearancePolygon(const core::IConfigProviderPtr& config,
                                    const IAppearanceOfMarkFactory& factory,
                                    const AppearancePtr& defaultAppearance) {
    Color32 lineColor = kWhite;
    int lineWidth = 1;
    if (const auto border = facetOf<IAppearancePolygonBorder>(defaultAppearance)) {
        lineColor = border->lineColor();
        lineWidth = border->lineWidth();
    }
    Color32 fillColor = kFillColor;
    if (const auto fill = facetOf<IAppearancePolygonFill>(defaultAppearance)) {
        fillColor = fill->fillColor();
    }
    if (config) {
        lineColor = core::readColor32(config, u"LineColor", lineColor);
        lineWidth = config->readInt(u"LineWidth", lineWidth);
        fillColor = core::readColor32(config, u"FillColor", fillColor);
    }
    return factory.createPolygonAppearance(lineColor, lineWidth, fillColor);
}

void writeAppearancePolygon(const core::IConfigWriterPtr& config, const AppearancePtr& appearance) {
    if (!config) {
        return;
    }
    if (const auto border = facetOf<IAppearancePolygonBorder>(appearance)) {
        core::writeColor32(*config, u"LineColor", border->lineColor());
        config->writeInt(u"LineWidth", border->lineWidth());
    }
    if (const auto fill = facetOf<IAppearancePolygonFill>(appearance)) {
        core::writeColor32(*config, u"FillColor", fill->fillColor());
    }
}

} // namespace cg::vector
