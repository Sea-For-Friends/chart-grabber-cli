// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// FEAT-NET-011 -- docs/specs/net.md §2.13, §7.6.
#include "cg/core/content_detector.h"

#include <QByteArray>

#include <gtest/gtest.h>

using cg::core::detectContentType;

TEST(ContentDetectorTest, DetectsJpeg) {
    EXPECT_EQ(detectContentType(QByteArray::fromHex("ffd800")), QByteArray("image/jpeg"));
}

TEST(ContentDetectorTest, DetectsPng) {
    EXPECT_EQ(detectContentType(QByteArray::fromHex("89504e4700")), QByteArray("image/png"));
}

TEST(ContentDetectorTest, DetectsWebp) {
    // RIFF + 4-byte size field + "WEBP", plus one byte to satisfy size > 12.
    const QByteArray data = QByteArray::fromHex("52494646") + QByteArray::fromHex("01020304") +
                            QByteArray::fromHex("57454250") + QByteArray::fromHex("00");
    ASSERT_EQ(data.size(), 13);
    EXPECT_EQ(detectContentType(data), QByteArray("image/webp"));
}

TEST(ContentDetectorTest, DetectsGif) {
    EXPECT_EQ(detectContentType(QByteArray::fromHex("4749463839")), QByteArray("image/gif"));
}

TEST(ContentDetectorTest, DetectsBmp) {
    EXPECT_EQ(detectContentType(QByteArray::fromHex("424d00")), QByteArray("image/bmp"));
}

TEST(ContentDetectorTest, DetectsTiffBigEndian) {
    EXPECT_EQ(detectContentType(QByteArray::fromHex("4d4d002a00")), QByteArray("image/tiff"));
}

TEST(ContentDetectorTest, DetectsTiffLittleEndian) {
    EXPECT_EQ(detectContentType(QByteArray::fromHex("49492a0000")), QByteArray("image/tiff"));
}

TEST(ContentDetectorTest, WebpRiffWithoutWebpReturnsEmpty) {
    // RIFF magic present, but bytes 8..11 are not "WEBP" -- the scan stops and returns "".
    const QByteArray data = QByteArray::fromHex("52494646") + QByteArray::fromHex("01020304") +
                            QByteArray::fromHex("41564920") + QByteArray::fromHex("00");
    ASSERT_EQ(data.size(), 13);
    EXPECT_EQ(detectContentType(data), QByteArray());
}

TEST(ContentDetectorTest, TooShortReturnsEmpty) {
    // Exactly 2 bytes: the "size > n" strictness means a 2-byte "FF D8" is not JPEG.
    EXPECT_EQ(detectContentType(QByteArray::fromHex("ffd8")), QByteArray());
}

TEST(ContentDetectorTest, UnknownBytesReturnEmpty) {
    EXPECT_EQ(detectContentType(QByteArray::fromHex("0011223344")), QByteArray());
}
