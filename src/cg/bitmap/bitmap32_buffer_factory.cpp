// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Bitmap32/u_Bitmap32BufferFactory.pas:66-124 (the pool branch of BuildEmpty, :108-113,
// and the pool itself, :72-78, are not ported: D row of T-0155)
#include "cg/bitmap/bitmap32_buffer_factory.h"

#include "cg/bitmap/bitmap32_buffer_factory_simple.h"

#include <algorithm>
#include <memory>

namespace cg::bitmap {

Bitmap32BufferFactory::Bitmap32BufferFactory() : simple_(std::make_shared<Bitmap32BufferFactorySimple>()) {}

Bitmap32BufferPtr Bitmap32BufferFactory::build(QPoint size, const core::Color32* data) const {
    // Ports :82-100 (Asserts :87-92 are off). Own body: it does not share code with the simple factory.
    auto result = buildEmpty(size);
    if (result && data) {
        std::copy_n(data, pixelCount(size), result->data());
    }
    return result;
}

Bitmap32BufferPtr Bitmap32BufferFactory::buildEmpty(QPoint size) const {
    // Ports :102-114. The pool branch (:108-110) is not ported; every size goes to the simple factory.
    return simple_->buildEmpty(size);
}

Bitmap32BufferPtr Bitmap32BufferFactory::buildEmptyClear(QPoint size, core::Color32 color) const {
    // Ports :116-125.
    auto result = buildEmpty(size);
    if (result) {
        std::fill_n(result->data(), pixelCount(size), color);
    }
    return result;
}

} // namespace cg::bitmap
