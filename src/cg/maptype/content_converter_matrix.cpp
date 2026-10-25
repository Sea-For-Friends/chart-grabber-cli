// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/content_converter_matrix.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cg::maptype {

namespace {

// Lower-cased ASCII key; false for a non-ASCII key (as core::ContentTypeRegistry::infoByType).
bool normalize(QByteArrayView key, QByteArray& out) {
    if (!std::ranges::all_of(key, [](char ch) { return static_cast<unsigned char>(ch) < 0x80; })) {
        return false;
    }
    out = QByteArray(key.constData(), key.size()).toLower();
    return true;
}

} // namespace

void ContentConverterMatrix::add(QByteArrayView sourceKey, QByteArrayView targetKey,
                                 ContentConverterPtr converter) {
    QByteArray source;
    QByteArray target;
    if (!normalize(sourceKey, source) || !normalize(targetKey, target)) {
        throw std::logic_error("ContentConverterMatrix: non-ASCII key");
    }
    // unreachable: every call is guarded by get() == null (u_ContentTypeManagerSimple.pas:297-575)
    if (!bySource_[source].emplace(std::move(target), std::move(converter)).second) {
        throw std::logic_error("ContentConverterMatrix: duplicate pair"); // dupError
    }
}

ContentConverterPtr ContentConverterMatrix::get(QByteArrayView sourceKey, QByteArrayView targetKey) const {
    QByteArray source;
    QByteArray target;
    if (!normalize(sourceKey, source) || !normalize(targetKey, target)) {
        return nullptr;
    }
    const auto sourceIt = bySource_.find(source);
    if (sourceIt == bySource_.end()) {
        return nullptr;
    }
    const auto targetIt = sourceIt->second.find(target);
    return targetIt == sourceIt->second.end() ? nullptr : targetIt->second;
}

} // namespace cg::maptype
