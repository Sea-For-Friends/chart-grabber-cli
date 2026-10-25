// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/BitmapTileSaveLoad/u_BitmapTileLibJpeg.pas:106-292 and the parts of Includes/LibJpeg/
// LibJpegRead.pas (:134-175, :203-290), LibJpegWrite.pas (:172-207, :225-334), LibJpegErrorHandler.pas
// (:17-40) and LibJpegInOutDataManager.pas (:80-113) they drive.
// Feature: FEAT-RND-042
//
// libjpeg reports every error through error_exit, which must not return. Legacy raises a Delphi
// exception through the library frames; the port does a longjmp to the call that owns the libjpeg
// object. The functions that contain setjmp own only trivial locals; the C++ exception is thrown by
// the callers after the longjmp has landed, never through libjpeg frames.

#include "cg/bitmap/bitmap_tile_lib_jpeg.h"

#include "cg/bitmap/bitmap_codec_error.h"

#include <QPoint>

#include <csetjmp>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

// jpeglib.h needs <cstdio> first and is not C++-aware on every distribution.
extern "C" {
#include <jpeglib.h>
}

namespace cg::bitmap {

namespace {

// jpeg_error_mgr + the jump target + the formatted text (LibJpegErrorHandler.pas:17-40).
struct JpegErrorMgr {
    jpeg_error_mgr pub{}; // must stay first: the callbacks cast from it
    std::jmp_buf jump{};
    char text[JMSG_LENGTH_MAX + 32]{};
};

void formatAndJump(j_common_ptr cinfo, const char* prefix) {
    auto* err = reinterpret_cast<JpegErrorMgr*>(cinfo->err);
    char message[JMSG_LENGTH_MAX] = {};
    (*cinfo->err->format_message)(cinfo, message);
    std::snprintf(err->text, sizeof(err->text), "%s [%d] %s", prefix, cinfo->err->msg_code, message);
    std::longjmp(err->jump, 1);
}

// :17-28
void errorExit(j_common_ptr cinfo) {
    formatAndJump(cinfo, "ERROR");
}

// :30-40: a libjpeg warning (only the first of a run reaches here) fails the call, as in legacy.
void outputMessage(j_common_ptr cinfo) {
    formatAndJump(cinfo, "OUTPUT");
}

void initErr(JpegErrorMgr& err) {
    jpeg_std_error(&err.pub);
    err.pub.error_exit = errorExit;
    err.pub.output_message = outputMessage;
}

// Source manager over the whole input (LibJpegInOutDataManager.pas:72-113): when the data is
// exhausted libjpeg is handed a fake EOI (FF D9) without a warning.
constexpr JOCTET kFakeEoi[2] = {0xFF, 0xD9};

struct JpegSource {
    jpeg_source_mgr pub{}; // must stay first
    const JOCTET* data = nullptr;
    std::size_t size = 0;
    bool delivered = false;
};

void initSource(j_decompress_ptr cinfo) { // libjpeg_init_source
    cinfo->src->bytes_in_buffer = 0;
    cinfo->src->next_input_byte = nullptr;
}

boolean fillInputBuffer(j_decompress_ptr cinfo) { // libjpeg_fill_input_buffer
    auto* src = reinterpret_cast<JpegSource*>(cinfo->src);
    if (!src->delivered && src->size > 0) {
        src->delivered = true;
        src->pub.next_input_byte = src->data;
        src->pub.bytes_in_buffer = src->size;
    } else {
        src->pub.next_input_byte = kFakeEoi;
        src->pub.bytes_in_buffer = 2;
    }
    return TRUE;
}

void skipInputData(j_decompress_ptr cinfo, long numBytes) { // libjpeg_skip_input_data
    if (numBytes <= 0) {
        return;
    }
    jpeg_source_mgr* src = cinfo->src;
    while (static_cast<std::size_t>(numBytes) > src->bytes_in_buffer) {
        numBytes -= static_cast<long>(src->bytes_in_buffer);
        (*src->fill_input_buffer)(cinfo);
    }
    src->next_input_byte += numBytes;
    src->bytes_in_buffer -= static_cast<std::size_t>(numBytes);
}

void termSource(j_decompress_ptr) {} // libjpeg_term_source

// The decompress object with everything that must outlive a longjmp. The destructor destroys the
// libjpeg object (jpeg_abort of the legacy error handler is covered by jpeg_destroy).
struct Decompressor {
    JpegErrorMgr err;
    JpegSource src;
    jpeg_decompress_struct cinfo{};

    Decompressor() = delete;
    explicit Decompressor(const QByteArray& input) {
        initErr(err);
        cinfo.err = &err.pub;
        if (!create()) {
            throw BitmapCodecError(err.text);
        }
        src.pub.init_source = initSource;
        src.pub.fill_input_buffer = fillInputBuffer;
        src.pub.skip_input_data = skipInputData;
        // Legacy leaves this callback nil (LibJpegRead.pas:212, 224-229): the L row / the D row of T-0176.
        src.pub.resync_to_restart = jpeg_resync_to_restart;
        src.pub.term_source = termSource;
        src.data = reinterpret_cast<const JOCTET*>(input.constData());
        src.size = static_cast<std::size_t>(input.size());
        cinfo.src = &src.pub;
    }
    Decompressor(const Decompressor&) = delete;
    Decompressor& operator=(const Decompressor&) = delete;
    ~Decompressor() { jpeg_destroy_decompress(&cinfo); }

private:
    bool create() {
        if (setjmp(err.jump) != 0) {
            return false;
        }
        jpeg_create_decompress(&cinfo);
        return true;
    }
};

// Return value: 1 = header read, 0 = jpeg_read_header gave 0, -1 = libjpeg error (text in err.text).
int readHeader(Decompressor& d) { // LibJpegRead.pas:146
    if (setjmp(d.err.jump) != 0) {
        return -1;
    }
    return jpeg_read_header(&d.cinfo, TRUE) > 0 ? 1 : 0;
}

// LibJpegRead.pas:237-290: rows go straight into the buffer. false = libjpeg error.
bool decompress(Decompressor& d, core::Color32* pixels) {
    if (setjmp(d.err.jump) != 0) {
        return false;
    }
    d.cinfo.out_color_space = JCS_EXT_BGRA; // :252
    jpeg_start_decompress(&d.cinfo);
    const std::size_t width = d.cinfo.output_width;
    while (d.cinfo.output_scanline < d.cinfo.output_height) {
        JSAMPROW row = reinterpret_cast<JSAMPROW>(pixels + d.cinfo.output_scanline * width);
        jpeg_read_scanlines(&d.cinfo, &row, 1);
    }
    jpeg_finish_decompress(&d.cinfo); // the legacy `finally`, :279-283; a warning here fails the load
    return true;
}

// The compress object plus the growing output buffer (jpeg_mem_dest; the bytes do not depend on
// the buffer size, the legacy buffer is 4096 bytes).
struct Compressor {
    JpegErrorMgr err;
    jpeg_compress_struct cinfo{};
    unsigned char* out = nullptr;
    unsigned long outSize = 0;

    Compressor() {
        initErr(err);
        cinfo.err = &err.pub;
        if (!create()) {
            throw BitmapCodecError(err.text);
        }
    }
    Compressor(const Compressor&) = delete;
    Compressor& operator=(const Compressor&) = delete;
    ~Compressor() {
        jpeg_destroy_compress(&cinfo);
        std::free(out); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): allocated by
                        // jpeg_mem_dest
    }

private:
    bool create() {
        if (setjmp(err.jump) != 0) {
            return false;
        }
        jpeg_create_compress(&cinfo);
        return true;
    }
};

// LibJpegWrite.pas:172-207, 258-334. false = libjpeg error (text in err.text).
bool compress(Compressor& c, const core::Color32* pixels, int width, int height, int quality) {
    if (setjmp(c.err.jump) != 0) {
        return false;
    }
    jpeg_mem_dest(&c.cinfo, &c.out, &c.outSize);
    c.cinfo.image_width = static_cast<JDIMENSION>(width);
    c.cinfo.image_height = static_cast<JDIMENSION>(height);
    c.cinfo.input_components = 4; // :194-195
    c.cinfo.in_color_space = JCS_EXT_BGRA;
    jpeg_set_defaults(&c.cinfo);
    jpeg_set_quality(&c.cinfo, quality, TRUE); // :204
    jpeg_start_compress(&c.cinfo, TRUE);       // :277
    const std::size_t w = static_cast<std::size_t>(width);
    while (c.cinfo.next_scanline < c.cinfo.image_height) {
        // libjpeg only reads the row
        JSAMPROW row =
            reinterpret_cast<JSAMPROW>(const_cast<core::Color32*>(pixels + c.cinfo.next_scanline * w));
        jpeg_write_scanlines(&c.cinfo, &row, 1);
    }
    jpeg_finish_compress(&c.cinfo);
    return true;
}

} // namespace

LibJpegTileLoader::LibJpegTileLoader(std::shared_ptr<const IBitmap32StaticFactory> staticFactory)
    : staticFactory_(std::move(staticFactory)) {
    // unreachable in legacy (u_BitmapTileLibJpeg.pas:110 asserts, the registry passes the factory it
    // owns); no register row
    if (!staticFactory_) {
        throw std::invalid_argument("LibJpegTileLoader: null factory");
    }
}

Bitmap32StaticPtr LibJpegTileLoader::load(const QByteArray& data) const {
    Decompressor d(data);
    const int header = readHeader(d);
    if (header < 0) {
        throw BitmapCodecError(d.err.text);
    }
    if (header == 0) {
        throw BitmapCodecError("Jpeg open error!"); // :145
    }
    // :133-138; the check of the size precedes any allocation, nullptr = refused
    const QPoint size(static_cast<int>(d.cinfo.image_width), static_cast<int>(d.cinfo.image_height));
    Bitmap32BufferPtr buffer = staticFactory_->bufferFactory()->buildEmpty(size);
    if (!buffer) {
        return nullptr;
    }
    if (!decompress(d, buffer->data())) {
        throw BitmapCodecError(d.err.text);
    }
    return staticFactory_->buildWithOwnBuffer(std::move(buffer)); // :143
}

LibJpegTileSaver::LibJpegTileSaver(quint8 quality) : quality_(quality) {}

QByteArray LibJpegTileSaver::save(const Bitmap32StaticPtr& bitmap) const {
    // D row of T-0160: legacy dereferences nil (access violation, no handler); the converter
    // u_ContentConverterBitmap.pas:77 passes the loader's nil for an oversize image.
    if (!bitmap) {
        throw BitmapCodecError("Jpeg compression error!");
    }
    const QPoint size = bitmap->size();
    // unreachable in legacy (Bitmap32/u_Bitmap32BufferFactorySimple.pas:141-147); no register row
    if (size.x() <= 0 || size.y() <= 0 || size.x() >= 65536 || size.y() >= 65536) {
        throw BitmapCodecError("Set output resolution first!"); // LibJpegWrite.pas:182-189
    }
    Compressor c;
    if (!compress(c, bitmap->data(), size.x(), size.y(), quality_)) {
        throw BitmapCodecError(c.err.text);
    }
    return QByteArray(reinterpret_cast<const char*>(c.out), static_cast<qsizetype>(c.outSize));
}

} // namespace cg::bitmap
