// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Bitmap32/u_Bitmap32BufferFactorySimple.pas:53-159
#include "cg/bitmap/bitmap32_buffer_factory_simple.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace cg::bitmap {

namespace {

// TBitmap32StaticSimple (:53-106). Legacy GetMem: the pixels are not zeroed, so the array is
// created with std::make_unique_for_overwrite (default-initialises; std::make_unique would zero it).
class Bitmap32StaticSimple final : public IBitmap32Buffer {
public:
    explicit Bitmap32StaticSimple(QPoint size)
        : size_(size), bits_(std::make_unique_for_overwrite<core::Color32[]>(pixelCount(size))) {}

    [[nodiscard]] QPoint size() const override { return size_; }
    [[nodiscard]] core::Color32* data() override { return bits_.get(); }
    [[nodiscard]] const core::Color32* data() const override { return bits_.get(); }

private:
    QPoint size_;
    std::unique_ptr<core::Color32[]> bits_;
};

} // namespace

// buildEmpty is the only place of the size rule (the constructor check at :81-86 is the same test).
Bitmap32BufferPtr Bitmap32BufferFactorySimple::buildEmpty(QPoint size) const {
    // Ports :131-148. The Asserts (:135-139) are off in the shipped build (SASPlanet.dproj:92).
    // The product is taken in 64 bits: legacy `X * Y` is a 32-bit Longint and wraps (L row of T-0155).
    const auto x = static_cast<std::int64_t>(size.x());
    const auto y = static_cast<std::int64_t>(size.y());
    constexpr std::int64_t kLimit = std::int64_t{1} << 16;
    constexpr std::int64_t kMaxPixels = std::int64_t{1} << 28;
    if (x > 0 && y > 0 && x < kLimit && y < kLimit && x * y < kMaxPixels) {
        return std::make_shared<Bitmap32StaticSimple>(size);
    }
    return nullptr;
}

Bitmap32BufferPtr Bitmap32BufferFactorySimple::build(QPoint size, const core::Color32* data) const {
    // Ports :111-129 (Asserts :116-121 are off). The copy is skipped for a null data, as in legacy.
    auto result = buildEmpty(size);
    if (result && data) {
        std::copy_n(data, pixelCount(size), result->data());
    }
    return result;
}

Bitmap32BufferPtr Bitmap32BufferFactorySimple::buildEmptyClear(QPoint size, core::Color32 color) const {
    // Ports :150-159 (FillLongword).
    auto result = buildEmpty(size);
    if (result) {
        std::fill_n(result->data(), pixelCount(size), color);
    }
    return result;
}

} // namespace cg::bitmap
