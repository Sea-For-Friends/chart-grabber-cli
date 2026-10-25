// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Network/u_ContentDecoder.pas:56-280.
// FEAT-NET-013 (BadContentEncoding is one branch of the download-result classification pipeline
// this decoder feeds).
//
// zlib, libbrotlidec and libzstd are linked at build time, not dlopen'ed [D-7]: whether a decoder
// is "available" is therefore a build-time, not run-time, question, and acceptEncodingList() is a
// compile-time constant.

#include "cg/core/result.h"

#include <QByteArray>
#include <QByteArrayView>

#include <cstdint>

namespace cg::net {

enum class Encoding : std::uint8_t { Identity, GZip, Deflate, Brotli, Zstd, Unknown };

// Exact, CASE-SENSITIVE, untrimmed match against "gzip"|"deflate"|"br"|"zstd"|"identity". Anything
// else -- including "GZIP" and "gzip, br" -- is Unknown, and Unknown makes decode() fail.
[[nodiscard]] Encoding encodingFromHeader(QByteArrayView value);

// What we advertise. Ports GetDecodersStr. With all libraries linked this is the constant
// "gzip, deflate, br, zstd" (separator ", ").
[[nodiscard]] QByteArray acceptEncodingList();

// Ports RemoveUnsupportedDecoders: lower-case the whole list, split on ',' only (no ";q=" parsing
// -- "gzip;q=0.5" becomes an unknown token and is dropped), trim each token, drop unsupported
// ones, re-join with ", ". May return "".
[[nodiscard]] QByteArray filterAcceptEncoding(QByteArrayView list);

// Ports Decode. `body.isEmpty()` or `encoding.isEmpty()` returns `body` unchanged, silently.
// Identity returns `body` unchanged. Unknown fails with
// `Unknown Content-Encoding: "<value>"`. Gzip/Deflate/Brotli/Zstd decode or fail with the literal
// messages documented in docs/specs/net.md §2.12 (Deflate tries zlib-wrapped inflate, then falls
// back to raw deflate on failure, reporting both error messages). Output is capped at 64 MiB;
// beyond that decode() fails with "Decompressed content too large" [D-12] -- legacy had no cap,
// which is a decompression-bomb exposure.
[[nodiscard]] cg::core::Result<QByteArray> decode(QByteArrayView encoding, QByteArray body);

} // namespace cg::net
