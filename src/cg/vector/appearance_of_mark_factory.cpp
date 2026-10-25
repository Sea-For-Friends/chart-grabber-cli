// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/appearance_of_mark_factory.h"

#include "cg/vector/appearance_of_mark_line.h"
#include "cg/vector/appearance_of_mark_point.h"
#include "cg/vector/appearance_of_mark_polygon.h"

namespace cg::vector {

namespace {

// UpdateHashByInteger: legacy takes an Integer, so a TColor32 goes through bit-for-bit.
void updateByInteger(core::HashValue& h, quint32 value) noexcept {
    core::hashUpdate(h, static_cast<qint32>(value));
}

} // namespace

AppearancePtr AppearanceOfMarkFactory::createLineAppearance(Color32 lineColor, int lineWidth) const {
    core::HashValue hash = 0x443e31d70873bb6bULL;
    updateByInteger(hash, lineColor);
    updateByInteger(hash, static_cast<quint32>(lineWidth));
    return std::make_shared<const AppearanceOfMarkLine>(hash, lineColor, lineWidth);
}

AppearancePtr AppearanceOfMarkFactory::createPointAppearance(Color32 textColor, Color32 textBgColor,
                                                             int fontSize, const QString& picName,
                                                             const MarkPicturePtr& pic,
                                                             int markerSize) const {
    core::HashValue hash1 = 0xdf2118b946ed0b43ULL;
    updateByInteger(hash1, textColor);
    updateByInteger(hash1, textBgColor);
    updateByInteger(hash1, static_cast<quint32>(fontSize));

    core::HashValue hash2;
    if (pic) {
        hash2 = 0x7498e432f9619b27ULL;
        core::hashUpdate(hash2, pic->hash()); // UpdateHashByHash
    } else {
        hash2 = 0x31bfcd0d9f48d1d3ULL;
        core::hashUpdate(hash2, QStringView{picName}); // no-op for an empty string
    }
    updateByInteger(hash2, static_cast<quint32>(markerSize));

    core::HashValue hash = 0x40fd28c43506c95dULL;
    core::hashUpdate(hash, hash1);
    core::hashUpdate(hash, hash2);

    return std::make_shared<const AppearanceOfMarkPoint>(hash, hash1, hash2, textColor, textBgColor, fontSize,
                                                         markerSize, picName, pic);
}

AppearancePtr AppearanceOfMarkFactory::createPolygonAppearance(Color32 lineColor, int lineWidth,
                                                               Color32 fillColor) const {
    core::HashValue hash1 = 0x8184bab36bb79df0ULL;
    updateByInteger(hash1, lineColor);
    updateByInteger(hash1, static_cast<quint32>(lineWidth));

    core::HashValue hash2 = 0x11b87fb1b900cc39ULL;
    updateByInteger(hash2, fillColor);

    core::HashValue hash = 0x501f3e9b18861e44ULL;
    core::hashUpdate(hash, hash1);
    core::hashUpdate(hash, hash2);

    return std::make_shared<const AppearanceOfMarkPolygon>(hash, hash1, hash2, lineColor, lineWidth,
                                                           fillColor);
}

} // namespace cg::vector
