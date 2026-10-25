// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Bitmap32/u_Bitmap32Static.pas:55-81
#include "cg/bitmap/bitmap32_static.h"

#include <stdexcept>
#include <utility>

namespace cg::bitmap {

Bitmap32Static::Bitmap32Static(core::HashValue hash, Bitmap32BufferPtr buffer) : buffer_(std::move(buffer)) {
    // unreachable in legacy (u_Bitmap32StaticFactory.pas:102-105 builds a static only for an assigned
    // buffer); no register row
    if (!buffer_) {
        throw std::invalid_argument("Bitmap32Static: null buffer");
    }
    hash_ = hash;
    data_ = buffer_->data();
    size_ = buffer_->size();
}

core::HashValue Bitmap32Static::hash() const {
    return hash_;
}

QPoint Bitmap32Static::size() const {
    return size_;
}

const core::Color32* Bitmap32Static::data() const {
    return data_;
}

} // namespace cg::bitmap
