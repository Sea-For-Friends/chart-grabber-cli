// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/BitmapTileSaveLoad/i_BitmapTileSaveLoad.pas:33-41 (IBitmapTileLoader, IBitmapTileSaver).
// Legacy exchanges IBinaryData; the port exchanges QByteArray (docs/specs/bitmap.md §2.4).

#include "cg/bitmap/bitmap32_static.h"

#include <QByteArray>

#include <memory>

namespace cg::bitmap {

class IBitmapTileLoader {
public:
    virtual ~IBitmapTileLoader() = default;
    [[nodiscard]] virtual Bitmap32StaticPtr load(const QByteArray& data) const = 0;
};

class IBitmapTileSaver {
public:
    virtual ~IBitmapTileSaver() = default;
    [[nodiscard]] virtual QByteArray save(const Bitmap32StaticPtr& bitmap) const = 0;
};

using BitmapTileLoaderPtr = std::shared_ptr<const IBitmapTileLoader>;
using BitmapTileSaverPtr = std::shared_ptr<const IBitmapTileSaver>;

} // namespace cg::bitmap
