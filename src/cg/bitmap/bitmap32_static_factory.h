// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Bitmap32/u_Bitmap32StaticFactory.pas:36-107 (TBitmap32StaticFactory). Legacy injects an
// IHashFunction; the port calls core::cityHash64, the function every other module calls (the legacy
// hash function is THashFunctionCityHash, u_GlobalState.pas:507).

#include "cg/bitmap/bitmap32_buffer_factory.h"

#include <QPoint>

#include <memory>

namespace cg::bitmap {

class Bitmap32StaticFactory final : public IBitmap32StaticFactory { // TBitmap32StaticFactory
public:
    explicit Bitmap32StaticFactory(Bitmap32BufferFactoryPtr bufferFactory);

    [[nodiscard]] Bitmap32BufferFactoryPtr bufferFactory() const override;
    [[nodiscard]] Bitmap32StaticPtr build(QPoint size, const core::Color32* data) const override;
    [[nodiscard]] Bitmap32StaticPtr buildWithOwnBuffer(Bitmap32BufferPtr buffer) const override;

private:
    Bitmap32BufferFactoryPtr bitmapFactory_; // FBitmapFactory
};

} // namespace cg::bitmap
