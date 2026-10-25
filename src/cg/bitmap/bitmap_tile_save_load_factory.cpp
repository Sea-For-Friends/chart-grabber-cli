// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: see bitmap_tile_save_load_factory.h (u_BitmapTileSaveLoadFactory.pas:143-335).

#include "cg/bitmap/bitmap_tile_save_load_factory.h"

#include "cg/bitmap/bitmap_codec_error.h"
#include "cg/bitmap/bitmap_tile_free_image.h"
#include "cg/bitmap/bitmap_tile_lib_jpeg.h"

#include <QtGlobal>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace cg::bitmap {

namespace {

// FreeImage.pas:623-628: the TIFF compression flags that the TIFF saver keeps (TIFF_NONE, TIFF_ADOBE_DEFLATE,
// TIFF_LZW, TIFF_JPEG).
constexpr quint32 kTiffNone = 0x0800;
constexpr quint32 kTiffAdobeDeflate = 0x0400;
constexpr quint32 kTiffLzw = 0x4000;
constexpr quint32 kTiffJpeg = 0x8000;

} // namespace

BitmapTileSaveLoadFactory::BitmapTileSaveLoadFactory(
    std::shared_ptr<const IBitmap32StaticFactory> staticFactory)
    : staticFactory_(std::move(staticFactory)) {
    // unreachable in legacy (u_GlobalState.pas:568 passes the factory it just built; :147 asserts); no
    // register row
    if (!staticFactory_) {
        throw std::invalid_argument("BitmapTileSaveLoadFactory: null factory");
    }
}

BitmapTileLoaderPtr BitmapTileSaveLoadFactory::createBmpLoader() const {
    return std::make_shared<BitmapTileFreeImageLoaderBmp>(staticFactory_);
}

BitmapTileSaverPtr BitmapTileSaveLoadFactory::createBmpSaver() const {
    return std::make_shared<BitmapTileFreeImageSaverBmp>();
}

BitmapTileLoaderPtr BitmapTileSaveLoadFactory::createGifLoader() const {
    return std::make_shared<BitmapTileFreeImageLoaderGif>(staticFactory_);
}

BitmapTileSaverPtr BitmapTileSaveLoadFactory::createGifSaver() const {
    // The GIF saver refuses on save (T-0162, D-135); the legacy converter argument is group O-3.
    return std::make_shared<BitmapTileFreeImageSaverGif>();
}

BitmapTileLoaderPtr BitmapTileSaveLoadFactory::createIcoLoader() const {
    return std::make_shared<BitmapTileFreeImageLoaderIco>(staticFactory_);
}

BitmapTileLoaderPtr BitmapTileSaveLoadFactory::createPngLoader() const {
    return std::make_shared<BitmapTileFreeImageLoaderPng>(staticFactory_);
}

BitmapTileSaverPtr BitmapTileSaveLoadFactory::createPngSaver(ImageColorBitPerPix colorDepth,
                                                             quint8 compressionLevel) const {
    int bitsPerPixel = 0;
    switch (colorDepth) {
    case ImageColorBitPerPix::I8bpp:
        // group O-3: the 8-bit palette saver (libimagequant) is not ported; its only caller is the export
        // dialog (fr_ImageFormatSelect.pas:371). Register row: the D row of T-0163.
        throw BitmapCodecError("PNG 8 bpp saver is not ported");
    case ImageColorBitPerPix::I24bpp:
        bitsPerPixel = 24;
        break;
    case ImageColorBitPerPix::I32bpp:
        bitsPerPixel = 32;
        break;
    default:
        // unreachable in legacy (u_ContentTypeManagerSimple.pas:701, fr_ImageFormatSelect.pas:371-373 pass
        // enum literals); no register row
        throw BitmapCodecError("Unexpected ColorDepth value: \"" +
                               std::to_string(static_cast<int>(colorDepth)) + "\"");
    }
    return std::make_shared<BitmapTileFreeImageSaverPng>(compressionLevel, bitsPerPixel);
}

BitmapTileLoaderPtr BitmapTileSaveLoadFactory::createJpegLoader() const {
    return std::make_shared<LibJpegTileLoader>(staticFactory_);
}

BitmapTileSaverPtr BitmapTileSaveLoadFactory::createJpegSaver(quint8 compressionQuality) const {
    return std::make_shared<LibJpegTileSaver>(compressionQuality);
}

BitmapTileLoaderPtr BitmapTileSaveLoadFactory::createTiffLoader() const {
    return std::make_shared<BitmapTileFreeImageLoaderTiff>(staticFactory_);
}

BitmapTileSaverPtr BitmapTileSaveLoadFactory::createTiffSaver(ImageColorBitPerPix colorDepth,
                                                              ImageTiffCompression compression) const {
    int bitsPerPixel = 0;
    switch (colorDepth) {
    case ImageColorBitPerPix::I8bpp:
        bitsPerPixel = 8;
        break;
    case ImageColorBitPerPix::I24bpp:
        bitsPerPixel = 24;
        break;
    case ImageColorBitPerPix::I32bpp:
        bitsPerPixel = 32;
        break;
    default:
        // unreachable in legacy (u_ContentTypeManagerSimple.pas:751 is the only caller and passes i32bpp); no
        // register row
        throw BitmapCodecError("Unexpected ColorDepth value: \"" +
                               std::to_string(static_cast<int>(colorDepth)) + "\"");
    }
    quint32 flag = 0;
    switch (compression) {
    case ImageTiffCompression::None:
        flag = kTiffNone;
        break;
    case ImageTiffCompression::Deflate:
        flag = kTiffAdobeDeflate;
        break;
    case ImageTiffCompression::Lzw:
        flag = kTiffLzw;
        break;
    case ImageTiffCompression::Jpeg:
        flag = kTiffJpeg;
        break;
    default:
        // unreachable in legacy (u_ContentTypeManagerSimple.pas:751 is the only caller and passes
        // itcDeflate); no register row
        throw BitmapCodecError("Unexpected CompressionType value: \"" +
                               std::to_string(static_cast<int>(compression)) + "\"");
    }
    return std::make_shared<BitmapTileFreeImageSaverTiff>(flag, bitsPerPixel);
}

BitmapTileLoaderPtr BitmapTileSaveLoadFactory::createWebpLoader() const {
    return std::make_shared<BitmapTileFreeImageLoaderWebp>(staticFactory_);
}

BitmapTileSaverPtr BitmapTileSaveLoadFactory::createWebpSaver(quint8 quality) const {
    return std::make_shared<BitmapTileFreeImageSaverWebp>(quality);
}

BitmapTileSaverPtr BitmapTileSaveLoadFactory::createWebpLosslessSaver() const {
    return std::make_shared<BitmapTileFreeImageSaverWebpLossless>();
}

} // namespace cg::bitmap
