// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/category.h"

#include <utility>

namespace cg::vector {

Category::Category(QString name) : name_(std::move(name)) {}

QString Category::name() const {
    return name_;
}

bool Category::isSame(const ICategory* category) const {
    return category == this;
}

bool Category::isEqual(const ICategory* category) const {
    if (category == nullptr) {
        return false;
    }
    if (category == this) {
        return true;
    }
    return category->name() == name_; // exact, case-sensitive (Delphi `<>`)
}

} // namespace cg::vector
