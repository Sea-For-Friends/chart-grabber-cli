// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing (docs/specs/bitmap.md §4). Replaces the Exception texts of the codec units: legacy raises a
// plain Exception / ELibJpegException; the port raises this type, the text is UTF-8.

#include <stdexcept>
#include <string>

namespace cg::bitmap {

class BitmapCodecError : public std::runtime_error {
public:
    explicit BitmapCodecError(const std::string& text) : std::runtime_error(text) {}
};

} // namespace cg::bitmap
