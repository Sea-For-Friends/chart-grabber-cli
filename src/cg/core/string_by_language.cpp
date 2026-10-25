// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/string_by_language.h"

#include <stdexcept>

namespace cg::core {

StringByLanguageWithStaticList::StringByLanguageWithStaticList(const QStringList& values) : values_(values) {}

QString StringByLanguageWithStaticList::getString(int langIndex) const {
    if (langIndex > 0 && langIndex < values_.size()) {
        return values_.at(langIndex);
    }
    return getDefault();
}

QString StringByLanguageWithStaticList::getDefault() const {
    if (values_.isEmpty()) {
        throw std::out_of_range("List index out of bounds");
    }
    return values_.at(0);
}

} // namespace cg::core
