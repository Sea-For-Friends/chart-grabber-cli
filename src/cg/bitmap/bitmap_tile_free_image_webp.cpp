// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/BitmapTileSaveLoad/u_BitmapTileFreeImage.pas:129-136, :149-174 (classes), :515-528, :560-611
// (constructors) and the 32-bit branch of Save :456-467, :476-491 for WebP. The encoder is libwebp with the
// parameters of FreeImage's PluginWebP.cpp as listed in docs/specs/bitmap.md §2.5 (from a GitHub copy and
// from memory, unverified against the shipped DLL). The GIF and TIFF savers refuse.
// Feature: FEAT-RND-044 (WebP), FEAT-RND-039 (GIF), FEAT-RND-043 (TIFF)

#include "cg/bitmap/bitmap_codec_error.h"
#include "cg/bitmap/bitmap_tile_free_image.h"

#include <QPoint>

#include <string>
#include <webp/encode.h>
#include <webp/mux.h>

namespace cg::bitmap {

namespace {

constexpr int kWebpLossless = 0x100; // FreeImage.pas:634 WEBP_LOSSLESS

// Owners of the libwebp C handles: a BitmapCodecError never leaks them.
struct PictureGuard {
    WebPPicture* picture;
    explicit PictureGuard(WebPPicture* handle) : picture(handle) {}
    ~PictureGuard() { WebPPictureFree(picture); }
    PictureGuard(const PictureGuard&) = delete;
    PictureGuard& operator=(const PictureGuard&) = delete;
};
struct WriterGuard {
    WebPMemoryWriter* writer;
    explicit WriterGuard(WebPMemoryWriter* handle) : writer(handle) {}
    ~WriterGuard() { WebPMemoryWriterClear(writer); }
    WriterGuard(const WriterGuard&) = delete;
    WriterGuard& operator=(const WriterGuard&) = delete;
};
struct MuxGuard {
    WebPMux* mux;
    explicit MuxGuard(WebPMux* handle) : mux(handle) {}
    ~MuxGuard() { WebPMuxDelete(mux); }
    MuxGuard(const MuxGuard&) = delete;
    MuxGuard& operator=(const MuxGuard&) = delete;
};
struct DataGuard {
    WebPData* data;
    explicit DataGuard(WebPData* handle) : data(handle) {}
    ~DataGuard() { WebPDataClear(data); }
    DataGuard(const DataGuard&) = delete;
    DataGuard& operator=(const DataGuard&) = delete;
};

[[noreturn]] void saveFailed() {
    throw BitmapCodecError("FreeBitmap.SaveToStream FAIL!"); // :478-482
}

// PluginWebP.cpp:475-515 (config, import, encode) and :595-648 (container); flag = quality or kWebpLossless.
QByteArray encodeWebp(const Bitmap32StaticPtr& bitmap, int flag) {
    // D row of T-0162: legacy dereferences nil (ABitmap.Size, :391), no handler; see the PNG saver.
    if (!bitmap) {
        saveFailed();
    }
    const QPoint size = bitmap->size();

    WebPConfig config;
    if (!WebPConfigInit(&config)) {
        saveFailed();
    }
    config.method = 6;
    const bool lossless = (flag & kWebpLossless) != 0;
    if (lossless) {
        config.lossless = 1;
    } else {
        config.lossless = 0;
        config.quality = static_cast<float>(flag & 0x7F);
    }
    if (!WebPValidateConfig(&config)) {
        saveFailed();
    }

    WebPPicture picture;
    if (!WebPPictureInit(&picture)) {
        saveFailed();
    }
    const PictureGuard pictureGuard(&picture);
    if (lossless) {
        picture.use_argb = 1;
    }
    picture.width = size.x();
    picture.height = size.y();

    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    const WriterGuard writerGuard(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;

    // Color32 memory order is B, G, R, A
    if (!WebPPictureImportBGRA(&picture, reinterpret_cast<const uint8_t*>(bitmap->data()), size.x() * 4)) {
        saveFailed();
    }
    if (!WebPEncode(&config, &picture)) {
        saveFailed();
    }

    WebPMux* mux = WebPMuxNew();
    if (mux == nullptr) {
        saveFailed();
    }
    const MuxGuard muxGuard(mux);
    WebPData image{writer.mem, writer.size};
    if (WebPMuxSetImage(mux, &image, 1) != WEBP_MUX_OK) {
        saveFailed();
    }
    WebPData assembled;
    WebPDataInit(&assembled);
    const DataGuard dataGuard(&assembled);
    if (WebPMuxAssemble(mux, &assembled) != WEBP_MUX_OK) {
        saveFailed();
    }
    return QByteArray(reinterpret_cast<const char*>(assembled.bytes), static_cast<qsizetype>(assembled.size));
}

} // namespace

// ---- :560-578
BitmapTileFreeImageSaverWebp::BitmapTileFreeImageSaverWebp(quint8 quality)
    : BitmapTileFreeImageSaver(quality, 32) {
    if (quality < 1 || quality > 100) {
        throw BitmapCodecError("Invalid WebP quality value: " + std::to_string(static_cast<int>(quality)));
    }
}

QByteArray BitmapTileFreeImageSaverWebp::save(const Bitmap32StaticPtr& bitmap) const {
    return encodeWebp(bitmap, flag());
}

// ---- :581-594
BitmapTileFreeImageSaverWebpLossless::BitmapTileFreeImageSaverWebpLossless()
    : BitmapTileFreeImageSaver(kWebpLossless, 32) {}

QByteArray BitmapTileFreeImageSaverWebpLossless::save(const Bitmap32StaticPtr& bitmap) const {
    return encodeWebp(bitmap, flag());
}

// ---- :515-528 (flag 0, 8 bpp; the 32-to-8 converter is not ported)
BitmapTileFreeImageSaverGif::BitmapTileFreeImageSaverGif() : BitmapTileFreeImageSaver(0, 8) {}

// not ported: GIF / TIFF savers (docs/specs/bitmap.md §2.5; palette output is group O-3)
QByteArray BitmapTileFreeImageSaverGif::save(const Bitmap32StaticPtr&) const {
    throw BitmapCodecError("GIF saver is not ported");
}

// ---- :597-611
BitmapTileFreeImageSaverTiff::BitmapTileFreeImageSaverTiff(quint32 compressionType, int bitsPerPixel)
    : BitmapTileFreeImageSaver(static_cast<int>(compressionType), static_cast<quint8>(bitsPerPixel)) {}

QByteArray BitmapTileFreeImageSaverTiff::save(const Bitmap32StaticPtr&) const {
    throw BitmapCodecError("TIFF saver is not ported");
}

} // namespace cg::bitmap
