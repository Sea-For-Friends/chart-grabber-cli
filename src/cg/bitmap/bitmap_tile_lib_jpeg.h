// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/BitmapTileSaveLoad/u_BitmapTileLibJpeg.pas:39-77 (TLibJpegTileLoader, TLibJpegTileSaver)
// with the parts of Includes/LibJpeg/LibJpegRead.pas, LibJpegWrite.pas, LibJpegErrorHandler.pas and
// LibJpegInOutDataManager.pas they drive (docs/specs/bitmap.md §2.4).
// Feature: FEAT-RND-042 (codec half; the callers are the converters and the codec registry)
//
// No state between calls: one object may be used from several threads.

#include "cg/bitmap/bitmap32_buffer_factory.h"
#include "cg/bitmap/bitmap_tile_save_load.h"

#include <QByteArray>
#include <QtGlobal>

#include <memory>

namespace cg::bitmap {

// FEAT-RND-042
class LibJpegTileLoader final : public IBitmapTileLoader { // TLibJpegTileLoader
public:
    explicit LibJpegTileLoader(std::shared_ptr<const IBitmap32StaticFactory> staticFactory);

    // Returns nullptr when the buffer factory refuses the size (:133-138); throws BitmapCodecError.
    [[nodiscard]] Bitmap32StaticPtr load(const QByteArray& data) const override;

private:
    std::shared_ptr<const IBitmap32StaticFactory> staticFactory_; // FBitmap32StaticFactory
};

// FEAT-RND-042
class LibJpegTileSaver final : public IBitmapTileSaver { // TLibJpegTileSaver
public:
    explicit LibJpegTileSaver(quint8 quality);

    // Throws BitmapCodecError.
    [[nodiscard]] QByteArray save(const Bitmap32StaticPtr& bitmap) const override;

private:
    quint8 quality_; // FCompressionQuality
};

} // namespace cg::bitmap
