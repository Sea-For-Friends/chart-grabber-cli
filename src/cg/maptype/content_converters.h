// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: the converter half of Src/ContentType/u_ContentTypeManagerSimple.pas (UpdateConverterMatrix :297-303
// and its four steps) and GetConverter (u_ContentTypeManagerBase.pas:182-188), docs/specs/maptype.md §2.13.5.
// kml <-> kmz entries of ConverterMatrixUpdateFixed (:325-362) are T-0178.

#include "cg/bitmap/content_type_manager_bitmap.h"
#include "cg/core/content_type.h"
#include "cg/maptype/content_converter_matrix.h"

#include <QByteArrayView>

#include <memory>

namespace cg::maptype {

// FEAT-NET-013
// Built once in the constructor; immutable afterwards, so one instance is shared by all download workers.
class ContentConverters {
public:
    ContentConverters(const core::ContentTypeRegistry& types,
                      const std::shared_ptr<const bitmap::ContentTypeManagerBitmap>& bitmaps);

    // GetConverter: null when no converter exists for the pair.
    [[nodiscard]] ContentConverterPtr converter(QByteArrayView sourceType, QByteArrayView targetType) const;

private:
    void updateFixed(const core::ContentTypeRegistry& types);    // :305-388
    void updateSynonyms(const core::ContentTypeRegistry& types); // :390-424
    [[nodiscard]] ContentConverterPtr
    findConverterWithSynonyms(const core::ContentTypeRegistry& types, const QByteArray& sourceType,
                              const QByteArray& targetType) const;        // :426-473
    void updateFixedWithSynonyms(const core::ContentTypeRegistry& types); // :475-509
    void updateBitmaps(const bitmap::ContentTypeManagerBitmap& bitmaps);  // :539-575

    ContentConverterMatrix matrix_;
};

} // namespace cg::maptype
