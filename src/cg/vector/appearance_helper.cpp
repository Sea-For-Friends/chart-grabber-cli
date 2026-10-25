// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/appearance_helper.h"

#include "cg/core/delphi_text.h"

#include <QLatin1StringView>
#include <QtGlobal>

#include <stdexcept>
#include <utility>

namespace cg::vector {

namespace {

using core::Color32;

// Legacy TColorSetHelper (u_AppearanceHelper.pas:87-104, 106-135, 137-251).
class ColorSetHelper final : public IColorSetHelper {
public:
    void reset() override {
        color_ = 0;
        found_ = false;
    }

    [[nodiscard]] Color32 color() const override { return color_; }

    void setColor(Color32 value) override {
        color_ = value;
        found_ = true;
    }

    [[nodiscard]] bool found() const override { return found_; }

    bool setGpxColorName(const QString& name) override;
    void setKmlColorValue(quint32 value) override;

private:
    Color32 color_ = 0;
    bool found_ = false;
};

// Legacy SetGPXColorName (u_AppearanceHelper.pas:137-243). The table is kept as written: it is
// dispatched on the length of the lower-cased name, and the odd mappings are legacy (LV-6).
bool ColorSetHelper::setGpxColorName(const QString& name) {
    found_ = false;
    color_ = core::clBlack32;
    const QString lower = core::delphiLowerCase(name);

    const auto hit = [this](Color32 value) {
        color_ = value;
        found_ = true;
    };
    const auto is = [&lower](QLatin1StringView wanted) { return lower == wanted; };

    switch (lower.size()) {
    case 3: // Red
        if (is(QLatin1StringView("red"))) {
            hit(core::clRed32);
        }
        break;
    case 4: // Blue, Cyan
        if (is(QLatin1StringView("blue"))) {
            hit(core::clBlue32);
        } else if (is(QLatin1StringView("cyan"))) {
            hit(core::clAqua32);
        }
        break;
    case 5: // Black, Green, White
        if (is(QLatin1StringView("black"))) {
            hit(core::clBlack32);
        } else if (is(QLatin1StringView("green"))) {
            hit(core::clGreen32);
        } else if (is(QLatin1StringView("white"))) {
            hit(core::clWhite32);
        }
        break;
    case 6: // Yellow (light yellow, LV-6)
        if (is(QLatin1StringView("yellow"))) {
            hit(core::clLightYellow32);
        }
        break;
    case 7: // DarkRed, Magenta (dark magenta, LV-6)
        if (is(QLatin1StringView("darkred"))) {
            hit(core::clDarkRed32);
        } else if (is(QLatin1StringView("magenta"))) {
            hit(core::clDarkMagenta32);
        }
        break;
    case 8: // DarkBlue, DarkCyan, DarkGray
        if (is(QLatin1StringView("darkblue"))) {
            hit(core::clDarkBlue32);
        } else if (is(QLatin1StringView("darkcyan"))) {
            hit(core::clDarkCyan32);
        } else if (is(QLatin1StringView("darkgray"))) {
            hit(core::clDarkGray32);
        }
        break;
    case 9: // DarkGreen, LightGray
        if (is(QLatin1StringView("darkgreen"))) {
            hit(core::clDarkGreen32);
        } else if (is(QLatin1StringView("lightgray"))) {
            hit(core::clLightGray32);
        }
        break;
    case 10: // DarkYellow (yellow, LV-6)
        if (is(QLatin1StringView("darkyellow"))) {
            hit(core::clYellow32);
        }
        break;
    case 11: // DarkMagenta, Transparent
        if (is(QLatin1StringView("darkmagenta"))) {
            hit(core::clDarkMagenta32);
        } else if (is(QLatin1StringView("transparent"))) {
            hit(Color32{0x00000000U});
        }
        break;
    default:
        break;
    }

    return found_;
}

// Legacy SetKMLColorValue (u_AppearanceHelper.pas:245-251): R and B bytes swapped; alpha and green kept.
void ColorSetHelper::setKmlColorValue(quint32 value) {
    color_ = (value & 0xFF00FF00U) | ((value & 0x00FF0000U) >> 16) | ((value & 0x000000FFU) << 16);
    found_ = true;
}

// Legacy TIntegerSetHelper (u_AppearanceHelper.pas:253-298).
class IntegerSetHelper final : public IIntegerSetHelper {
public:
    void reset() override {
        value_ = 0;
        found_ = false;
    }

    [[nodiscard]] int value() const override { return value_; }

    void setValue(int value) override {
        value_ = value;
        found_ = true;
    }

    [[nodiscard]] bool found() const override { return found_; }

private:
    int value_ = 0;
    bool found_ = false;
};

// Legacy TIconSetHelper (u_AppearanceHelper.pas:300-369).
class IconSetHelper final : public IIconSetHelper {
public:
    explicit IconSetHelper(MarkPictureListPtr markPictureList)
        : markPictureList_(std::move(markPictureList)) {}

    void reset() override {
        icon_ = nullptr;
        found_ = false;
    }

    [[nodiscard]] MarkPicturePtr icon() const override { return icon_; }

    // Found is set even for a null picture (legacy SetIcon, :365-369).
    void setIcon(const MarkPicturePtr& icon) override {
        icon_ = icon;
        found_ = true;
    }

    [[nodiscard]] bool found() const override { return found_; }

    // Legacy SetByName (:347-363): no list or no match keeps the previous icon and found flag.
    bool setByName(const QString& name) override {
        if (!markPictureList_) {
            return false;
        }
        const MarkPicturePtr picture = markPictureList_->findByName(name);
        if (!picture) {
            return false;
        }
        found_ = true;
        icon_ = picture;
        return true;
    }

private:
    MarkPictureListPtr markPictureList_;
    MarkPicturePtr icon_;
    bool found_ = false;
};

} // namespace

// Legacy TAppearanceHelper.Create (u_AppearanceHelper.pas:373-396).
AppearanceHelper::AppearanceHelper(ImportPointParamsPtr pointParams, ImportLineParamsPtr lineParams,
                                   ImportPolyParamsPtr polyParams, MarkPictureListPtr markPictureList,
                                   AppearanceOfMarkFactoryPtr appearanceOfMarkFactory)
    : pointParams_(std::move(pointParams)), lineParams_(std::move(lineParams)),
      polyParams_(std::move(polyParams)), appearanceOfMarkFactory_(std::move(appearanceOfMarkFactory)),
      textColor_(std::make_unique<ColorSetHelper>()), lineColor_(std::make_unique<ColorSetHelper>()),
      fillColor_(std::make_unique<ColorSetHelper>()), lineWidth_(std::make_unique<IntegerSetHelper>()),
      textSize_(std::make_unique<IntegerSetHelper>()), iconSize_(std::make_unique<IntegerSetHelper>()),
      icon_(std::make_unique<IconSetHelper>(std::move(markPictureList))) {
    Q_ASSERT(appearanceOfMarkFactory_ != nullptr);
}

// Legacy TAppearanceHelper.Reset (u_AppearanceHelper.pas:580-589).
void AppearanceHelper::reset() {
    textColor_->reset();
    lineColor_->reset();
    fillColor_->reset();
    lineWidth_->reset();
    textSize_->reset();
    iconSize_->reset();
    icon_->reset();
}

IColorSetHelper& AppearanceHelper::textColor() {
    return *textColor_;
}

IColorSetHelper& AppearanceHelper::lineColor() {
    return *lineColor_;
}

IColorSetHelper& AppearanceHelper::fillColor() {
    return *fillColor_;
}

IIntegerSetHelper& AppearanceHelper::lineWidth() {
    return *lineWidth_;
}

IIntegerSetHelper& AppearanceHelper::textSize() {
    return *textSize_;
}

IIntegerSetHelper& AppearanceHelper::iconSize() {
    return *iconSize_;
}

IIconSetHelper& AppearanceHelper::icon() {
    return *icon_;
}

// Legacy RedefinePointAppearance (u_AppearanceHelper.pas:398-455). Property pairing: icon -> pic and
// picName (IsForcePicName); text colour -> textColor; fill colour -> textBgColor (IsForceTextBgColor);
// text size -> fontSize; icon size -> markerSize.
AppearancePtr AppearanceHelper::redefinePointAppearance() const {
    if (!pointParams_) {
        return nullptr;
    }
    const AppearancePointCaptionPtr caption = pointParams_->captionAppearance();
    const AppearancePointIconPtr iconParams = pointParams_->iconAppearance();
    // unreachable in legacy (every TImport*Params caller passes a factory-built appearance:
    // u_VectorItemTreeMarksDb.pas:124-137, u_ZmpInfo.pas:1033-1059, fr_MarksGeneralOptions.pas:275-292); no
    // register row
    if (!caption || !iconParams) {
        throw std::invalid_argument("null point facet");
    }

    MarkPicturePtr pic;
    QString picName;
    if (!pointParams_->isForcePicName() && icon_->found()) {
        pic = icon_->icon();
        picName = pic ? pic->name() : QString();
    } else {
        pic = iconParams->pic();
        picName = iconParams->picName();
    }

    const Color32 textColor = (!pointParams_->isForceTextColor() && textColor_->found())
                                  ? textColor_->color()
                                  : caption->textColor();
    const Color32 textBgColor = (!pointParams_->isForceTextBgColor() && fillColor_->found())
                                    ? fillColor_->color()
                                    : caption->textBgColor();
    const int fontSize =
        (!pointParams_->isForceFontSize() && textSize_->found()) ? textSize_->value() : caption->fontSize();
    const int markerSize = (!pointParams_->isForceMarkerSize() && iconSize_->found())
                               ? iconSize_->value()
                               : iconParams->markerSize();

    return appearanceOfMarkFactory_->createPointAppearance(textColor, textBgColor, fontSize, picName, pic,
                                                           markerSize);
}

// Legacy RedefineLineAppearance (u_AppearanceHelper.pas:457-482). LV-1: the force flags are the
// ones ImportLineParams provides (import_config.cpp), reproduced as they are.
AppearancePtr AppearanceHelper::redefineLineAppearance() const {
    if (!lineParams_) {
        return nullptr;
    }
    const AppearanceLinePtr line = lineParams_->lineAppearance();
    // unreachable in legacy (every TImport*Params caller passes a factory-built appearance:
    // u_VectorItemTreeMarksDb.pas:124-137, u_ZmpInfo.pas:1033-1059, fr_MarksGeneralOptions.pas:275-292); no
    // register row
    if (!line) {
        throw std::invalid_argument("null line facet");
    }

    const Color32 lineColor =
        (!lineParams_->isForceLineColor() && lineColor_->found()) ? lineColor_->color() : line->lineColor();
    const int lineWidth =
        (!lineParams_->isForceLineWidth() && lineWidth_->found()) ? lineWidth_->value() : line->lineWidth();

    return appearanceOfMarkFactory_->createLineAppearance(lineColor, lineWidth);
}

// Legacy RedefinePolygonAppearance (u_AppearanceHelper.pas:484-518). Border colour and width come
// from the line helpers; fill colour from the fill helper.
AppearancePtr AppearanceHelper::redefinePolygonAppearance() const {
    if (!polyParams_) {
        return nullptr;
    }
    const AppearancePolygonBorderPtr border = polyParams_->borderAppearance();
    const AppearancePolygonFillPtr fill = polyParams_->fillAppearance();
    // unreachable in legacy (every TImport*Params caller passes a factory-built appearance:
    // u_VectorItemTreeMarksDb.pas:124-137, u_ZmpInfo.pas:1033-1059, fr_MarksGeneralOptions.pas:275-292); no
    // register row
    if (!border || !fill) {
        throw std::invalid_argument("null polygon facet");
    }

    const Color32 lineColor =
        (!polyParams_->isForceLineColor() && lineColor_->found()) ? lineColor_->color() : border->lineColor();
    const int lineWidth =
        (!polyParams_->isForceLineWidth() && lineWidth_->found()) ? lineWidth_->value() : border->lineWidth();
    const Color32 fillColor =
        (!polyParams_->isForceFillColor() && fillColor_->found()) ? fillColor_->color() : fill->fillColor();

    return appearanceOfMarkFactory_->createPolygonAppearance(lineColor, lineWidth, fillColor);
}

// Legacy GetHasPointAppearance (u_AppearanceHelper.pas:525-533).
bool AppearanceHelper::hasPointAppearance() const {
    return textColor_->found() || fillColor_->found() || textSize_->found() || iconSize_->found() ||
           icon_->found();
}

// Legacy GetHasLineAppearance (u_AppearanceHelper.pas:535-540).
bool AppearanceHelper::hasLineAppearance() const {
    return lineColor_->found() || lineWidth_->found();
}

// Legacy GetHasPolygonAppearance (u_AppearanceHelper.pas:542-548).
bool AppearanceHelper::hasPolygonAppearance() const {
    return lineColor_->found() || fillColor_->found() || lineWidth_->found();
}

} // namespace cg::vector
