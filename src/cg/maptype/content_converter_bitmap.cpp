// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/content_converter_bitmap.h"

#include <stdexcept>
#include <utility>

namespace cg::maptype {

ContentConverterBitmap::ContentConverterBitmap(bitmap::BitmapTileLoaderPtr source,
                                               bitmap::BitmapTileSaverPtr target)
    : source_(std::move(source)), target_(std::move(target)) {
    // unreachable in legacy (u_ContentTypeManagerSimple.pas:559-566); no register row
    if (!source_) {
        throw std::runtime_error(
            "Can't load bitmap from source type"); // SAS_ERR_CantLoadBitmapFromSourceType :63-66
    }
    if (!target_) {
        throw std::runtime_error(
            "Can't save bitmap to target type"); // SAS_ERR_CantSaveBitmapToTargetType :68-72
    }
}

std::optional<QByteArray> ContentConverterBitmap::convert(const QByteArray& data) const {
    return target_->save(source_->load(data)); // :75-78 Save(Load(data))
}

} // namespace cg::maptype
