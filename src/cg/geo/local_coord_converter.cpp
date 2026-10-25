// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/local_coord_converter.h"

#include <QtGlobal>

#include <cmath>
#include <utility>

namespace cg::geo {
namespace {

[[nodiscard]] DoublePoint rectCenter(const RectI& r) noexcept {
    // u_GeoFunc.pas RectCenter(TRect)
    return {(static_cast<double>(r.left) + r.right) / 2.0, (static_cast<double>(r.top) + r.bottom) / 2.0};
}

// u_GeoFunc.pas DoublePointsEqual: NaN treated as "empty"; two empty points compare equal.
[[nodiscard]] bool doublePointsEqual(DoublePoint a, DoublePoint b) noexcept {
    const bool aEmpty = std::isnan(a.x()) || std::isnan(a.y());
    const bool bEmpty = std::isnan(b.x()) || std::isnan(b.y());
    if (aEmpty && bEmpty)
        return true;
    if (aEmpty || bEmpty)
        return false;
    return a.x() == b.x() && a.y() == b.y();
}

// u_LocalCoordConverterFactorySimpe.pas: every changeProjection* method that must reproject one
// map-pixel point from the old projection to the new one repeats this same-type/relative shortcut
// vs. general lon/lat round-trip choice (T-0016 review finding 14).
[[nodiscard]] DoublePoint reprojectMapPixel(const ProjectionPtr& from, const ProjectionPtr& to,
                                            DoublePoint p) {
    if (from->projectionType()->isSame(*to->projectionType())) {
        return to->relative2PixelPosFloat(from->pixelPosFloat2Relative(p));
    }
    LonLat lonLat = from->pixelPosFloat2LonLat(p);
    to->projectionType()->validateLonLatPos(lonLat);
    return to->lonLat2PixelPosFloat(lonLat);
}

} // namespace

LocalCoordConverter::LocalCoordConverter(core::HashValue hash, RectI localRect, RectI rectInMapPixel,
                                         DoubleRect rectInMapPixelFloat, DoublePoint mapPixelCenter,
                                         ProjectionPtr projection, double scale,
                                         DoublePoint mapPixelAtLocalZero)
    : hash_(hash), localRect_(localRect), localSize_({localRect.width(), localRect.height()}),
      rectInMapPixel_(rectInMapPixel), rectInMapPixelFloat_(rectInMapPixelFloat),
      mapPixelCenter_(mapPixelCenter), projection_(std::move(projection)), scale_(scale),
      mapPixelAtLocalZero_(mapPixelAtLocalZero) {
    // u_LocalCoordConverter.pas:202-235's constructor asserts (programmer errors, not user input).
    Q_ASSERT(projection_ != nullptr);
    Q_ASSERT(localRect_.left <= localRect_.right && localRect_.top <= localRect_.bottom);
    Q_ASSERT(rectInMapPixel_.left <= rectInMapPixel_.right && rectInMapPixel_.top <= rectInMapPixel_.bottom);
    Q_ASSERT(rectInMapPixelFloat_.left <= rectInMapPixelFloat_.right &&
             rectInMapPixelFloat_.top <= rectInMapPixelFloat_.bottom);
}

bool LocalCoordConverter::isSameConverter(const ILocalCoordConverter& other) const {
    // u_LocalCoordConverter.pas:252-270,443-458 (three legacy overrides merged: they differ only
    // in float-vs-int comparison at scale 1, which the unified class does not need to distinguish).
    if (this == &other)
        return true;
    if (hash_ != core::kNoHash && other.hash() != core::kNoHash && hash_ != other.hash())
        return false;
    if (!projection_->isSame(*other.projection()))
        return false;
    if (!(localRect_ == other.localRect()))
        return false;
    if (std::abs(scale_ - other.scale()) >= 0.001)
        return false;
    return doublePointsEqual(mapPixelAtLocalZero_, other.localPixel2MapPixelFloat({0, 0}));
}

PointI LocalCoordConverter::localPixel2MapPixel(PointI p, PointRounding r) const {
    return roundPoint(localPixel2MapPixelFloat(p), r);
}
DoublePoint LocalCoordConverter::localPixel2MapPixelFloat(PointI p) const {
    return localPixelFloat2MapPixelFloat(DoublePoint(p));
}
DoublePoint LocalCoordConverter::localPixelFloat2MapPixelFloat(DoublePoint p) const {
    // u_LocalCoordConverter.pas:486-491 / 584-589 / 678-683 -- the one formula (scale_ == 1.0 is
    // the fast path: division by 1.0 is exact, so no branch is needed for correctness, only for
    // clarity per docs/specs/geo.md §2.7).
    if (scale_ == 1.0) {
        return {p.x() + mapPixelAtLocalZero_.x(), p.y() + mapPixelAtLocalZero_.y()};
    }
    return {p.x() / scale_ + mapPixelAtLocalZero_.x(), p.y() / scale_ + mapPixelAtLocalZero_.y()};
}
PointI LocalCoordConverter::mapPixel2LocalPixel(PointI p, PointRounding r) const {
    return roundPoint(mapPixel2LocalPixelFloat(p), r);
}
DoublePoint LocalCoordConverter::mapPixel2LocalPixelFloat(PointI p) const {
    return mapPixelFloat2LocalPixelFloat(DoublePoint(p));
}
DoublePoint LocalCoordConverter::mapPixelFloat2LocalPixelFloat(DoublePoint p) const {
    if (scale_ == 1.0) {
        return {p.x() - mapPixelAtLocalZero_.x(), p.y() - mapPixelAtLocalZero_.y()};
    }
    return {(p.x() - mapPixelAtLocalZero_.x()) * scale_, (p.y() - mapPixelAtLocalZero_.y()) * scale_};
}

namespace {
// u_LocalCoordConverter.pas:297-327/372-402: TRectRounding -> the (topLeft, bottomRight) pair of
// TPointRounding it expands to.
void roundingPair(RectRounding m, PointRounding& topLeft, PointRounding& bottomRight) noexcept {
    switch (m) {
    case RectRounding::Closest:
        topLeft = bottomRight = PointRounding::Closest;
        break;
    case RectRounding::Outside:
        topLeft = PointRounding::ToTopLeft;
        bottomRight = PointRounding::ToBottomRight;
        break;
    case RectRounding::Inside:
        topLeft = PointRounding::ToBottomRight;
        bottomRight = PointRounding::ToTopLeft;
        break;
    case RectRounding::ToTopLeft:
        topLeft = bottomRight = PointRounding::ToTopLeft;
        break;
    }
}
} // namespace

RectI LocalCoordConverter::localRect2MapRect(const RectI& r, RectRounding m) const {
    // u_LocalCoordConverter.pas:304-305: locals are pre-seeded to prToTopLeft before the case.
    PointRounding tl = PointRounding::ToTopLeft, br = PointRounding::ToTopLeft;
    roundingPair(m, tl, br);
    const PointI mappedTl = localPixel2MapPixel(r.topLeft(), tl);
    const PointI mappedBr = localPixel2MapPixel(r.bottomRight(), br);
    return {mappedTl.x(), mappedTl.y(), mappedBr.x(), mappedBr.y()};
}
DoubleRect LocalCoordConverter::localRect2MapRectFloat(const RectI& r) const {
    const DoublePoint tl = localPixel2MapPixelFloat(r.topLeft());
    const DoublePoint br = localPixel2MapPixelFloat(r.bottomRight());
    return {tl.x(), tl.y(), br.x(), br.y()};
}
DoubleRect LocalCoordConverter::localRectFloat2MapRectFloat(const DoubleRect& r) const {
    const DoublePoint tl = localPixelFloat2MapPixelFloat(r.topLeft());
    const DoublePoint br = localPixelFloat2MapPixelFloat(r.bottomRight());
    return {tl.x(), tl.y(), br.x(), br.y()};
}
RectI LocalCoordConverter::mapRect2LocalRect(const RectI& r, RectRounding m) const {
    // u_LocalCoordConverter.pas:304-305: locals are pre-seeded to prToTopLeft before the case.
    PointRounding tl = PointRounding::ToTopLeft, br = PointRounding::ToTopLeft;
    roundingPair(m, tl, br);
    const PointI mappedTl = mapPixel2LocalPixel(r.topLeft(), tl);
    const PointI mappedBr = mapPixel2LocalPixel(r.bottomRight(), br);
    return {mappedTl.x(), mappedTl.y(), mappedBr.x(), mappedBr.y()};
}
DoubleRect LocalCoordConverter::mapRect2LocalRectFloat(const RectI& r) const {
    const DoublePoint tl = mapPixel2LocalPixelFloat(r.topLeft());
    const DoublePoint br = mapPixel2LocalPixelFloat(r.bottomRight());
    return {tl.x(), tl.y(), br.x(), br.y()};
}
DoubleRect LocalCoordConverter::mapRectFloat2LocalRectFloat(const DoubleRect& r) const {
    const DoublePoint tl = mapPixelFloat2LocalPixelFloat(r.topLeft());
    const DoublePoint br = mapPixelFloat2LocalPixelFloat(r.bottomRight());
    return {tl.x(), tl.y(), br.x(), br.y()};
}

PointI LocalCoordConverter::lonLat2LocalPixel(LonLat p, PointRounding r) const {
    return roundPoint(lonLat2LocalPixelFloat(p), r);
}
DoublePoint LocalCoordConverter::lonLat2LocalPixelFloat(LonLat p) const {
    return mapPixelFloat2LocalPixelFloat(projection_->lonLat2PixelPosFloat(p));
}
DoubleRect LocalCoordConverter::lonLatRect2LocalRectFloat(const LonLatRect& r) const {
    return mapRectFloat2LocalRectFloat(projection_->lonLatRect2PixelRectFloat(r));
}

// ============================================================================================
// LocalCoordConverterFactory
// ============================================================================================

LocalCoordConverterFactory::LocalCoordConverterFactory(HashSeed hash) : hash_(hash) {}

LocalCoordConverterPtr LocalCoordConverterFactory::createNoScale(const RectI& localRect,
                                                                 ProjectionPtr projection,
                                                                 DoublePoint mapPixelAtLocalZero) const {
    // u_LocalCoordConverterFactory.pas:172-206
    const DoublePoint localCenter = rectCenter(localRect);
    DoublePoint mapPixelCenter{localCenter.x() + mapPixelAtLocalZero.x(),
                               localCenter.y() + mapPixelAtLocalZero.y()};
    projection->validatePixelPosFloatStrict(mapPixelCenter, false);

    DoubleRect rectInMapPixelFloat{
        localRect.left + mapPixelAtLocalZero.x(), localRect.top + mapPixelAtLocalZero.y(),
        localRect.right + mapPixelAtLocalZero.x(), localRect.bottom + mapPixelAtLocalZero.y()};
    projection->validatePixelRectFloat(rectInMapPixelFloat);

    RectI rectInMapPixel = roundRect(rectInMapPixelFloat, RectRounding::Closest);
    projection->validatePixelRect(rectInMapPixel);

    const core::HashValue hash = hash_(1, localRect, projection, mapPixelAtLocalZero);
    return std::make_shared<const LocalCoordConverter>(hash, localRect, rectInMapPixel, rectInMapPixelFloat,
                                                       mapPixelCenter, std::move(projection), 1.0,
                                                       mapPixelAtLocalZero);
}

LocalCoordConverterPtr LocalCoordConverterFactory::createNoScaleIntDelta(const RectI& localRect,
                                                                         ProjectionPtr projection,
                                                                         PointI mapPixelAtLocalZero) const {
    // u_LocalCoordConverterFactory.pas:208-244
    Q_ASSERT(localRect.left <= localRect.right && localRect.top <= localRect.bottom);
    const DoublePoint localCenter = rectCenter(localRect);
    DoublePoint mapPixelCenter{localCenter.x() + mapPixelAtLocalZero.x(),
                               localCenter.y() + mapPixelAtLocalZero.y()};
    projection->validatePixelPosFloatStrict(mapPixelCenter, false);

    RectI rectInMapPixel{localRect.left + mapPixelAtLocalZero.x(), localRect.top + mapPixelAtLocalZero.y(),
                         localRect.right + mapPixelAtLocalZero.x(),
                         localRect.bottom + mapPixelAtLocalZero.y()};
    projection->validatePixelRect(rectInMapPixel);

    const DoubleRect rectInMapPixelFloat =
        toDoubleRect(rectInMapPixel); // derived FROM the int rect, not re-validated

    const core::HashValue hash = hash_(2, localRect, projection, mapPixelAtLocalZero);
    return std::make_shared<const LocalCoordConverter>(hash, localRect, rectInMapPixel, rectInMapPixelFloat,
                                                       mapPixelCenter, std::move(projection), 1.0,
                                                       DoublePoint(mapPixelAtLocalZero));
}

LocalCoordConverterPtr LocalCoordConverterFactory::createScaled(const RectI& localRect,
                                                                ProjectionPtr projection, double mapScale,
                                                                DoublePoint mapPixelAtLocalZero) const {
    // u_LocalCoordConverterFactory.pas:246-284
    const DoublePoint localCenter = rectCenter(localRect);
    DoublePoint mapPixelCenter{localCenter.x() / mapScale + mapPixelAtLocalZero.x(),
                               localCenter.y() / mapScale + mapPixelAtLocalZero.y()};
    projection->validatePixelPosFloatStrict(mapPixelCenter, false);

    DoubleRect rectInMapPixelFloat{localRect.left / mapScale + mapPixelAtLocalZero.x(),
                                   localRect.top / mapScale + mapPixelAtLocalZero.y(),
                                   localRect.right / mapScale + mapPixelAtLocalZero.x(),
                                   localRect.bottom / mapScale + mapPixelAtLocalZero.y()};
    projection->validatePixelRectFloat(rectInMapPixelFloat);

    RectI rectInMapPixel = roundRect(rectInMapPixelFloat, RectRounding::Closest);
    projection->validatePixelRect(rectInMapPixel);

    const core::HashValue hash = hash_(3, localRect, projection, mapScale, mapPixelAtLocalZero);
    return std::make_shared<const LocalCoordConverter>(hash, localRect, rectInMapPixel, rectInMapPixelFloat,
                                                       mapPixelCenter, std::move(projection), mapScale,
                                                       mapPixelAtLocalZero);
}

LocalCoordConverterPtr LocalCoordConverterFactory::createConverter(const RectI& localRect,
                                                                   ProjectionPtr projection, double mapScale,
                                                                   DoublePoint mapPixelAtLocalZero) const {
    // u_LocalCoordConverterFactorySimpe.pas:381-444: pick the cheapest representation that is
    // exact for the requested (scale, zero) pair.
    const DoublePoint localCenter = rectCenter(localRect);
    if (std::abs(mapScale - 1.0) < 0.001) {
        const PointI topLeftAsInt = roundPoint(mapPixelAtLocalZero, PointRounding::Closest);
        if (std::abs(mapPixelAtLocalZero.x() - topLeftAsInt.x()) < 0.001 &&
            std::abs(mapPixelAtLocalZero.y() - topLeftAsInt.y()) < 0.001) {
            DoublePoint centerFloat{mapPixelAtLocalZero.x() + localCenter.x(),
                                    mapPixelAtLocalZero.y() + localCenter.y()};
            const PointI centerInt = roundPoint(centerFloat, PointRounding::Closest);
            PointI centerValidated = centerInt;
            projection->validatePixelPosStrict(centerValidated, false);
            const DoublePoint topLeftFloat{static_cast<double>(centerValidated.x()) - localCenter.x(),
                                           static_cast<double>(centerValidated.y()) - localCenter.y()};
            return createNoScaleIntDelta(localRect, std::move(projection),
                                         roundPoint(topLeftFloat, PointRounding::Closest));
        }
        DoublePoint centerFloat{mapPixelAtLocalZero.x() + localCenter.x(),
                                mapPixelAtLocalZero.y() + localCenter.y()};
        projection->validatePixelPosFloatStrict(centerFloat, false);
        const DoublePoint topLeftFloat{centerFloat.x() - localCenter.x(), centerFloat.y() - localCenter.y()};
        return createNoScale(localRect, std::move(projection), topLeftFloat);
    }
    const DoublePoint scaledCenter{localCenter.x() / mapScale, localCenter.y() / mapScale};
    DoublePoint centerFloat{mapPixelAtLocalZero.x() + scaledCenter.x(),
                            mapPixelAtLocalZero.y() + scaledCenter.y()};
    projection->validatePixelPosFloatStrict(centerFloat, false);
    const DoublePoint topLeftFloat{centerFloat.x() - scaledCenter.x(), centerFloat.y() - scaledCenter.y()};
    return createScaled(localRect, std::move(projection), mapScale, topLeftFloat);
}

LocalCoordConverterPtr LocalCoordConverterFactory::createForTile(ProjectionPtr projection,
                                                                 PointI tile) const {
    // u_LocalCoordConverterFactorySimpe.pas:459-477
    const RectI pixelRect = projection->tilePos2PixelRect(tile);
    const RectI bitmapTileRect{0, 0, pixelRect.width(), pixelRect.height()};
    return createNoScaleIntDelta(bitmapTileRect, std::move(projection), pixelRect.topLeft());
}

LocalCoordConverterPtr LocalCoordConverterFactory::changeCenterLonLat(const LocalCoordConverterPtr& src,
                                                                      LonLat c) const {
    // u_LocalCoordConverterFactorySimpe.pas:130-166
    const ProjectionPtr projection = src->projection();
    LonLat validated = c;
    projection->projectionType()->validateLonLatPos(validated);
    const DoublePoint centerOld = src->centerMapPixelFloat();
    const DoublePoint centerNew = projection->lonLat2PixelPosFloat(validated);
    if (std::abs(centerOld.x() - centerNew.x()) < 0.001 && std::abs(centerOld.y() - centerNew.y()) < 0.001) {
        return src;
    }
    const RectI localRect = src->localRect();
    const double scale = src->scale();
    const DoublePoint localCenter = rectCenter(localRect);
    const DoublePoint topLeft{centerNew.x() - localCenter.x() / scale,
                              centerNew.y() - localCenter.y() / scale};
    return createConverter(localRect, projection, scale, topLeft);
}

LocalCoordConverterPtr LocalCoordConverterFactory::changeByLocalDelta(const LocalCoordConverterPtr& src,
                                                                      DoublePoint d) const {
    // u_LocalCoordConverterFactorySimpe.pas:109-128
    const RectI localRect = src->localRect();
    const double scale = src->scale();
    const DoublePoint topLeft =
        src->localPixelFloat2MapPixelFloat({localRect.left + d.x(), localRect.top + d.y()});
    return createConverter(localRect, src->projection(), scale, topLeft);
}

LocalCoordConverterPtr LocalCoordConverterFactory::changeCenterToLocalPoint(const LocalCoordConverterPtr& src,
                                                                            PointI p) const {
    // u_LocalCoordConverterFactorySimpe.pas:205-239
    const ProjectionPtr projection = src->projection();
    const DoublePoint centerOld = src->centerMapPixelFloat();
    DoublePoint centerNew = src->localPixel2MapPixelFloat(p);
    projection->validatePixelPosFloatStrict(centerNew, true);
    if (std::abs(centerOld.x() - centerNew.x()) < 0.001 && std::abs(centerOld.y() - centerNew.y()) < 0.001) {
        return src;
    }
    const RectI localRect = src->localRect();
    const double scale = src->scale();
    const DoublePoint localCenter = rectCenter(localRect);
    const DoublePoint topLeft{centerNew.x() - localCenter.x() / scale,
                              centerNew.y() - localCenter.y() / scale};
    return createConverter(localRect, projection, scale, topLeft);
}

LocalCoordConverterPtr
LocalCoordConverterFactory::changeCenterLonLatAndProjection(const LocalCoordConverterPtr& src,
                                                            ProjectionPtr projection, LonLat c) const {
    // u_LocalCoordConverterFactorySimpe.pas:168-203
    const ProjectionPtr oldProjection = src->projection();
    if (oldProjection->isSame(*projection)) {
        return changeCenterLonLat(src, c);
    }
    LonLat validated = c;
    projection->projectionType()->validateLonLatPos(validated);
    const DoublePoint centerNew = projection->lonLat2PixelPosFloat(validated);
    const RectI localRect = src->localRect();
    const double scale = src->scale();
    const DoublePoint localCenter = rectCenter(localRect);
    const DoublePoint topLeft{centerNew.x() - localCenter.x() / scale,
                              centerNew.y() - localCenter.y() / scale};
    return createConverter(localRect, std::move(projection), scale, topLeft);
}

LocalCoordConverterPtr
LocalCoordConverterFactory::changeProjectionWithFreezeAtCenter(const LocalCoordConverterPtr& src,
                                                               ProjectionPtr projection) const {
    // u_LocalCoordConverterFactorySimpe.pas:241-280
    const ProjectionPtr oldProjection = src->projection();
    if (oldProjection->isSame(*projection)) {
        return src;
    }
    const DoublePoint centerNew = reprojectMapPixel(oldProjection, projection, src->centerMapPixelFloat());
    const RectI localRect = src->localRect();
    const double scale = src->scale();
    const DoublePoint topLeft{centerNew.x() - (localRect.right - localRect.left) / 2.0 / scale,
                              centerNew.y() - (localRect.bottom - localRect.top) / 2.0 / scale};
    return createConverter(localRect, std::move(projection), scale, topLeft);
}

LocalCoordConverterPtr LocalCoordConverterFactory::changeProjectionWithFreezeAtVisualPoint(
    const LocalCoordConverterPtr& src, ProjectionPtr projection, PointI freezePoint) const {
    // u_LocalCoordConverterFactorySimpe.pas:282-330
    const ProjectionPtr oldProjection = src->projection();
    if (oldProjection->isSame(*projection)) {
        return src;
    }
    DoublePoint freezeMapPoint = src->localPixel2MapPixelFloat(freezePoint);
    oldProjection->validatePixelPosFloatStrict(freezeMapPoint, false);
    const DoublePoint freezeLocalPoint = src->mapPixelFloat2LocalPixelFloat(freezeMapPoint);
    const DoublePoint mapFreezePointAtNew = reprojectMapPixel(oldProjection, projection, freezeMapPoint);

    const RectI localRect = src->localRect();
    const double scale = src->scale();
    const DoublePoint topLeft{mapFreezePointAtNew.x() - freezeLocalPoint.x() / scale,
                              mapFreezePointAtNew.y() - freezeLocalPoint.y() / scale};
    return createConverter(localRect, std::move(projection), scale, topLeft);
}

LocalCoordConverterPtr
LocalCoordConverterFactory::changeProjectionWithScaleUpdate(const LocalCoordConverterPtr& src,
                                                            ProjectionPtr projection) const {
    // u_LocalCoordConverterFactorySimpe.pas:332-379
    const ProjectionPtr oldProjection = src->projection();
    if (oldProjection->isSame(*projection)) {
        return src;
    }
    const DoublePoint centerNew = reprojectMapPixel(oldProjection, projection, src->centerMapPixelFloat());
    const RectI localRect = src->localRect();
    const DoublePoint freezePoint = rectCenter(localRect);
    const double scaleOld = src->scale();
    const double scaleNew = scaleOld * (oldProjection->pixelsFloat() / projection->pixelsFloat());
    const DoublePoint topLeft{centerNew.x() - freezePoint.x() / scaleNew,
                              centerNew.y() - freezePoint.y() / scaleNew};
    return createConverter(localRect, std::move(projection), scaleNew, topLeft);
}

} // namespace cg::geo
