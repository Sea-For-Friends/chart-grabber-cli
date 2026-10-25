// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// FEAT-NET-013 -- docs/specs/net.md §2.12, §7.5.
// Fixture bodies are generated in-test with the same system libraries used by the production
// code (zlib, brotli, zstd) -- no binary fixture files, matching the ticket's acceptance note.
#include "cg/core/ini_file.h"
#include "cg/net/content_decoder.h"
#include "cg/net/http_headers.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QString>
#include <QStringList>

#include <brotli/encode.h>
#include <gtest/gtest.h>
#include <zstd.h>

#include <array>
#include <zlib.h>

#include "fixture_path.h"

using cg::net::acceptEncodingList;
using cg::net::decode;
using cg::net::Encoding;
using cg::net::encodingFromHeader;
using cg::net::filterAcceptEncoding;

namespace {

QByteArray compressZlib(const QByteArray& input, int windowBits) {
    z_stream strm{};
    EXPECT_EQ(deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY),
              Z_OK);
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    strm.avail_in = static_cast<uInt>(input.size());
    QByteArray out(static_cast<qsizetype>(deflateBound(&strm, static_cast<uLong>(input.size()))),
                   Qt::Uninitialized);
    strm.next_out = reinterpret_cast<Bytef*>(out.data());
    strm.avail_out = static_cast<uInt>(out.size());
    const int ret = deflate(&strm, Z_FINISH);
    EXPECT_EQ(ret, Z_STREAM_END);
    out.resize(out.size() - static_cast<qsizetype>(strm.avail_out));
    deflateEnd(&strm);
    return out;
}

QByteArray compressBrotli(const QByteArray& input) {
    size_t encodedSize = BrotliEncoderMaxCompressedSize(static_cast<size_t>(input.size()));
    QByteArray out(static_cast<qsizetype>(encodedSize), Qt::Uninitialized);
    const auto* inBytes = reinterpret_cast<const uint8_t*>(input.constData());
    auto* outBytes = reinterpret_cast<uint8_t*>(out.data());
    EXPECT_TRUE(BrotliEncoderCompress(BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_GENERIC,
                                      static_cast<size_t>(input.size()), inBytes, &encodedSize, outBytes));
    out.resize(static_cast<qsizetype>(encodedSize));
    return out;
}

QByteArray compressZstd(const QByteArray& input) {
    const size_t bound = ZSTD_compressBound(static_cast<size_t>(input.size()));
    QByteArray out(static_cast<qsizetype>(bound), Qt::Uninitialized);
    const size_t written =
        ZSTD_compress(out.data(), bound, input.constData(), static_cast<size_t>(input.size()), 3);
    EXPECT_FALSE(ZSTD_isError(written));
    out.resize(static_cast<qsizetype>(written));
    return out;
}

} // namespace

TEST(ContentDecoderTest, EncodingFromHeaderExactMatches) {
    EXPECT_EQ(encodingFromHeader("gzip"), Encoding::GZip);
    EXPECT_EQ(encodingFromHeader("deflate"), Encoding::Deflate);
    EXPECT_EQ(encodingFromHeader("br"), Encoding::Brotli);
    EXPECT_EQ(encodingFromHeader("zstd"), Encoding::Zstd);
    EXPECT_EQ(encodingFromHeader("identity"), Encoding::Identity);
}

TEST(ContentDecoderTest, EncodingFromHeaderIsCaseSensitive) {
    EXPECT_EQ(encodingFromHeader("GZIP"), Encoding::Unknown);
}

TEST(ContentDecoderTest, EncodingFromHeaderMultiValueIsUnknown) {
    EXPECT_EQ(encodingFromHeader("gzip, br"), Encoding::Unknown);
}

TEST(ContentDecoderTest, AcceptEncodingListWithAllLibraries) {
    EXPECT_EQ(acceptEncodingList(), QByteArray("gzip, deflate, br, zstd"));
}

TEST(ContentDecoderTest, FilterKeepsSupportedOnly) {
    EXPECT_EQ(filterAcceptEncoding("gzip, unsupported-codec, br"), QByteArray("gzip, br"));
}

TEST(ContentDecoderTest, FilterLowercasesWholeList) {
    EXPECT_EQ(filterAcceptEncoding("GZIP, DEFLATE"), QByteArray("gzip, deflate"));
}

TEST(ContentDecoderTest, FilterDropsQValueTokens) {
    EXPECT_EQ(filterAcceptEncoding("gzip;q=0.5, deflate"), QByteArray("deflate"));
}

TEST(ContentDecoderTest, FilterMayReturnEmpty) {
    EXPECT_EQ(filterAcceptEncoding("unsupported1, unsupported2"), QByteArray());
}

TEST(ContentDecoderTest, DecodeEmptyBodyIsNoOp) {
    const auto result = decode("gzip", QByteArray());
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->isEmpty());
}

TEST(ContentDecoderTest, DecodeEmptyEncodingIsNoOp) {
    const QByteArray body = "raw bytes, untouched";
    const auto result = decode("", body);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, body);
}

TEST(ContentDecoderTest, DecodeGzipRoundTrip) {
    // Brotli and zstd round-trips ride along here (docs/specs/net.md §7.5).
    const QByteArray plain = "the quick brown fox jumps over the lazy dog, repeated for bulk. "
                             "the quick brown fox jumps over the lazy dog, repeated for bulk.";
    struct Case {
        const char* encoding;
        QByteArray compressed;
    };
    const std::array<Case, 3> cases{{
        {"gzip", compressZlib(plain, 15 + 16)},
        {"br", compressBrotli(plain)},
        {"zstd", compressZstd(plain)},
    }};
    for (const auto& c : cases) {
        SCOPED_TRACE(c.encoding);
        const auto result = decode(c.encoding, c.compressed);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, plain);
    }
}

TEST(ContentDecoderTest, DecodeZlibDeflate) {
    const QByteArray plain = "zlib-wrapped deflate payload";
    const QByteArray compressed = compressZlib(plain, 15);
    const auto result = decode("deflate", compressed);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, plain);
}

TEST(ContentDecoderTest, DecodeRawDeflateFallback) {
    // Some tile servers send raw (headerless) deflate; the zlib-wrapped attempt fails first, then
    // the raw fallback (windowBits = -15) succeeds.
    const QByteArray plain = "raw deflate payload, no zlib header or trailer";
    const QByteArray compressed = compressZlib(plain, -15);
    const auto result = decode("deflate", compressed);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, plain);
}

TEST(ContentDecoderTest, DecodeUnknownFails) {
    const auto result = decode("bzip2", QByteArray("payload"));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), cg::core::ErrorCode::Unsupported);
    EXPECT_TRUE(result.error().what().contains(QStringLiteral("Unknown Content-Encoding")));
}

// Acceptance corpus check (docs/backlog/T-0012A, fixtures from T-0004): every stock ZMP whose
// RequestHead carries "Accept-Encoding: gzip, deflate" must round-trip unchanged through
// filterAcceptEncoding. RequestHead stores a literal "\r\n" (backslash r backslash n) escape for
// real CRLF; no ADR/spec names the unescape step for this port yet (see the ticket's Open
// Questions), so this test performs it locally to reconstruct the real header blob.
TEST(ContentDecoderTest, FilterAcceptEncodingCorpusRoundTrip) {
    CG_SKIP_IF_NO_CORPUS();
    QDirIterator it(QDir(cgFixturesDir()).filePath(QStringLiteral("zmp")),
                    QStringList{QStringLiteral("params.txt")}, QDir::Files, QDirIterator::Subdirectories);
    int checked = 0;
    while (it.hasNext()) {
        const QString path = it.next();
        const auto ini = cg::core::IniFile::fromFile(path);
        ASSERT_TRUE(ini.has_value()) << path.toStdString();
        const QString raw = ini->readString(u"PARAMS", u"RequestHead");
        if (raw.isEmpty()) {
            continue;
        }
        QByteArray headers = raw.toUtf8();
        headers.replace("\\r\\n", "\r\n");
        const QByteArray old = cg::net::getHeaderValueUp(headers, "ACCEPT-ENCODING");
        if (old != QByteArray("gzip, deflate")) {
            continue;
        }
        ++checked;
        EXPECT_EQ(filterAcceptEncoding(old), old) << path.toStdString();
    }
    // The ticket text says "29 stock ZMPs"; the committed T-0004 fixture corpus (514 stock ZMPs)
    // actually has 105 matching this exact value today -- flagged as an open question rather than
    // silently changed, per docs/conventions/agent-protocol.md.
    EXPECT_EQ(checked, 105);
}
