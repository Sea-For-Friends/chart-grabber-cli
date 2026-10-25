// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Bitmap32/u_Bitmap32StaticFactory.pas:61-107
#include "cg/bitmap/bitmap32_static_factory.h"

#include "cg/core/hash.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

namespace cg::bitmap {

Bitmap32StaticFactory::Bitmap32StaticFactory(Bitmap32BufferFactoryPtr bufferFactory)
    : bitmapFactory_(std::move(bufferFactory)) {
    // unreachable in legacy (u_GlobalState.pas:555-563 passes the buffer factory it has just created); no
    // register row
    if (!bitmapFactory_) {
        throw std::invalid_argument("Bitmap32StaticFactory: null buffer factory");
    }
}

Bitmap32BufferFactoryPtr Bitmap32StaticFactory::bufferFactory() const {
    return bitmapFactory_; // :73-76
}

Bitmap32StaticPtr Bitmap32StaticFactory::build(QPoint size, const core::Color32* data) const {
    // Ports :78-92 (Asserts :85 and :88 are off in the shipped build). A null buffer gives a null static.
    Bitmap32StaticPtr result;
    auto buffer = bitmapFactory_->build(size, data);
    if (buffer) {
        result = buildWithOwnBuffer(std::move(buffer));
    }
    return result;
}

Bitmap32StaticPtr Bitmap32StaticFactory::buildWithOwnBuffer(Bitmap32BufferPtr buffer) const {
    // Ports :94-107 (Assert :101 is off). The hash covers x * y * 4 bytes of the pixels.
    if (!buffer) {
        return nullptr;
    }
    const QPoint size = buffer->size();
    const std::size_t byteCount = pixelCount(size) * sizeof(core::Color32);
    const core::HashValue hash = core::cityHash64(buffer->data(), byteCount);
    return std::make_shared<Bitmap32Static>(hash, std::move(buffer));
}

} // namespace cg::bitmap
