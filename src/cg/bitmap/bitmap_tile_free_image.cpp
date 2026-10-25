// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/BitmapTileSaveLoad/u_BitmapTileFreeImage.pas:195-268 (loader), :350-371 and :373-495 (saver
// base, the 32 and 24 bit branches), :499-511 (BMP), :532-556 (PNG); the PNG encoding is the one of
// FreeImage's PluginPNG.cpp as listed in docs/specs/bitmap.md §2.5 (unverified against the shipped DLL).
// Feature: FEAT-RND-041, FEAT-RND-038
//
// libpng reports every error through error_fn, which must not return. The port does a longjmp to the call
// that owns the libpng structs; the function that contains setjmp owns only trivial locals and the C++
// exception is thrown by its caller, never through libpng frames.

#include "cg/bitmap/bitmap_tile_free_image.h"

#include "cg/bitmap/bitmap_codec_error.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QPoint>

#include <png.h>

#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <zlib.h>

namespace cg::bitmap {

namespace {

constexpr int kPngZNoCompression = 0x0100; // FreeImage.pas:598 PNG_Z_NO_COMPRESSION

// FreeImage_Save/pHYs default of 72 dpi, in pixels per metre (PluginPNG.cpp, spec §2.5)
constexpr png_uint_32 kPixelsPerMeter = 2835;

void pngError(png_structp png, png_const_charp) {
    std::longjmp(png_jmpbuf(png), 1);
}

void pngWarning(png_structp, png_const_charp) {}

// png_set_write_fn callback: appends to the vector given as io pointer. The exception of the vector is
// caught here and turned into png_error after the catch scope has ended.
void pngWrite(png_structp png, png_bytep data, png_size_t length) {
    auto* out = static_cast<std::vector<unsigned char>*>(png_get_io_ptr(png));
    bool ok = true;
    try {
        out->insert(out->end(), data, data + length);
    } catch (const std::bad_alloc&) {
        ok = false;
    }
    if (!ok) {
        png_error(png, "out of memory");
    }
}

void pngFlush(png_structp) {}

// One encode: rows of `rowBytes` bytes (B, G, R[, A]), colour type RGB or RGBA. Returns false on a libpng
// error. Only trivial locals live across setjmp.
bool encodePng(const unsigned char* pixels, std::size_t rowBytes, int width, int height, int colorType,
               int level, std::vector<unsigned char>& out) {
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, pngError, pngWarning);
    if (png == nullptr) {
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_write_struct(&png, nullptr);
        return false;
    }
    if (setjmp(png_jmpbuf(png)) != 0) {
        png_destroy_write_struct(&png, &info);
        return false;
    }
    png_set_write_fn(png, &out, pngWrite, pngFlush);
    png_set_IHDR(png, info, static_cast<png_uint_32>(width), static_cast<png_uint_32>(height), 8, colorType,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_pHYs(png, info, kPixelsPerMeter, kPixelsPerMeter, PNG_RESOLUTION_METER);
    // spec §2.5 gives these two under "pixel depth >= 16": ours is 24 or 32
    png_set_compression_level(png, level);
    png_set_compression_strategy(png, Z_FILTERED);
    png_set_filter(png, 0, PNG_FILTER_NONE | PNG_FILTER_SUB | PNG_FILTER_PAETH);
    png_write_info(png, info);
    png_set_bgr(png);
    for (int y = 0; y < height; ++y) {
        png_write_row(png, pixels + static_cast<std::size_t>(y) * rowBytes);
    }
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    return true;
}

} // namespace

// ---- loader (:195-268)

BitmapTileFreeImageLoader::BitmapTileFreeImageLoader(
    std::shared_ptr<const IBitmap32StaticFactory> staticFactory)
    : staticFactory_(std::move(staticFactory)) {
    // unreachable in legacy (u_BitmapTileFreeImage.pas:200 asserts, the registry passes the factory it
    // owns); no register row
    if (!staticFactory_) {
        throw std::invalid_argument("BitmapTileFreeImageLoader: null factory");
    }
}

Bitmap32StaticPtr BitmapTileFreeImageLoader::load(const QByteArray& data) const {
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);   // no format argument: the signature decides (FreeBitmap.pas:954-975)
    reader.setAutoTransform(false); // FreeImage does not rotate by EXIF
    const QImage image = reader.read();
    if (image.isNull()) {
        throw BitmapCodecError("FreeBitmap.LoadFromMemory FAIL!"); // :255-257
    }
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    if (argb.isNull()) {
        throw BitmapCodecError("FreeBitmap.ConvertTo32Bits FAIL!"); // :249-252
    }
    Bitmap32BufferPtr out = staticFactory_->bufferFactory()->buildEmpty(QPoint(argb.width(), argb.height()));
    if (!out) {
        return nullptr; // :230-234
    }
    const std::size_t rowBytes = static_cast<std::size_t>(argb.width()) * 4;
    core::Color32* dst = out->data();
    for (int i = 0; i < argb.height(); ++i) { // QImage is top-down: no flip (legacy :240-246 flips)
        std::memcpy(dst + static_cast<std::size_t>(i) * static_cast<std::size_t>(argb.width()),
                    argb.constScanLine(i), rowBytes);
    }
    return staticFactory_->buildWithOwnBuffer(std::move(out));
}

// ---- BMP saver (:499-511)

BitmapTileFreeImageSaverBmp::BitmapTileFreeImageSaverBmp() : BitmapTileFreeImageSaver(0, 32) {}

QByteArray BitmapTileFreeImageSaverBmp::save(const Bitmap32StaticPtr& bitmap) const {
    // D row of T-0161: legacy dereferences nil (ABitmap.Size, :391), no handler; the converter
    // u_ContentConverterBitmap.pas:77 passes the loader's nil for an oversize image.
    if (!bitmap) {
        throw BitmapCodecError("FreeBitmap.SaveToStream FAIL!");
    }
    const QPoint size = bitmap->size();
    // FreeImage PluginBMP.cpp Save (:1406-1470) for a 32 bpp dib made by FreeImage_AllocateT
    // (BitmapAccess.cpp:432-443, :395): unverified against the shipped DLL. Header fields not set there
    // stay 0 (biSizeImage, biClrUsed, biClrImportant); the resolution is 2835 (72 dpi); no masks, no palette.
    const std::size_t pixelBytes = pixelCount(size) * 4;
    constexpr qsizetype kHeaders = 14 + 40;
    QByteArray result(kHeaders, '\0');
    auto put = [&result](qsizetype at, std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            result[at + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
        }
    };
    put(0, 0x4D42, 2);                                            // bfType 'BM'
    put(2, static_cast<std::uint32_t>(kHeaders + pixelBytes), 4); // bfSize
    put(10, kHeaders, 4);                                         // bfOffBits
    put(14, 40, 4);                                               // biSize
    put(18, static_cast<std::uint32_t>(size.x()), 4);             // biWidth
    put(22, static_cast<std::uint32_t>(size.y()), 4);             // biHeight, bottom-up
    put(26, 1, 2);                                                // biPlanes
    put(28, 32, 2);                                               // biBitCount
    put(38, 2835, 4);                                             // biXPelsPerMeter
    put(42, 2835, 4);                                             // biYPelsPerMeter
    result.reserve(kHeaders + static_cast<qsizetype>(pixelBytes));
    const std::size_t rowBytes = static_cast<std::size_t>(size.x()) * 4;
    const auto* bgra = reinterpret_cast<const char*>(bitmap->data());
    // :456-467 stores row I at scanline H-1-I; the file is bottom-up
    for (int y = size.y() - 1; y >= 0; --y) {
        result.append(bgra + static_cast<std::size_t>(y) * rowBytes, static_cast<qsizetype>(rowBytes));
    }
    return result;
}

// ---- PNG saver (:532-556, Save :373-495 for the 32 and 24 bit branches)

BitmapTileFreeImageSaverPng::BitmapTileFreeImageSaverPng(quint8 compress, int bitsPerPixel)
    : BitmapTileFreeImageSaver(compress == 0 ? kPngZNoCompression : compress,
                               static_cast<quint8>(bitsPerPixel)) {
    if (compress > 9) {
        throw BitmapCodecError("Invalid PNG compression level: " + std::to_string(compress)); // :542-547
    }
    // unreachable in legacy (u_BitmapTileSaveLoadFactory.pas:218-241 passes 8/24/32 only); no register row
    if (bitsPerPixel != 24 && bitsPerPixel != 32) {
        throw BitmapCodecError("PNG 8 bpp saver is not ported");
    }
}

QByteArray BitmapTileFreeImageSaverPng::save(const Bitmap32StaticPtr& bitmap) const {
    // D row of T-0161: see BitmapTileFreeImageSaverBmp::save
    if (!bitmap) {
        throw BitmapCodecError("FreeBitmap.SaveToStream FAIL!");
    }
    const QPoint size = bitmap->size();
    const std::size_t count = pixelCount(size);
    const auto* bgra = reinterpret_cast<const unsigned char*>(bitmap->data());
    const int level = flag() == kPngZNoCompression ? 0 : flag();

    // 24 bit: ConvertTo24Bits drops alpha (:468-474); 32 bit: RGB rows when every alpha is $FF (spec §2.5)
    bool withAlpha = false;
    if (bitsPerPixel() == 32) {
        for (std::size_t i = 0; i < count && !withAlpha; ++i) {
            withAlpha = bgra[i * 4 + 3] != 0xFF;
        }
    }
    std::vector<unsigned char> rgb;
    const unsigned char* pixels = bgra;
    std::size_t rowBytes = static_cast<std::size_t>(size.x()) * 4;
    if (!withAlpha) {
        rgb.resize(count * 3);
        for (std::size_t i = 0; i < count; ++i) {
            std::memcpy(&rgb[i * 3], bgra + i * 4, 3);
        }
        pixels = rgb.data();
        rowBytes = static_cast<std::size_t>(size.x()) * 3;
    }
    std::vector<unsigned char> out;
    if (!encodePng(pixels, rowBytes, size.x(), size.y(),
                   withAlpha ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_RGB, level, out)) {
        throw BitmapCodecError("FreeBitmap.SaveToStream FAIL!"); // :478-482
    }
    return QByteArray(reinterpret_cast<const char*>(out.data()), static_cast<qsizetype>(out.size()));
}

} // namespace cg::bitmap
