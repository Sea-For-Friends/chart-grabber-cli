// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/language_list_static.h"

#include <stdexcept>

namespace cg::core {

LanguageListStatic::LanguageListStatic(const QStringList& codes) {
    for (const QString& c : codes) {
        if (list_.contains(c, Qt::CaseInsensitive)) {
            throw std::invalid_argument("List does not allow duplicates");
        }
        list_.append(c);
    }
}

int LanguageListStatic::count() const {
    return static_cast<int>(list_.size());
}

QString LanguageListStatic::code(int index) const {
    if (index < 0 || index >= list_.size()) {
        throw std::out_of_range("List index out of bounds");
    }
    return list_.at(index);
}

std::optional<int> LanguageListStatic::findCode(const QString& code) const {
    for (qsizetype i = 0; i < list_.size(); ++i) {
        if (list_.at(i).compare(code, Qt::CaseInsensitive) == 0) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

} // namespace cg::core
