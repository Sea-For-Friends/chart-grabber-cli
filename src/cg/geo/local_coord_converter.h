// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_LocalCoordConverter.pas, u_LocalCoordConverter.pas,
// i_LocalCoordConverterFactory.pas, u_LocalCoordConverterFactory.pas,
// i_LocalCoordConverterFactorySimpe.pas, u_LocalCoordConverterFactorySimpe.pas
//
// The renderer's per-frame object: an IProjection, a viewport rect in *local* (widget) pixels, a
// scale, and the map-pixel coordinate that sits at local (0,0). Immutable snapshot, no locks
// (§8.1); a new frame gets a new converter, never a mutated one.

#include "cg/geo/geo_types.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection.h"

#include <memory>

namespace cg::geo {

class ILocalCoordConverter {
public:
    virtual ~ILocalCoordConverter() = default;

    [[nodiscard]] virtual core::HashValue hash() const noexcept = 0;
    [[nodiscard]] virtual bool isSameConverter(const ILocalCoordConverter& other) const = 0;

    [[nodiscard]] virtual const ProjectionPtr& projection() const noexcept = 0;
    [[nodiscard]] virtual double scale() const noexcept = 0;
    [[nodiscard]] virtual RectI localRect() const noexcept = 0;
    [[nodiscard]] virtual PointI localRectSize() const noexcept = 0;
    [[nodiscard]] virtual RectI rectInMapPixel() const = 0;
    [[nodiscard]] virtual DoubleRect rectInMapPixelFloat() const = 0;
    [[nodiscard]] virtual DoublePoint centerMapPixelFloat() const = 0;
    [[nodiscard]] virtual LonLat centerLonLat() const = 0;

    [[nodiscard]] virtual PointI localPixel2MapPixel(PointI p, PointRounding r) const = 0;
    [[nodiscard]] virtual DoublePoint localPixel2MapPixelFloat(PointI p) const = 0;
    [[nodiscard]] virtual DoublePoint localPixelFloat2MapPixelFloat(DoublePoint p) const = 0;
    [[nodiscard]] virtual PointI mapPixel2LocalPixel(PointI p, PointRounding r) const = 0;
    [[nodiscard]] virtual DoublePoint mapPixel2LocalPixelFloat(PointI p) const = 0;
    [[nodiscard]] virtual DoublePoint mapPixelFloat2LocalPixelFloat(DoublePoint p) const = 0;

    [[nodiscard]] virtual RectI localRect2MapRect(const RectI& r, RectRounding m) const = 0;
    [[nodiscard]] virtual DoubleRect localRect2MapRectFloat(const RectI& r) const = 0;
    [[nodiscard]] virtual DoubleRect localRectFloat2MapRectFloat(const DoubleRect& r) const = 0;
    [[nodiscard]] virtual RectI mapRect2LocalRect(const RectI& r, RectRounding m) const = 0;
    [[nodiscard]] virtual DoubleRect mapRect2LocalRectFloat(const RectI& r) const = 0;
    [[nodiscard]] virtual DoubleRect mapRectFloat2LocalRectFloat(const DoubleRect& r) const = 0;

    [[nodiscard]] virtual PointI lonLat2LocalPixel(LonLat p, PointRounding r) const = 0;
    [[nodiscard]] virtual DoublePoint lonLat2LocalPixelFloat(LonLat p) const = 0;
    [[nodiscard]] virtual DoubleRect lonLatRect2LocalRectFloat(const LonLatRect& r) const = 0;
};
using LocalCoordConverterPtr = std::shared_ptr<const ILocalCoordConverter>;

// u_LocalCoordConverter.pas. Legacy has three classes (TLocalCoordConverter,
// TLocalCoordConverterNoScaleIntDelta, TLocalCoordConverterNoScale) that differ only in whether
// they multiply by `scale`; ported as one class with a `scale_ == 1.0` fast path
// (docs/specs/geo.md §2.7), not three.
class LocalCoordConverter final : public ILocalCoordConverter {
public:
    LocalCoordConverter(core::HashValue hash, RectI localRect, RectI rectInMapPixel,
                        DoubleRect rectInMapPixelFloat, DoublePoint mapPixelCenter, ProjectionPtr projection,
                        double scale, DoublePoint mapPixelAtLocalZero);

    [[nodiscard]] core::HashValue hash() const noexcept override { return hash_; }
    [[nodiscard]] bool isSameConverter(const ILocalCoordConverter& other) const override;

    [[nodiscard]] const ProjectionPtr& projection() const noexcept override { return projection_; }
    [[nodiscard]] double scale() const noexcept override { return scale_; }
    [[nodiscard]] RectI localRect() const noexcept override { return localRect_; }
    [[nodiscard]] PointI localRectSize() const noexcept override { return localSize_; }
    [[nodiscard]] RectI rectInMapPixel() const override { return rectInMapPixel_; }
    [[nodiscard]] DoubleRect rectInMapPixelFloat() const override { return rectInMapPixelFloat_; }
    [[nodiscard]] DoublePoint centerMapPixelFloat() const override { return mapPixelCenter_; }
    [[nodiscard]] LonLat centerLonLat() const override {
        return projection_->pixelPosFloat2LonLat(mapPixelCenter_);
    }

    [[nodiscard]] PointI localPixel2MapPixel(PointI p, PointRounding r) const override;
    [[nodiscard]] DoublePoint localPixel2MapPixelFloat(PointI p) const override;
    [[nodiscard]] DoublePoint localPixelFloat2MapPixelFloat(DoublePoint p) const override;
    [[nodiscard]] PointI mapPixel2LocalPixel(PointI p, PointRounding r) const override;
    [[nodiscard]] DoublePoint mapPixel2LocalPixelFloat(PointI p) const override;
    [[nodiscard]] DoublePoint mapPixelFloat2LocalPixelFloat(DoublePoint p) const override;

    [[nodiscard]] RectI localRect2MapRect(const RectI& r, RectRounding m) const override;
    [[nodiscard]] DoubleRect localRect2MapRectFloat(const RectI& r) const override;
    [[nodiscard]] DoubleRect localRectFloat2MapRectFloat(const DoubleRect& r) const override;
    [[nodiscard]] RectI mapRect2LocalRect(const RectI& r, RectRounding m) const override;
    [[nodiscard]] DoubleRect mapRect2LocalRectFloat(const RectI& r) const override;
    [[nodiscard]] DoubleRect mapRectFloat2LocalRectFloat(const DoubleRect& r) const override;

    [[nodiscard]] PointI lonLat2LocalPixel(LonLat p, PointRounding r) const override;
    [[nodiscard]] DoublePoint lonLat2LocalPixelFloat(LonLat p) const override;
    [[nodiscard]] DoubleRect lonLatRect2LocalRectFloat(const LonLatRect& r) const override;

private:
    core::HashValue hash_;
    RectI localRect_;
    PointI localSize_;
    RectI rectInMapPixel_;
    DoubleRect rectInMapPixelFloat_;
    DoublePoint mapPixelCenter_;
    ProjectionPtr projection_;
    double scale_;
    DoublePoint mapPixelAtLocalZero_;
};

// i_LocalCoordConverterFactory.pas (3 methods) + i_LocalCoordConverterFactorySimpe.pas (10
// methods, minus the legacy unit-name typo) merged into one factory (docs/specs/geo.md §2.7.1).
class LocalCoordConverterFactory {
public:
    explicit LocalCoordConverterFactory(HashSeed hash);

    [[nodiscard]] LocalCoordConverterPtr
    createNoScaleIntDelta(const RectI& localRect, ProjectionPtr projection, PointI mapPixelAtLocalZero) const;
    [[nodiscard]] LocalCoordConverterPtr createNoScale(const RectI& localRect, ProjectionPtr projection,
                                                       DoublePoint mapPixelAtLocalZero) const;
    [[nodiscard]] LocalCoordConverterPtr createScaled(const RectI& localRect, ProjectionPtr projection,
                                                      double mapScale, DoublePoint mapPixelAtLocalZero) const;

    [[nodiscard]] LocalCoordConverterPtr createForTile(ProjectionPtr projection, PointI tile) const;

    // Spec §2.7.1 types `src` as `const ILocalCoordConverter&`, mirroring the legacy
    // `const ASource: ILocalCoordConverter` parameter. Legacy's "no change needed" paths
    // (`Result := ASource; Exit;`) return the SAME refcounted interface, which Delphi extends for
    // free; a C++ reference cannot be turned back into a shared_ptr without either UB or a
    // non-owning alias that dangles once the caller's own shared_ptr is dropped. `src` is
    // therefore taken by `const LocalCoordConverterPtr&` here -- a disclosed, safety-motivated
    // deviation from the spec's literal signature (see docs/handoffs/T-0016.md); the behavior is
    // unchanged, only the ownership is made explicit.
    [[nodiscard]] LocalCoordConverterPtr changeCenterLonLat(const LocalCoordConverterPtr& src,
                                                            LonLat c) const;
    [[nodiscard]] LocalCoordConverterPtr changeByLocalDelta(const LocalCoordConverterPtr& src,
                                                            DoublePoint d) const;
    [[nodiscard]] LocalCoordConverterPtr changeCenterToLocalPoint(const LocalCoordConverterPtr& src,
                                                                  PointI p) const;
    [[nodiscard]] LocalCoordConverterPtr changeCenterLonLatAndProjection(const LocalCoordConverterPtr& src,
                                                                         ProjectionPtr projection,
                                                                         LonLat c) const;
    [[nodiscard]] LocalCoordConverterPtr
    changeProjectionWithFreezeAtVisualPoint(const LocalCoordConverterPtr& src, ProjectionPtr projection,
                                            PointI freezePoint) const;
    [[nodiscard]] LocalCoordConverterPtr changeProjectionWithFreezeAtCenter(const LocalCoordConverterPtr& src,
                                                                            ProjectionPtr projection) const;
    [[nodiscard]] LocalCoordConverterPtr changeProjectionWithScaleUpdate(const LocalCoordConverterPtr& src,
                                                                         ProjectionPtr projection) const;

private:
    // u_LocalCoordConverterFactorySimpe.pas:381-444 CreateConverter: picks the cheapest of the
    // three concrete shapes for the given (scale, mapPixelAtLocalZero) pair.
    [[nodiscard]] LocalCoordConverterPtr createConverter(const RectI& localRect, ProjectionPtr projection,
                                                         double mapScale,
                                                         DoublePoint mapPixelAtLocalZero) const;

    HashSeed hash_;
};

} // namespace cg::geo
