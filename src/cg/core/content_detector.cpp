// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Network/u_ContentDetecter.pas:60-148.
#include "cg/core/content_detector.h"

#include <array>
#include <cstring>

namespace cg::core {

namespace {

struct Magic {
    const char* type;
    std::array<unsigned char, 4> bytes;
    qsizetype n;
};

// Evaluated strictly in this order (docs/specs/net.md §2.13).
constexpr std::array<Magic, 7> kMagic = {{
    {"image/jpeg", {0xFF, 0xD8, 0x00, 0x00}, 2},
    {"image/png", {0x89, 0x50, 0x4E, 0x47}, 4},
    {"image/webp", {0x52, 0x49, 0x46, 0x46}, 4}, // "RIFF"
    {"image/gif", {0x47, 0x49, 0x46, 0x38}, 4},
    {"image/bmp", {0x42, 0x4D, 0x00, 0x00}, 2},
    {"image/tiff", {0x4D, 0x4D, 0x00, 0x2A}, 4},
    {"image/tiff", {0x49, 0x49, 0x2A, 0x00}, 4},
}};

[[nodiscard]] bool isWebp(QByteArrayView data) {
    return data.size() > 12 && std::memcmp(data.constData() + 8, "WEBP", 4) == 0;
}

} // namespace

QByteArray detectContentType(QByteArrayView data) {
    if (data.isEmpty()) {
        return {};
    }
    for (const Magic& magic : kMagic) {
        if (data.size() > magic.n &&
            std::memcmp(data.constData(), magic.bytes.data(), static_cast<size_t>(magic.n)) == 0) {
            if (std::strcmp(magic.type, "image/webp") == 0 && !isWebp(data)) {
                return {}; // The scan stops here -- legacy behavior, not a bug.
            }
            return QByteArray(magic.type);
        }
    }
    return {};
}

} // namespace cg::core
