// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/ContentType/u_ContentTypeFunc.pas (whole unit, :49-100)
//
// Content-type predicates by MIME type. Each compares the type with the legacy SameTextA, which the
// port reads as ASCII case folding without trimming: `image/jpeg ` with a trailing blank is not JPEG.
// The ContentTypeInfo overloads are the IContentTypeInfoBasic overloads of the unit (they forward the type).

#include "cg/core/content_type.h"

#include <QByteArrayView>

namespace cg::core {

[[nodiscard]] bool isBmpContentType(QByteArrayView contentType);   // :49-52, image/bmp
[[nodiscard]] bool isBmpContentType(const ContentTypeInfo& info);  // :54-57
[[nodiscard]] bool isGifContentType(QByteArrayView contentType);   // :59-62, image/gif
[[nodiscard]] bool isGifContentType(const ContentTypeInfo& info);  // :64-67
[[nodiscard]] bool isWebpContentType(QByteArrayView contentType);  // :69-72, image/webp
[[nodiscard]] bool isWebpContentType(const ContentTypeInfo& info); // :74-77
[[nodiscard]] bool isJpegContentType(QByteArrayView contentType);  // :79-85, jpg, jpeg, pjpeg
[[nodiscard]] bool isJpegContentType(const ContentTypeInfo& info); // :87-90
[[nodiscard]] bool isPngContentType(QByteArrayView contentType);   // :92-95, image/png
[[nodiscard]] bool isPngContentType(const ContentTypeInfo& info);  // :97-100

} // namespace cg::core
