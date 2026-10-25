// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/BitmapTileSaveLoad/u_BitmapTileFreeImage.pas:41-127 and :138-147 (the loader, its six
// subclasses, the saver base, the BMP and PNG savers) and :195-346, :350-511, :532-556 (their bodies)
// (docs/specs/bitmap.md §2.5). FreeImage itself is not used: the loaders are QImageReader, the PNG saver
// is libpng, the BMP saver is hand-written (T-0177). The WebP savers (libwebp) and the refusing GIF and TIFF
// savers are T-0162 (:129-136, :149-174, :515-528, :560-611), their bodies are in
// bitmap_tile_free_image_webp.cpp. Feature: FEAT-RND-041 (the loader base shares this id with the PNG saver),
// FEAT-RND-038 (BMP saver), FEAT-RND-044 (WebP savers), FEAT-RND-039 (GIF saver), FEAT-RND-043 (TIFF saver)
//
// No state between calls: one object may be used from several threads.

#include "cg/bitmap/bitmap32_buffer_factory.h"
#include "cg/bitmap/bitmap_tile_save_load.h"

#include <QByteArray>
#include <QtGlobal>

#include <memory>

namespace cg::bitmap {

// FEAT-RND-041
class BitmapTileFreeImageLoader : public IBitmapTileLoader { // TBitmapTileFreeImageLoader
public:
    explicit BitmapTileFreeImageLoader(std::shared_ptr<const IBitmap32StaticFactory> staticFactory);

    // The bytes are sniffed, the format this loader is registered for is never used (the L row of T-0161).
    // Returns nullptr when the buffer factory refuses the size (:230-234); throws BitmapCodecError.
    [[nodiscard]] Bitmap32StaticPtr load(const QByteArray& data) const override;

private:
    std::shared_ptr<const IBitmap32StaticFactory> staticFactory_; // FBitmap32StaticFactory
};

// The subclasses differ in legacy only by the performance-counter name (:270-346).
class BitmapTileFreeImageLoaderBmp final : public BitmapTileFreeImageLoader {
public:
    using BitmapTileFreeImageLoader::BitmapTileFreeImageLoader;
};
class BitmapTileFreeImageLoaderIco final : public BitmapTileFreeImageLoader {
public:
    using BitmapTileFreeImageLoader::BitmapTileFreeImageLoader;
};
class BitmapTileFreeImageLoaderGif final : public BitmapTileFreeImageLoader {
public:
    using BitmapTileFreeImageLoader::BitmapTileFreeImageLoader;
};
class BitmapTileFreeImageLoaderPng final : public BitmapTileFreeImageLoader {
public:
    using BitmapTileFreeImageLoader::BitmapTileFreeImageLoader;
};
class BitmapTileFreeImageLoaderWebp final : public BitmapTileFreeImageLoader {
public:
    using BitmapTileFreeImageLoader::BitmapTileFreeImageLoader;
};
class BitmapTileFreeImageLoaderTiff final : public BitmapTileFreeImageLoader {
public:
    using BitmapTileFreeImageLoader::BitmapTileFreeImageLoader;
};

// Common part of the savers (:102-119): the encoder flag and the bits per pixel (8, 24 or 32).
// `save` of the interface is implemented by each saver.
// NOLINTNEXTLINE(readability-identifier-naming): abstract class with logic, keeps the legacy name
class BitmapTileFreeImageSaver : public IBitmapTileSaver { // TBitmapTileFreeImageSaver
protected:
    BitmapTileFreeImageSaver(int flag, quint8 bitsPerPixel) : flag_(flag), bitsPerPixel_(bitsPerPixel) {}

    [[nodiscard]] int flag() const { return flag_; }
    [[nodiscard]] quint8 bitsPerPixel() const { return bitsPerPixel_; }

private:
    int flag_;            // FFlag
    quint8 bitsPerPixel_; // FBitPerPixel
};

// FEAT-RND-038
class BitmapTileFreeImageSaverBmp final : public BitmapTileFreeImageSaver { // TBitmapTileFreeImageSaverBmp
public:
    BitmapTileFreeImageSaverBmp();

    // Throws BitmapCodecError.
    [[nodiscard]] QByteArray save(const Bitmap32StaticPtr& bitmap) const override;
};

// FEAT-RND-041
class BitmapTileFreeImageSaverPng final : public BitmapTileFreeImageSaver { // TBitmapTileFreeImageSaverPng
public:
    // compress 0 = no compression, 1..9 = zlib level, else BitmapCodecError; bitsPerPixel 24 or 32
    // (8 is not ported: BitmapCodecError).
    BitmapTileFreeImageSaverPng(quint8 compress, int bitsPerPixel);

    // Throws BitmapCodecError.
    [[nodiscard]] QByteArray save(const Bitmap32StaticPtr& bitmap) const override;
};

// FEAT-RND-044
class BitmapTileFreeImageSaverWebp final : public BitmapTileFreeImageSaver { // TBitmapTileFreeImageSaverWebp
public:
    // quality 1..100, else BitmapCodecError("Invalid WebP quality value: <n>") (:566-568)
    explicit BitmapTileFreeImageSaverWebp(quint8 quality);

    // Throws BitmapCodecError.
    [[nodiscard]] QByteArray save(const Bitmap32StaticPtr& bitmap) const override;
};

// FEAT-RND-044
class BitmapTileFreeImageSaverWebpLossless final
    : public BitmapTileFreeImageSaver { // TBitmapTileFreeImageSaverWebpLossless
public:
    BitmapTileFreeImageSaverWebpLossless();

    // Throws BitmapCodecError.
    [[nodiscard]] QByteArray save(const Bitmap32StaticPtr& bitmap) const override;
};

// FEAT-RND-039
class BitmapTileFreeImageSaverGif final : public BitmapTileFreeImageSaver { // TBitmapTileFreeImageSaverGif
public:
    BitmapTileFreeImageSaverGif();

    // Always throws BitmapCodecError("GIF saver is not ported") (the D row of T-0162).
    [[nodiscard]] QByteArray save(const Bitmap32StaticPtr& bitmap) const override;
};

// FEAT-RND-043
class BitmapTileFreeImageSaverTiff final : public BitmapTileFreeImageSaver { // TBitmapTileFreeImageSaverTiff
public:
    BitmapTileFreeImageSaverTiff(quint32 compressionType, int bitsPerPixel);

    // Always throws BitmapCodecError("TIFF saver is not ported") (the D row of T-0162).
    [[nodiscard]] QByteArray save(const Bitmap32StaticPtr& bitmap) const override;
};

} // namespace cg::bitmap
