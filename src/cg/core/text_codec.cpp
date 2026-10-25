// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/text_codec.h"

#include <QStringDecoder>
#include <QStringEncoder>

#include <array>
#include <cstdint>

namespace cg::core {
namespace {

constexpr QByteArrayView utf8Bom = "\xEF\xBB\xBF";

// CP1251 -> Unicode for the high half; the low half is ASCII. Qt 6 no longer ships the legacy
// codecs and Qt6::Core5Compat is a dependency this project will not take (spec 2.11).
// 0x98 is unassigned in CP1251 and maps to U+0098, as in the WHATWG windows-1251 index, so that
// every byte value round-trips.
constexpr std::array<char16_t, 128> cp1251ToUnicode = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A,
    0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0098, 0x2122,
    0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6,
    0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456,
    0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, 0x0410,
    0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D,
    0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A,
    0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444,
    0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F};

[[nodiscard]] QString decodeCp1251(QByteArrayView raw) {
    QString text;
    text.reserve(static_cast<qsizetype>(raw.size()));
    for (const char byte : raw) {
        const auto value = static_cast<std::uint8_t>(byte);
        text.append(value < 0x80 ? QChar{value} : QChar{cp1251ToUnicode[value - 0x80U]});
    }
    return text;
}

// `ok`, when non-null, is set to false if any character had no CP1251 representation and was
// substituted with '?'. The caller decides whether that is acceptable (encodeConfigText(),
// best-effort) or a failure (encodeConfigTextChecked(), spec §4 "nothing is ever swallowed").
[[nodiscard]] QByteArray encodeCp1251(const QString& text, bool* ok = nullptr) {
    QByteArray raw;
    raw.reserve(text.size());
    if (ok != nullptr) {
        *ok = true;
    }
    for (const QChar ch : text) {
        const char16_t unit = ch.unicode();
        if (unit < 0x80) {
            raw.append(static_cast<char>(unit));
            continue;
        }
        char mapped = '?';
        bool found = false;
        for (std::size_t i = 0; i < cp1251ToUnicode.size(); ++i) {
            if (cp1251ToUnicode[i] == unit) {
                mapped = static_cast<char>(static_cast<std::uint8_t>(i + 0x80U));
                found = true;
                break;
            }
        }
        if (!found && ok != nullptr) {
            *ok = false;
        }
        raw.append(mapped);
    }
    return raw;
}

// Finding 6 (T-0002A rework): a bare QStringDecoder never sees an explicit "end of stream", so a
// file truncated mid multi-byte sequence read as valid; QString::fromUtf8() substitutes U+FFFD
// for it, and the byte-identical round trip broke silently. Round-tripping through toUtf8() and
// comparing against the input catches that (and any other non-canonical sequence) without
// depending on QStringDecoder's streaming-flush semantics.
[[nodiscard]] bool isValidUtf8(const QByteArray& raw) {
    return QString::fromUtf8(raw).toUtf8() == raw;
}

} // namespace

DecodedText decodeConfigText(const QByteArray& raw) {
    if (raw.startsWith(utf8Bom)) {
        return {QString::fromUtf8(raw.sliced(3)), TextEncoding::Utf8Bom};
    }
    if (raw.size() >= 2) {
        const auto first = static_cast<std::uint8_t>(raw[0]);
        const auto second = static_cast<std::uint8_t>(raw[1]);
        if (first == 0xFF && second == 0xFE) {
            QStringDecoder decoder(QStringDecoder::Utf16LE);
            return {decoder(raw.sliced(2)), TextEncoding::Utf16LeBom};
        }
        if (first == 0xFE && second == 0xFF) {
            QStringDecoder decoder(QStringDecoder::Utf16BE);
            return {decoder(raw.sliced(2)), TextEncoding::Utf16BeBom};
        }
    }
    if (isValidUtf8(raw)) {
        return {QString::fromUtf8(raw), TextEncoding::Utf8};
    }
    return {decodeCp1251(raw), TextEncoding::Cp1251};
}

QByteArray encodeConfigText(const QString& text) {
    return encodeConfigText(text, TextEncoding::Utf8Bom);
}

QByteArray encodeConfigText(const QString& text, TextEncoding encoding) {
    switch (encoding) {
    case TextEncoding::Utf8Bom:
        return utf8Bom.toByteArray() + text.toUtf8();
    case TextEncoding::Utf8:
        return text.toUtf8();
    case TextEncoding::Utf16LeBom: {
        QStringEncoder encoder(QStringEncoder::Utf16LE);
        return QByteArray("\xFF\xFE", 2) + encoder(text);
    }
    case TextEncoding::Utf16BeBom: {
        QStringEncoder encoder(QStringEncoder::Utf16BE);
        return QByteArray("\xFE\xFF", 2) + encoder(text);
    }
    case TextEncoding::Cp1251:
        return encodeCp1251(text);
    }
    return text.toUtf8();
}

Result<QByteArray> encodeConfigTextChecked(const QString& text, TextEncoding encoding) {
    if (encoding == TextEncoding::Cp1251) {
        bool ok = false;
        QByteArray raw = encodeCp1251(text, &ok);
        if (!ok) {
            return fail(ErrorCode::Unsupported,
                        QStringLiteral("value contains a character not representable in CP1251"));
        }
        return raw;
    }
    return encodeConfigText(text, encoding);
}

} // namespace cg::core
