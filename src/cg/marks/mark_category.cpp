// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_category.h"

#include <utility>

namespace cg::marks {

MarkCategory::MarkCategory(QString name, bool visible, int afterScale, int beforeScale)
    : name_(std::move(name)), visible_(visible), afterScale_(afterScale), beforeScale_(beforeScale) {}

QString MarkCategory::name() const {
    return name_;
}

bool MarkCategory::isSame(const vector::ICategory* category) const {
    return category == this;
}

bool MarkCategory::isEqual(const vector::ICategory* category) const {
    if (category == nullptr) {
        return false;
    }
    if (category == this) {
        return true;
    }
    if (category->name() != name_) {
        return false;
    }
    const auto* other =
        dynamic_cast<const IMarkCategory*>(category); // Supports(ACategory, IMarkCategory, ...)
    if (other == nullptr) {
        return false;
    }
    return other->visible() == visible_ && other->afterScale() == afterScale_ &&
           other->beforeScale() == beforeScale_;
}

bool MarkCategory::visible() const {
    return visible_;
}

int MarkCategory::afterScale() const {
    return afterScale_;
}

int MarkCategory::beforeScale() const {
    return beforeScale_;
}

} // namespace cg::marks
