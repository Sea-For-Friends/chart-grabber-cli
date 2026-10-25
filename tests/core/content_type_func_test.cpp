// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/core/content_type_func (Src/ContentType/u_ContentTypeFunc.pas :49-100). T-0183.
// FEAT-REG-069 (Export to MBTiles) is contributed to by this unit (the direct-copy predicate uses it).
//
// Expectations are literals from the Pascal: IsBmp image/bmp, IsGif image/gif, IsWebp image/webp,
// IsJpeg image/jpg | image/jpeg | image/pjpeg, IsPng image/png, each compared with SameTextA (equality
// without trimming, ASCII letters fold).
#include "cg/core/content_type.h"
#include "cg/core/content_type_func.h"

#include <QByteArray>

#include <gtest/gtest.h>

#include <vector>

using namespace cg::core;

namespace {

enum class Kind { None, Bmp, Gif, Webp, Jpeg, Png };

struct Case {
    const char* mime;
    Kind kind; // the one predicate that must answer true
};

const std::vector<Case>& cases() {
    static const std::vector<Case> c = {
        {"image/bmp", Kind::Bmp},
        {"IMAGE/BMP", Kind::Bmp},
        {"Image/Bmp", Kind::Bmp},
        {"image/gif", Kind::Gif},
        {"IMAGE/GIF", Kind::Gif},
        {"Image/Gif", Kind::Gif},
        {"image/webp", Kind::Webp},
        {"IMAGE/WEBP", Kind::Webp},
        {"Image/WebP", Kind::Webp},
        {"image/jpg", Kind::Jpeg},
        {"IMAGE/JPG", Kind::Jpeg},
        {"Image/Jpg", Kind::Jpeg},
        {"image/jpeg", Kind::Jpeg},
        {"IMAGE/JPEG", Kind::Jpeg},
        {"Image/JpEg", Kind::Jpeg},
        {"image/pjpeg", Kind::Jpeg},
        {"IMAGE/PJPEG", Kind::Jpeg},
        {"Image/PJpeg", Kind::Jpeg},
        {"image/png", Kind::Png},
        {"IMAGE/PNG", Kind::Png},
        {"Image/Png", Kind::Png},
        // no trimming: a blank before or after makes a different text
        {"image/jpeg ", Kind::None},
        {" image/jpeg", Kind::None},
        {"image/png ", Kind::None},
        {" image/png", Kind::None},
        {"image/bmp ", Kind::None},
        {"image/gif\t", Kind::None},
        {"image/webp ", Kind::None},
        {"image/pjpeg ", Kind::None},
        // other types and near misses
        {"image/tiff", Kind::None},
        {"image/x-icon", Kind::None},
        {"image/jpe", Kind::None},
        {"image/jpegx", Kind::None},
        {"image/x-ms-bmp", Kind::None},
        {"jpeg", Kind::None},
        {"image/", Kind::None},
        {"", Kind::None},
        // non-ASCII bytes never fold onto ASCII letters: Cyrillic er (U+0440) instead of Latin p
        {"image/\xD1\x80ng", Kind::None},
        {"\xD0\x98mage/png", Kind::None},
    };
    return c;
}

} // namespace

TEST(ContentTypeFunc, EachMimeSelectsExactlyOnePredicateInBothOverloads) {
    for (const Case& c : cases()) {
        const QByteArray mime(c.mime);
        const ContentTypeInfo info{mime, QByteArray(".x"), ContentKind::Bitmap};
        EXPECT_EQ(isBmpContentType(mime), c.kind == Kind::Bmp) << c.mime;
        EXPECT_EQ(isGifContentType(mime), c.kind == Kind::Gif) << c.mime;
        EXPECT_EQ(isWebpContentType(mime), c.kind == Kind::Webp) << c.mime;
        EXPECT_EQ(isJpegContentType(mime), c.kind == Kind::Jpeg) << c.mime;
        EXPECT_EQ(isPngContentType(mime), c.kind == Kind::Png) << c.mime;
        EXPECT_EQ(isBmpContentType(info), c.kind == Kind::Bmp) << c.mime;
        EXPECT_EQ(isGifContentType(info), c.kind == Kind::Gif) << c.mime;
        EXPECT_EQ(isWebpContentType(info), c.kind == Kind::Webp) << c.mime;
        EXPECT_EQ(isJpegContentType(info), c.kind == Kind::Jpeg) << c.mime;
        EXPECT_EQ(isPngContentType(info), c.kind == Kind::Png) << c.mime;
    }
}

TEST(ContentTypeFunc, TheInfoOverloadReadsTheTypeNotTheExtensionOrTheKind) {
    // the IContentTypeInfoBasic overload forwards GetContentType only (:54-57 ...)
    const ContentTypeInfo pngWithJpgExt{QByteArray("image/png"), QByteArray(".jpg"), ContentKind::Vector};
    EXPECT_TRUE(isPngContentType(pngWithJpgExt));
    EXPECT_FALSE(isJpegContentType(pngWithJpgExt));
    const ContentTypeInfo jpgWithPngExt{QByteArray("image/jpg"), QByteArray(".png"), ContentKind::Bitmap};
    EXPECT_TRUE(isJpegContentType(jpgWithPngExt));
    EXPECT_FALSE(isPngContentType(jpgWithPngExt));
}
