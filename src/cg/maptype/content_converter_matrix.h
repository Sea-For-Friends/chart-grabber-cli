// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/ContentType/u_ContentConverterMatrix.pas + u_ContentConvertersListByKey.pas (two-level map
// source key -> target key -> converter).

#include "cg/maptype/content_converter.h"

#include <QByteArray>
#include <QByteArrayView>

#include <map>

namespace cg::maptype {

class ContentConverterMatrix {
public:
    // :53-68 / :52-59. A duplicate pair throws std::logic_error (legacy dupError).
    void add(QByteArrayView sourceKey, QByteArrayView targetKey, ContentConverterPtr converter);
    // :93-106 / :82-91. Keys are matched ASCII case-insensitively; null when either key is missing.
    [[nodiscard]] ContentConverterPtr get(QByteArrayView sourceKey, QByteArrayView targetKey) const;

private:
    std::map<QByteArray, std::map<QByteArray, ContentConverterPtr>> bySource_;
};

} // namespace cg::maptype
