// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-UI-200 (file decoding for the ini backend); docs/specs/core.md 2.11, plan 7.6
#include "cg/core/text_codec.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <gtest/gtest.h>

#include "fixture_path.h"

using cg::core::decodeConfigText;
using cg::core::DecodedText;
using cg::core::encodeConfigText;
using cg::core::encodeConfigTextChecked;
using cg::core::ErrorCode;
using cg::core::TextEncoding;

namespace {

QByteArray readFixture(const QString& relativePath) {
    QFile file(cgFixture(relativePath));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << relativePath.toStdString();
    return file.readAll();
}

} // namespace

TEST(TextCodec, Utf8BomDetected) {
    const QByteArray raw = QByteArray("\xEF\xBB\xBF") + QByteArray("Key=Значение");
    const DecodedText decoded = decodeConfigText(raw);
    EXPECT_EQ(decoded.encoding, TextEncoding::Utf8Bom);
    EXPECT_EQ(decoded.text, QStringLiteral("Key=Значение"));
}

TEST(TextCodec, Utf16LeBomDetected) {
    QByteArray raw("\xFF\xFE", 2);
    raw += QByteArray("A\0B\0", 4);
    const DecodedText decoded = decodeConfigText(raw);
    EXPECT_EQ(decoded.encoding, TextEncoding::Utf16LeBom);
    EXPECT_EQ(decoded.text, QStringLiteral("AB"));
}

TEST(TextCodec, Utf16BeBomDetected) {
    QByteArray raw("\xFE\xFF", 2);
    raw += QByteArray("\0A\0B", 4);
    const DecodedText decoded = decodeConfigText(raw);
    EXPECT_EQ(decoded.encoding, TextEncoding::Utf16BeBom);
    EXPECT_EQ(decoded.text, QStringLiteral("AB"));
}

TEST(TextCodec, BomlessValidUtf8IsUtf8) {
    const DecodedText decoded = decodeConfigText(QByteArray("name=Ростов"));
    EXPECT_EQ(decoded.encoding, TextEncoding::Utf8);
    EXPECT_EQ(decoded.text, QStringLiteral("name=Ростов"));
}

TEST(TextCodec, BomlessInvalidUtf8FallsBackToCp1251) {
    CG_SKIP_IF_NO_CORPUS();
    const QByteArray raw =
        readFixture(QStringLiteral("zmp/sas.maps/_Coverage/Beeline/Beeline_IoT.zmp/params.txt"));
    ASSERT_FALSE(raw.isEmpty());
    const DecodedText decoded = decodeConfigText(raw);
    EXPECT_EQ(decoded.encoding, TextEncoding::Cp1251);
    EXPECT_TRUE(decoded.text.contains(QStringLiteral("Сотовые сети")));
}

// Finding 6 (T-0002A rework): a file truncated mid multi-byte UTF-8 sequence must not be reported
// valid UTF-8 (it would then decode with a silently substituted U+FFFD and never round-trip).
TEST(TextCodec, BomlessTruncatedMultiByteSequenceFallsBackToCp1251) {
    QByteArray raw = "name=";
    raw.append(static_cast<char>(0xD0)); // start of a 2-byte UTF-8 sequence, no continuation byte
    const DecodedText decoded = decodeConfigText(raw);
    EXPECT_EQ(decoded.encoding, TextEncoding::Cp1251);
}

TEST(TextCodec, EncodeAddsUtf8Bom) {
    const QByteArray encoded = encodeConfigText(QStringLiteral("Key=1"));
    EXPECT_TRUE(encoded.startsWith(QByteArray("\xEF\xBB\xBF")));
    EXPECT_EQ(encoded, QByteArray("\xEF\xBB\xBF") + QByteArray("Key=1"));
}

// Finding 2 (T-0002A rework): encodeConfigTextChecked must fail loudly instead of the '?'
// substitution encodeConfigText performs, per spec §4 "nothing is ever swallowed".
TEST(TextCodec, EncodeCheckedCp1251RoundTripSucceeds) {
    const auto result = encodeConfigTextChecked(QStringLiteral("Значение"), TextEncoding::Cp1251);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(decodeConfigText(*result).text, QStringLiteral("Значение"));
}

TEST(TextCodec, EncodeCheckedCp1251FailsOnUnrepresentableChar) {
    // U+4E2D ("中") has no CP1251 code point; encodeConfigText() would silently emit '?'.
    const auto result = encodeConfigTextChecked(QStringLiteral("map=中"), TextEncoding::Cp1251);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::Unsupported);
    // The lossy sibling still substitutes -- this pins the divergence the checked path exists to
    // prevent from reaching disk.
    EXPECT_TRUE(encodeConfigText(QStringLiteral("map=中"), TextEncoding::Cp1251).contains('?'));
}

TEST(TextCodec, DecodeEncodeRoundTripIsIdentity) {
    CG_SKIP_IF_NO_CORPUS();
    const QStringList fixtures = {
        QStringLiteral("ini/FormsState.ini"),
        QStringLiteral("ini/TileStorage_DBMS.Default.ini"),
        QStringLiteral("ini/TerrainData/SASTerrain.Default.ini"),
        QStringLiteral("ini/quirks.ini"),
        QStringLiteral("zmp/sas.maps/_Coverage/Beeline/Beeline_IoT.zmp/params.txt"),
    };
    for (const QString& fixture : fixtures) {
        const QByteArray raw = readFixture(fixture);
        const DecodedText decoded = decodeConfigText(raw);
        EXPECT_EQ(encodeConfigText(decoded.text, decoded.encoding), raw) << fixture.toStdString();
    }
}
