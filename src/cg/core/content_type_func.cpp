// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/ContentType/u_ContentTypeFunc.pas
#include "cg/core/content_type_func.h"

#include "cg/core/delphi_text.h"

#include <QString>

namespace cg::core {

namespace {

// SameTextA (u_AnsiStr.pas:62). The bytes are compared as Latin-1 text, so the ASCII folding of
// delphiSameText applies to the 7-bit letters only.
bool sameTextA(QByteArrayView contentType, QByteArrayView expected) {
    return delphiSameText(QString::fromLatin1(contentType), QString::fromLatin1(expected));
}

} // namespace

bool isBmpContentType(QByteArrayView contentType) {
    return sameTextA(contentType, "image/bmp");
}

bool isBmpContentType(const ContentTypeInfo& info) {
    return isBmpContentType(info.type);
}

bool isGifContentType(QByteArrayView contentType) {
    return sameTextA(contentType, "image/gif");
}

bool isGifContentType(const ContentTypeInfo& info) {
    return isGifContentType(info.type);
}

bool isWebpContentType(QByteArrayView contentType) {
    return sameTextA(contentType, "image/webp");
}

bool isWebpContentType(const ContentTypeInfo& info) {
    return isWebpContentType(info.type);
}

bool isJpegContentType(QByteArrayView contentType) {
    return sameTextA(contentType, "image/jpg") || sameTextA(contentType, "image/jpeg") ||
           sameTextA(contentType, "image/pjpeg");
}

bool isJpegContentType(const ContentTypeInfo& info) {
    return isJpegContentType(info.type);
}

bool isPngContentType(QByteArrayView contentType) {
    return sameTextA(contentType, "image/png");
}

bool isPngContentType(const ContentTypeInfo& info) {
    return isPngContentType(info.type);
}

} // namespace cg::core
