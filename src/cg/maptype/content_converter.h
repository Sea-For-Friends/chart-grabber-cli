// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/ContentType/i_ContentConverter.pas:31-37 (IContentConverter) and
// Src/ContentType/u_ContentConverterBase.pas:85-100 (TContentConverterBase, TContentConverterSimpleCopy).
// GetSource / GetTarget are not kept: no caller outside the converter units (docs/backlog/T-0120).

#include <QByteArray>

#include <memory>
#include <optional>

namespace cg::maptype {

class IContentConverter {
public:
    virtual ~IContentConverter() = default;
    [[nodiscard]] virtual bool isSimpleCopy() const = 0; // GetIsSimpleCopy
    // std::nullopt is legacy's nil IBinaryData. Throws like legacy raised (bitmap::BitmapCodecError).
    [[nodiscard]] virtual std::optional<QByteArray> convert(const QByteArray& data) const = 0; // Convert
};

using ContentConverterPtr = std::shared_ptr<const IContentConverter>;

class ContentConverterSimpleCopy final : public IContentConverter { // TContentConverterSimpleCopy :92-100
public:
    [[nodiscard]] bool isSimpleCopy() const override { return true; }
    [[nodiscard]] std::optional<QByteArray> convert(const QByteArray& data) const override { return data; }
};

} // namespace cg::maptype
