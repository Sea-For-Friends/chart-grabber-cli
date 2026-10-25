// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Network/u_ContentDecoder.pas:56-280.
#include "cg/net/content_decoder.h"

#include <QList>

#include <brotli/decode.h>
#include <zstd.h>

#include <utility>
#include <zlib.h>

namespace cg::net {

namespace {

// A tile is never anywhere near this size; legacy had no cap at all, which is a
// decompression-bomb exposure this port closes [D-12].
constexpr qsizetype kMaxDecodedSize = qsizetype{64} * 1024 * 1024;
constexpr qsizetype kChunkSize = qsizetype{64} * 1024;

struct DecodeAttempt {
    bool ok = false;
    QByteArray data;
    QString error;
};

DecodeAttempt zlibInflate(const QByteArray& input, int windowBits) {
    DecodeAttempt attempt;
    z_stream strm{};
    if (inflateInit2(&strm, windowBits) != Z_OK) {
        attempt.error = QStringLiteral("inflateInit2 failed");
        return attempt;
    }
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    strm.avail_in = static_cast<uInt>(input.size());

    QByteArray out;
    int ret = Z_OK;
    while (true) {
        if (out.size() + kChunkSize > kMaxDecodedSize) {
            attempt.error = QStringLiteral("Decompressed content too large");
            inflateEnd(&strm);
            return attempt;
        }
        const qsizetype prevSize = out.size();
        out.resize(prevSize + kChunkSize);
        strm.next_out = reinterpret_cast<Bytef*>(out.data() + prevSize);
        strm.avail_out = static_cast<uInt>(kChunkSize);
        ret = inflate(&strm, Z_NO_FLUSH);
        out.resize(prevSize + kChunkSize - static_cast<qsizetype>(strm.avail_out));
        if (ret == Z_STREAM_END) {
            break;
        }
        if (ret != Z_OK) {
            attempt.error =
                strm.msg ? QString::fromLatin1(strm.msg) : QStringLiteral("zlib error %1").arg(ret);
            inflateEnd(&strm);
            return attempt;
        }
        if (strm.avail_in == 0 && strm.avail_out == kChunkSize) {
            // No input left and no progress made: a truncated stream.
            attempt.error = QStringLiteral("unexpected end of stream");
            inflateEnd(&strm);
            return attempt;
        }
    }
    inflateEnd(&strm);
    attempt.ok = true;
    attempt.data = std::move(out);
    return attempt;
}

DecodeAttempt brotliDecompress(const QByteArray& input) {
    DecodeAttempt attempt;
    BrotliDecoderState* state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (state == nullptr) {
        attempt.error = QStringLiteral("BrotliDecoderCreateInstance failed");
        return attempt;
    }
    const auto* nextIn = reinterpret_cast<const uint8_t*>(input.constData());
    size_t availIn = static_cast<size_t>(input.size());
    QByteArray out;
    BrotliDecoderResult result = BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT;
    while (result != BROTLI_DECODER_RESULT_SUCCESS) {
        if (out.size() + kChunkSize > kMaxDecodedSize) {
            attempt.error = QStringLiteral("Decompressed content too large");
            BrotliDecoderDestroyInstance(state);
            return attempt;
        }
        const qsizetype prevSize = out.size();
        out.resize(prevSize + kChunkSize);
        auto* nextOut = reinterpret_cast<uint8_t*>(out.data() + prevSize);
        size_t availOut = static_cast<size_t>(kChunkSize);
        result = BrotliDecoderDecompressStream(state, &availIn, &nextIn, &availOut, &nextOut, nullptr);
        out.resize(prevSize + kChunkSize - static_cast<qsizetype>(availOut));
        if (result == BROTLI_DECODER_RESULT_ERROR) {
            attempt.error = QString::fromLatin1(BrotliDecoderErrorString(BrotliDecoderGetErrorCode(state)));
            BrotliDecoderDestroyInstance(state);
            return attempt;
        }
        if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT && availIn == 0) {
            attempt.error = QStringLiteral("truncated Brotli stream");
            BrotliDecoderDestroyInstance(state);
            return attempt;
        }
    }
    BrotliDecoderDestroyInstance(state);
    attempt.ok = true;
    attempt.data = std::move(out);
    return attempt;
}

DecodeAttempt zstdDecompress(const QByteArray& input) {
    DecodeAttempt attempt;
    ZSTD_DStream* stream = ZSTD_createDStream();
    if (stream == nullptr) {
        attempt.error = QStringLiteral("ZSTD_createDStream failed");
        return attempt;
    }
    ZSTD_initDStream(stream);
    ZSTD_inBuffer inBuf{input.constData(), static_cast<size_t>(input.size()), 0};
    QByteArray out;
    size_t lastRet = 0;
    while (inBuf.pos < inBuf.size) {
        if (out.size() + kChunkSize > kMaxDecodedSize) {
            attempt.error = QStringLiteral("Decompressed content too large");
            ZSTD_freeDStream(stream);
            return attempt;
        }
        const qsizetype prevSize = out.size();
        out.resize(prevSize + kChunkSize);
        ZSTD_outBuffer outBuf{out.data() + prevSize, static_cast<size_t>(kChunkSize), 0};
        lastRet = ZSTD_decompressStream(stream, &outBuf, &inBuf);
        out.resize(prevSize + static_cast<qsizetype>(outBuf.pos));
        if (ZSTD_isError(lastRet) != 0U) {
            attempt.error = QString::fromLatin1(ZSTD_getErrorName(lastRet));
            ZSTD_freeDStream(stream);
            return attempt;
        }
    }
    ZSTD_freeDStream(stream);
    if (lastRet != 0) {
        attempt.error = QStringLiteral("truncated zstd stream");
        return attempt;
    }
    attempt.ok = true;
    attempt.data = std::move(out);
    return attempt;
}

} // namespace

Encoding encodingFromHeader(QByteArrayView value) {
    if (value == "gzip") {
        return Encoding::GZip;
    }
    if (value == "deflate") {
        return Encoding::Deflate;
    }
    if (value == "br") {
        return Encoding::Brotli;
    }
    if (value == "zstd") {
        return Encoding::Zstd;
    }
    if (value == "identity") {
        return Encoding::Identity;
    }
    return Encoding::Unknown;
}

QByteArray acceptEncodingList() {
    return QByteArrayLiteral("gzip, deflate, br, zstd");
}

QByteArray filterAcceptEncoding(QByteArrayView list) {
    if (list.isEmpty()) {
        return {};
    }
    const QByteArray lower = list.toByteArray().toLower();
    const QList<QByteArray> tokens = lower.split(',');
    QByteArray result;
    for (const QByteArray& token : tokens) {
        const QByteArray trimmed = token.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        switch (encodingFromHeader(trimmed)) {
        case Encoding::GZip:
        case Encoding::Deflate:
        case Encoding::Identity:
        case Encoding::Brotli:
        case Encoding::Zstd:
            break;
        case Encoding::Unknown:
            continue;
        }
        if (!result.isEmpty()) {
            result += ", ";
        }
        result += trimmed;
    }
    return result;
}

cg::core::Result<QByteArray> decode(QByteArrayView encoding, QByteArray body) {
    if (body.isEmpty() || encoding.isEmpty()) {
        return body;
    }
    switch (encodingFromHeader(encoding)) {
    case Encoding::Identity:
        return body;
    case Encoding::GZip: {
        const DecodeAttempt attempt = zlibInflate(body, 15 + 16); // gzip auto-detect
        if (!attempt.ok) {
            return cg::core::fail(cg::core::ErrorCode::ParseError,
                                  QStringLiteral("Gzip decompression failed!"));
        }
        return attempt.data;
    }
    case Encoding::Deflate: {
        const DecodeAttempt zlibWrapped = zlibInflate(body, 15);
        if (zlibWrapped.ok) {
            return zlibWrapped.data;
        }
        const DecodeAttempt raw = zlibInflate(body, -15);
        if (raw.ok) {
            return raw.data;
        }
        return cg::core::fail(cg::core::ErrorCode::ParseError,
                              QStringLiteral("Deflate decompression failed (zlib and raw formats): %1 | %2")
                                  .arg(zlibWrapped.error, raw.error));
    }
    case Encoding::Brotli: {
        const DecodeAttempt attempt = brotliDecompress(body);
        if (!attempt.ok) {
            return cg::core::fail(cg::core::ErrorCode::ParseError, attempt.error);
        }
        return attempt.data;
    }
    case Encoding::Zstd: {
        const DecodeAttempt attempt = zstdDecompress(body);
        if (!attempt.ok) {
            return cg::core::fail(cg::core::ErrorCode::ParseError, attempt.error);
        }
        return attempt.data;
    }
    case Encoding::Unknown:
        break;
    }
    return cg::core::fail(
        cg::core::ErrorCode::Unsupported,
        QStringLiteral("Unknown Content-Encoding: \"%1\"").arg(QString::fromUtf8(encoding.toByteArray())));
}

} // namespace cg::net
