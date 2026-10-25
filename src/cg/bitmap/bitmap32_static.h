// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Bitmap32/i_Bitmap32Static.pas:33-52 (IBitmap32Buffer, IBitmap32Static) and
// Src/Bitmap32/u_Bitmap32Static.pas:34-81 (TBitmap32Static). IBitmapChangeable (:54-57) is not
// ported (docs/specs/bitmap.md §1.1: no stage-0 caller).
//
// Pixel layout: core::Color32 is $AARRGGBB, not premultiplied. On a little-endian machine the bytes
// in memory are B, G, R, A, which is what libjpeg JCS_EXT_BGRA, libpng png_set_bgr and libwebp
// ImportBGRA expect, and what legacy relies on.

#include "cg/core/color32.h"
#include "cg/core/hash.h"

#include <QPoint>
#include <QtGlobal>

#include <cstddef>
#include <memory>

static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN, "bitmap pixels are B, G, R, A bytes: little-endian only");

namespace cg::bitmap {

// Number of pixels (x * y) of a size; the callers have already checked the size rule.
[[nodiscard]] inline std::size_t pixelCount(QPoint size) {
    return static_cast<std::size_t>(size.x()) * static_cast<std::size_t>(size.y());
}

// Mutable until it is wrapped by a static (i_Bitmap32Static.pas:33-40).
class IBitmap32Buffer {
public:
    virtual ~IBitmap32Buffer() = default;
    [[nodiscard]] virtual QPoint size() const = 0;
    [[nodiscard]] virtual core::Color32* data() = 0;
    [[nodiscard]] virtual const core::Color32* data() const = 0;
};
using Bitmap32BufferPtr = std::shared_ptr<IBitmap32Buffer>;

// Immutable pixel array with its content hash (i_Bitmap32Static.pas:42-52).
class IBitmap32Static {
public:
    virtual ~IBitmap32Static() = default;
    [[nodiscard]] virtual core::HashValue hash() const = 0;
    [[nodiscard]] virtual QPoint size() const = 0;
    // size().x() * size().y() values, row-major, top row first.
    [[nodiscard]] virtual const core::Color32* data() const = 0;
};
using Bitmap32StaticPtr = std::shared_ptr<const IBitmap32Static>;

// Ports TBitmap32Static: keeps the buffer alive and caches its data pointer and size.
class Bitmap32Static final : public IBitmap32Static { // TBitmap32Static
public:
    Bitmap32Static(core::HashValue hash, Bitmap32BufferPtr buffer);

    [[nodiscard]] core::HashValue hash() const override;
    [[nodiscard]] QPoint size() const override;
    [[nodiscard]] const core::Color32* data() const override;

private:
    Bitmap32BufferPtr buffer_;
    core::HashValue hash_ = core::kNoHash;
    const core::Color32* data_ = nullptr;
    QPoint size_;
};

} // namespace cg::bitmap
