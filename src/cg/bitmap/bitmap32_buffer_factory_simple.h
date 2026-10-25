// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Bitmap32/u_Bitmap32BufferFactorySimple.pas:34-159 (TBitmap32BufferFactorySimple). The
// private buffer class TBitmap32StaticSimple (:53-106) lives in the .cpp.
//
// The size rule of buildEmpty (0 < x < 65536, 0 < y < 65536, x * y < 2^28) is written once, here.

#include "cg/bitmap/bitmap32_buffer_factory.h"

#include <QPoint>

namespace cg::bitmap {

class Bitmap32BufferFactorySimple final : public IBitmap32BufferFactory { // TBitmap32BufferFactorySimple
public:
    [[nodiscard]] Bitmap32BufferPtr build(QPoint size, const core::Color32* data) const override;
    [[nodiscard]] Bitmap32BufferPtr buildEmpty(QPoint size) const override;
    [[nodiscard]] Bitmap32BufferPtr buildEmptyClear(QPoint size, core::Color32 color) const override;
};

} // namespace cg::bitmap
