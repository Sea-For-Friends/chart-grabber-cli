// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: the implicit encoding behavior of TStringList.LoadFromStream / TMemIniFile.Encoding
//        (u_SASMainConfigProvider.pas:68, u_ConfigDataProviderByFolder.pas:118-121)
//
// One decoding rule for every legacy text config file (.ini, params.txt, .hlg, .sls): honour a
// BOM if there is one, otherwise UTF-8 if the bytes are valid UTF-8, otherwise CP1251 (the
// Windows ANSI codepage of the SAS.Planet user base). Legacy always wrote UTF-8 with a BOM.

#include "cg/core/result.h"

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace cg::core {

enum class TextEncoding : std::uint8_t { Utf8Bom, Utf8, Utf16LeBom, Utf16BeBom, Cp1251 };

struct DecodedText {
    QString text;
    TextEncoding encoding; // what the BOM said, or the fallback that was used
};

[[nodiscard]] DecodedText decodeConfigText(const QByteArray& raw);

// Encodes with a UTF-8 BOM, matching what legacy wrote.
[[nodiscard]] QByteArray encodeConfigText(const QString& text);

// Re-encodes in the encoding a file was loaded with, so that rewriting an untouched file is
// byte-identical (ADR-0010). `Utf8Bom` is the encoding of a newly created file. Best-effort:
// a character with no Cp1251 representation is substituted with '?' (see
// encodeConfigTextChecked() for the failing counterpart persistence must use).
[[nodiscard]] QByteArray encodeConfigText(const QString& text, TextEncoding encoding);

// Same as encodeConfigText(text, encoding), but fails loudly instead of substituting '?' when
// `encoding` cannot represent every character in `text` (only possible for Cp1251 -- UTF-8/
// UTF-16 represent all of Unicode). Spec §4 "Nothing is ever swallowed": a settings value that
// does not round-trip through the file's codec must not be silently corrupted on save.
[[nodiscard]] Result<QByteArray> encodeConfigTextChecked(const QString& text, TextEncoding encoding);

} // namespace cg::core
