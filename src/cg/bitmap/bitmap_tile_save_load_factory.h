// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/BitmapTileSaveLoad/i_BitmapTileSaveLoadFactory.pas:31-105 (the two enums and the interface) and
// Src/BitmapTileSaveLoad/u_BitmapTileSaveLoadFactory.pas:36-335 (TBitmapTileSaveLoadFactory),
// docs/specs/bitmap.md §2.6. Not ported in stage 0: the 32-to-8 converter and the format-id provider of the
// constructor (:150-155), the performance counter lists (GetValidPerfCounterList :131-138), the 8-bit PNG
// branch (group O-3, a refusal here).

#include "cg/bitmap/bitmap32_buffer_factory.h"
#include "cg/bitmap/bitmap_tile_save_load.h"

#include <QtGlobal>

#include <cstdint>
#include <memory>

namespace cg::bitmap {

enum class ImageColorBitPerPix : std::uint8_t { I8bpp = 0, I24bpp = 1, I32bpp = 2 }; // TImageColorBitPerPix
enum class ImageTiffCompression : std::uint8_t { None, Deflate, Lzw, Jpeg };         // TImageTiffCompression

// Default arguments are those of the legacy interface: PNG i32bpp level 6, JPEG 75, TIFF i32bpp deflate,
// WebP 75.
class IBitmapTileSaveLoadFactory { // IBitmapTileSaveLoadFactory
public:
    virtual ~IBitmapTileSaveLoadFactory() = default;

    // BMP
    [[nodiscard]] virtual BitmapTileLoaderPtr createBmpLoader() const = 0;
    [[nodiscard]] virtual BitmapTileSaverPtr createBmpSaver() const = 0;

    // GIF
    [[nodiscard]] virtual BitmapTileLoaderPtr createGifLoader() const = 0;
    [[nodiscard]] virtual BitmapTileSaverPtr createGifSaver() const = 0;

    // ICO (loader only)
    [[nodiscard]] virtual BitmapTileLoaderPtr createIcoLoader() const = 0;

    // PNG
    [[nodiscard]] virtual BitmapTileLoaderPtr createPngLoader() const = 0;
    [[nodiscard]] virtual BitmapTileSaverPtr
    createPngSaver(ImageColorBitPerPix colorDepth = ImageColorBitPerPix::I32bpp,
                   quint8 compressionLevel = 6) const = 0;

    // JPEG
    [[nodiscard]] virtual BitmapTileLoaderPtr createJpegLoader() const = 0;
    [[nodiscard]] virtual BitmapTileSaverPtr createJpegSaver(quint8 compressionQuality = 75) const = 0;

    // TIFF
    [[nodiscard]] virtual BitmapTileLoaderPtr createTiffLoader() const = 0;
    [[nodiscard]] virtual BitmapTileSaverPtr
    createTiffSaver(ImageColorBitPerPix colorDepth = ImageColorBitPerPix::I32bpp,
                    ImageTiffCompression compression = ImageTiffCompression::Deflate) const = 0;

    // WebP
    [[nodiscard]] virtual BitmapTileLoaderPtr createWebpLoader() const = 0;
    [[nodiscard]] virtual BitmapTileSaverPtr createWebpSaver(quint8 quality = 75) const = 0;
    [[nodiscard]] virtual BitmapTileSaverPtr createWebpLosslessSaver() const = 0;
};

// Creators build the classes of bitmap_tile_lib_jpeg.h and bitmap_tile_free_image.h with the legacy
// parameters. The defaults live on IBitmapTileSaveLoadFactory: calls through this concrete class pass every
// argument.
class BitmapTileSaveLoadFactory final : public IBitmapTileSaveLoadFactory { // TBitmapTileSaveLoadFactory
public:
    explicit BitmapTileSaveLoadFactory(std::shared_ptr<const IBitmap32StaticFactory> staticFactory);

    [[nodiscard]] BitmapTileLoaderPtr createBmpLoader() const override;
    [[nodiscard]] BitmapTileSaverPtr createBmpSaver() const override;
    [[nodiscard]] BitmapTileLoaderPtr createGifLoader() const override;
    [[nodiscard]] BitmapTileSaverPtr createGifSaver() const override;
    [[nodiscard]] BitmapTileLoaderPtr createIcoLoader() const override;
    [[nodiscard]] BitmapTileLoaderPtr createPngLoader() const override;
    [[nodiscard]] BitmapTileSaverPtr createPngSaver(ImageColorBitPerPix colorDepth,
                                                    quint8 compressionLevel) const override;
    [[nodiscard]] BitmapTileLoaderPtr createJpegLoader() const override;
    [[nodiscard]] BitmapTileSaverPtr createJpegSaver(quint8 compressionQuality) const override;
    [[nodiscard]] BitmapTileLoaderPtr createTiffLoader() const override;
    [[nodiscard]] BitmapTileSaverPtr createTiffSaver(ImageColorBitPerPix colorDepth,
                                                     ImageTiffCompression compression) const override;
    [[nodiscard]] BitmapTileLoaderPtr createWebpLoader() const override;
    [[nodiscard]] BitmapTileSaverPtr createWebpSaver(quint8 quality) const override;
    [[nodiscard]] BitmapTileSaverPtr createWebpLosslessSaver() const override;

private:
    std::shared_ptr<const IBitmap32StaticFactory> staticFactory_; // FBitmap32StaticFactory
};

} // namespace cg::bitmap
