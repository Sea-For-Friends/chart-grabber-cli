// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/ContentType/u_ContentConverterBitmap.pas:54-78 (TContentConverterBitmap).

#include "cg/bitmap/bitmap_tile_save_load.h"
#include "cg/maptype/content_converter.h"

namespace cg::maptype {

class ContentConverterBitmap final : public IContentConverter {
public:
    // :54-73. Null arguments throw std::runtime_error with the legacy texts.
    ContentConverterBitmap(bitmap::BitmapTileLoaderPtr source, bitmap::BitmapTileSaverPtr target);

    [[nodiscard]] bool isSimpleCopy() const override { return false; } // TContentConverterBase :85-88
    [[nodiscard]] std::optional<QByteArray> convert(const QByteArray& data) const override; // :75-78

private:
    bitmap::BitmapTileLoaderPtr source_;
    bitmap::BitmapTileSaverPtr target_;
};

} // namespace cg::maptype
