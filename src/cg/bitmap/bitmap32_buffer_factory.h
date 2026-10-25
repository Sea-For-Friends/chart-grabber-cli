// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Bitmap32/i_Bitmap32BufferFactory.pas:32-55 (IBitmap32BufferFactory, IBitmap32StaticFactory)
// and Src/Bitmap32/u_Bitmap32BufferFactory.pas:37-125 (TBitmap32BufferFactory, without the
// 256 x 256 object pool: docs/parity/deviations.md, the D row of T-0155).
//
// The buffer factory forwards buildEmpty to the simple factory (bitmap32_buffer_factory_simple.h) and
// keeps its own build / buildEmptyClear, as legacy does.

#include "cg/bitmap/bitmap32_static.h"

#include <QPoint>

#include <memory>

namespace cg::bitmap {

class IBitmap32BufferFactory {
public:
    virtual ~IBitmap32BufferFactory() = default;
    // data may be null: the buffer is then returned without its pixels copied (legacy `if AData <> nil`).
    [[nodiscard]] virtual Bitmap32BufferPtr build(QPoint size, const core::Color32* data) const = 0;
    [[nodiscard]] virtual Bitmap32BufferPtr buildEmpty(QPoint size) const = 0;
    [[nodiscard]] virtual Bitmap32BufferPtr buildEmptyClear(QPoint size, core::Color32 color) const = 0;
};
using Bitmap32BufferFactoryPtr = std::shared_ptr<const IBitmap32BufferFactory>;

class IBitmap32StaticFactory {
public:
    virtual ~IBitmap32StaticFactory() = default;
    [[nodiscard]] virtual Bitmap32BufferFactoryPtr bufferFactory() const = 0;
    [[nodiscard]] virtual Bitmap32StaticPtr build(QPoint size, const core::Color32* data) const = 0;
    [[nodiscard]] virtual Bitmap32StaticPtr buildWithOwnBuffer(Bitmap32BufferPtr buffer) const = 0;
};
using Bitmap32StaticFactoryPtr = std::shared_ptr<const IBitmap32StaticFactory>;

class Bitmap32BufferFactory final : public IBitmap32BufferFactory { // TBitmap32BufferFactory
public:
    Bitmap32BufferFactory(); // creates the simple factory (legacy Create, u_Bitmap32BufferFactory.pas:79)

    [[nodiscard]] Bitmap32BufferPtr build(QPoint size, const core::Color32* data) const override;
    [[nodiscard]] Bitmap32BufferPtr buildEmpty(QPoint size) const override;
    [[nodiscard]] Bitmap32BufferPtr buildEmptyClear(QPoint size, core::Color32 color) const override;

private:
    Bitmap32BufferFactoryPtr simple_; // FFactorySimple
};

} // namespace cg::bitmap
