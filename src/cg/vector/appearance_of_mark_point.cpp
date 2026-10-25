// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/appearance_of_mark_point.h"

#include <utility>

namespace cg::vector {

AppearanceOfMarkPoint::AppearanceOfMarkPoint(core::HashValue hashCommon, core::HashValue hashCaption,
                                             core::HashValue hashIcon, Color32 textColor, Color32 textBgColor,
                                             int fontSize, int markerSize, QString picName,
                                             MarkPicturePtr pic)
    : hashCommon_(hashCommon), hashCaption_(hashCaption), hashIcon_(hashIcon), textColor_(textColor),
      textBgColor_(textBgColor), fontSize_(fontSize), markerSize_(markerSize), picName_(std::move(picName)),
      pic_(std::move(pic)) {
    if (pic_) {
        picName_ = pic_->name();
    }
}

core::HashValue AppearanceOfMarkPoint::hash() const noexcept {
    return hashCommon_;
}

bool AppearanceOfMarkPoint::isEqual(const IAppearance* value) const {
    return value != nullptr && hashCommon_ == value->hash();
}

core::HashValue AppearanceOfMarkPoint::hashCaption() const noexcept {
    return hashCaption_;
}
Color32 AppearanceOfMarkPoint::textColor() const {
    return textColor_;
}
Color32 AppearanceOfMarkPoint::textBgColor() const {
    return textBgColor_;
}
int AppearanceOfMarkPoint::fontSize() const {
    return fontSize_;
}

core::HashValue AppearanceOfMarkPoint::hashIcon() const noexcept {
    return hashIcon_;
}
int AppearanceOfMarkPoint::markerSize() const {
    return markerSize_;
}
QString AppearanceOfMarkPoint::picName() const {
    return picName_;
}
MarkPicturePtr AppearanceOfMarkPoint::pic() const {
    return pic_;
}

} // namespace cg::vector
