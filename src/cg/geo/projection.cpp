// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/projection.h"

#include "cg/geo/log_geo.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace cg::geo {
namespace {

constexpr int kMaxInt = std::numeric_limits<int>::max();

// The exact two's-complement wraparound a 32-bit Delphi Integer silently produces on overflow
// (range checking off, the shipping default) -- computed explicitly so the port never relies on
// C++ signed-overflow UB to reproduce it. Well-defined since C++20: converting an out-of-range
// unsigned value to a signed type yields the value congruent mod 2^32 ([conv.integral]).
[[nodiscard]] int wrapInt32(qint64 v) noexcept {
    return static_cast<int>(static_cast<quint32>(static_cast<quint64>(v)));
}

} // namespace

ProjectionBasic256x256::ProjectionBasic256x256(core::HashValue hash, ProjectionTypePtr projectionType,
                                               quint8 zoom)
    : hash_(hash), projectionType_(std::move(projectionType)), zoom_(zoom) {
    Q_ASSERT(projectionType_ != nullptr);
    Q_ASSERT(zoom_ < 24);
    // u_ProjectionBasic256x256.pas:343-365
    tiles_ = 1 << zoom_;
    pixels_ = (zoom_ < 23) ? (1 << zoom_) * 256 : kMaxInt;
    tilesFloat_ = static_cast<double>(tiles_);
    pixelsFloat_ = tilesFloat_ * 256.0; // NOT double(pixels_): at zoom 23 they differ
    tileSplitCode_ = kTileSplitQuadrate256x256;
}

bool ProjectionBasic256x256::isSame(const IProjection& other) const {
    // u_ProjectionBasic256x256.pas:1149-1174
    if (this == &other)
        return true;
    if (hash_ != core::kNoHash && other.hash() != core::kNoHash && hash_ != other.hash())
        return false;
    if (tileSplitCode_ != other.tileSplitCode())
        return false;
    if (zoom_ != other.zoom())
        return false;
    return projectionType_->isSame(*other.projectionType());
}

// --- "Internal" clamp family: silent, logged, never wraps -------------------------------------

void ProjectionBasic256x256::internalValidatePixelPos(PointI& p) const {
    if (p.x() < 0) {
        qCDebug(logGeo, "pixel X below 0, clamped");
        p.setX(0);
    } else if (p.x() > pixels_) {
        qCDebug(logGeo, "pixel X above %d, clamped", pixels_);
        p.setX(pixels_);
    }
    if (p.y() < 0) {
        qCDebug(logGeo, "pixel Y below 0, clamped");
        p.setY(0);
    } else if (p.y() > pixels_) {
        qCDebug(logGeo, "pixel Y above %d, clamped", pixels_);
        p.setY(pixels_);
    }
}

void ProjectionBasic256x256::internalValidatePixelPosFloat(DoublePoint& p) const {
    if (p.x() < 0) {
        qCDebug(logGeo, "pixel X below 0, clamped");
        p.setX(0.0);
    } else if (p.x() > pixelsFloat_) {
        qCDebug(logGeo, "pixel X above %f, clamped", pixelsFloat_);
        p.setX(pixelsFloat_);
    }
    if (p.y() < 0) {
        qCDebug(logGeo, "pixel Y below 0, clamped");
        p.setY(0.0);
    } else if (p.y() > pixelsFloat_) {
        qCDebug(logGeo, "pixel Y above %f, clamped", pixelsFloat_);
        p.setY(pixelsFloat_);
    }
}

namespace {
// One clamp rule (never wraps, silently saturates at each edge/axis) applied to all four rect
// edges instead of transcribed once per edge (T-0016 review finding 14: "the clamp is one rule
// applied eight times").
template <class Rect, class T> void clampRectEdges(Rect& r, T lo, T hi) noexcept {
    r.left = std::clamp(r.left, lo, hi);
    r.top = std::clamp(r.top, lo, hi);
    r.right = std::clamp(r.right, lo, hi);
    r.bottom = std::clamp(r.bottom, lo, hi);
}
} // namespace

void ProjectionBasic256x256::internalValidatePixelRect(RectI& r) const {
    clampRectEdges(r, 0, pixels_);
}

void ProjectionBasic256x256::internalValidatePixelRectFloat(DoubleRect& r) const {
    clampRectEdges(r, 0.0, pixelsFloat_);
}

void ProjectionBasic256x256::internalValidateTilePos(PointI& p) const {
    p.setX(std::clamp(p.x(), 0, tiles_));
    p.setY(std::clamp(p.y(), 0, tiles_));
}

void ProjectionBasic256x256::internalValidateTilePosStrict(PointI& p) const {
    p.setX(std::clamp(p.x(), 0, tiles_ - 1));
    p.setY(std::clamp(p.y(), 0, tiles_ - 1));
}

void ProjectionBasic256x256::internalValidateTilePosFloat(DoublePoint& p) const {
    p.setX(std::clamp(p.x(), 0.0, tilesFloat_));
    p.setY(std::clamp(p.y(), 0.0, tilesFloat_));
}

void ProjectionBasic256x256::internalValidateTileRect(RectI& r) const {
    clampRectEdges(r, 0, tiles_);
}

void ProjectionBasic256x256::internalValidateTileRectFloat(DoubleRect& r) const {
    clampRectEdges(r, 0.0, tilesFloat_);
}

void ProjectionBasic256x256::internalValidateRelativePos(DoublePoint& p) const {
    p.setX(std::clamp(p.x(), 0.0, 1.0));
    p.setY(std::clamp(p.y(), 0.0, 1.0));
}

void ProjectionBasic256x256::internalValidateRelativeRect(DoubleRect& r) const {
    clampRectEdges(r, 0.0, 1.0);
}

// --- the six core primitives + the zoom-23 patch -----------------------------------------------

DoublePoint ProjectionBasic256x256::internalPixelPos2Relative(PointI p) const noexcept {
    return {p.x() / pixelsFloat_, p.y() / pixelsFloat_};
}
DoublePoint ProjectionBasic256x256::internalPixelPosFloat2Relative(DoublePoint p) const noexcept {
    return {p.x() / pixelsFloat_, p.y() / pixelsFloat_};
}
DoubleRect ProjectionBasic256x256::internalPixelRect2RelativeRect(const RectI& r) const noexcept {
    return {r.left / pixelsFloat_, r.top / pixelsFloat_, r.right / pixelsFloat_, r.bottom / pixelsFloat_};
}
DoubleRect ProjectionBasic256x256::internalPixelRectFloat2RelativeRect(const DoubleRect& r) const noexcept {
    return {r.left / pixelsFloat_, r.top / pixelsFloat_, r.right / pixelsFloat_, r.bottom / pixelsFloat_};
}

RectI ProjectionBasic256x256::internalTilePos2PixelRect(PointI t) const noexcept {
    // u_ProjectionBasic256x256.pas:766-782
    const int left = wrapInt32(static_cast<qint64>(t.x()) * 256);
    const int top = wrapInt32(static_cast<qint64>(t.y()) * 256);
    int right = wrapInt32(static_cast<qint64>(left) + 256);
    int bottom = wrapInt32(static_cast<qint64>(top) + 256);
    if (zoom_ == 23) {
        if (right < 0)
            right = kMaxInt;
        if (bottom < 0)
            bottom = kMaxInt;
    }
    return {left, top, right, bottom};
}

DoublePoint ProjectionBasic256x256::internalTilePos2Relative(PointI t) const noexcept {
    return {t.x() / tilesFloat_, t.y() / tilesFloat_};
}
DoublePoint ProjectionBasic256x256::internalTilePosFloat2Relative(DoublePoint t) const noexcept {
    return {t.x() / tilesFloat_, t.y() / tilesFloat_};
}
DoubleRect ProjectionBasic256x256::internalTileRect2RelativeRect(const RectI& r) const noexcept {
    return {r.left / tilesFloat_, r.top / tilesFloat_, r.right / tilesFloat_, r.bottom / tilesFloat_};
}
DoubleRect ProjectionBasic256x256::internalTileRectFloat2RelativeRect(const DoubleRect& r) const noexcept {
    return {r.left / tilesFloat_, r.top / tilesFloat_, r.right / tilesFloat_, r.bottom / tilesFloat_};
}

DoublePoint ProjectionBasic256x256::internalRelative2PixelPosFloat(DoublePoint p) const noexcept {
    return {p.x() * pixelsFloat_, p.y() * pixelsFloat_};
}
DoubleRect ProjectionBasic256x256::internalRelativeRect2PixelRectFloat(const DoubleRect& r) const noexcept {
    return {r.left * pixelsFloat_, r.top * pixelsFloat_, r.right * pixelsFloat_, r.bottom * pixelsFloat_};
}
DoublePoint ProjectionBasic256x256::internalRelative2TilePosFloat(DoublePoint p) const noexcept {
    return {p.x() * tilesFloat_, p.y() * tilesFloat_};
}
DoubleRect ProjectionBasic256x256::internalRelativeRect2TileRectFloat(const DoubleRect& r) const noexcept {
    return {r.left * tilesFloat_, r.top * tilesFloat_, r.right * tilesFloat_, r.bottom * tilesFloat_};
}

// --- pixel -> * ---------------------------------------------------------------------------------

DoublePoint ProjectionBasic256x256::pixelPos2Relative(PointI p) const {
    internalValidatePixelPos(p);
    return internalPixelPos2Relative(p);
}
LonLat ProjectionBasic256x256::pixelPos2LonLat(PointI p) const {
    internalValidatePixelPos(p);
    return projectionType_->relative2LonLat(internalPixelPos2Relative(p));
}
DoublePoint ProjectionBasic256x256::pixelPos2TilePosFloat(PointI p) const {
    // u_ProjectionBasic256x256.pas:1271-1276: NO clamping.
    return {p.x() / 256.0, p.y() / 256.0};
}
DoublePoint ProjectionBasic256x256::pixelPosFloat2Relative(DoublePoint p) const {
    internalValidatePixelPosFloat(p);
    return internalPixelPosFloat2Relative(p);
}
LonLat ProjectionBasic256x256::pixelPosFloat2LonLat(DoublePoint p) const {
    internalValidatePixelPosFloat(p);
    return projectionType_->relative2LonLat(internalPixelPosFloat2Relative(p));
}
DoublePoint ProjectionBasic256x256::pixelPosFloat2TilePosFloat(DoublePoint p) const {
    return {p.x() / 256.0, p.y() / 256.0}; // NO clamping, same as the integer overload
}

RectI ProjectionBasic256x256::pixelRect2TileRect(const RectI& r) const {
    RectI clamped = r;
    internalValidatePixelRect(clamped);
    // u_ProjectionBasic256x256.pas:1331-1341 ("shr 8"/"(x+255) shr 8"). Computed in qint64 so the
    // zoom-23 boundary (pixels_ == INT_MAX) never triggers signed overflow; the true (unwrapped)
    // value is what "shr 8" on the non-negative bit pattern already produces (docs/specs/geo.md
    // §2.5.2: the tile rect *covers* the pixel rect -- left/top round in, right/bottom round out).
    const qint64 left = static_cast<qint64>(clamped.left) / 256;
    const qint64 top = static_cast<qint64>(clamped.top) / 256;
    const qint64 right = (static_cast<qint64>(clamped.right) + 255) / 256;
    const qint64 bottom = (static_cast<qint64>(clamped.bottom) + 255) / 256;
    return {static_cast<int>(left), static_cast<int>(top), static_cast<int>(right), static_cast<int>(bottom)};
}
DoubleRect ProjectionBasic256x256::pixelRect2RelativeRect(const RectI& r) const {
    RectI clamped = r;
    internalValidatePixelRect(clamped);
    return internalPixelRect2RelativeRect(clamped);
}
LonLatRect ProjectionBasic256x256::pixelRect2LonLatRect(const RectI& r) const {
    RectI clamped = r;
    internalValidatePixelRect(clamped);
    return projectionType_->relativeRect2LonLatRect(internalPixelRect2RelativeRect(clamped));
}
DoubleRect ProjectionBasic256x256::pixelRect2TileRectFloat(const RectI& r) const {
    RectI clamped = r;
    internalValidatePixelRect(clamped);
    return {clamped.left / 256.0, clamped.top / 256.0, clamped.right / 256.0, clamped.bottom / 256.0};
}
DoubleRect ProjectionBasic256x256::pixelRectFloat2RelativeRect(const DoubleRect& r) const {
    DoubleRect clamped = r;
    internalValidatePixelRectFloat(clamped);
    return internalPixelRectFloat2RelativeRect(clamped);
}
LonLatRect ProjectionBasic256x256::pixelRectFloat2LonLatRect(const DoubleRect& r) const {
    DoubleRect clamped = r;
    internalValidatePixelRectFloat(clamped);
    return projectionType_->relativeRect2LonLatRect(internalPixelRectFloat2RelativeRect(clamped));
}
DoubleRect ProjectionBasic256x256::pixelRectFloat2TileRectFloat(const DoubleRect& r) const {
    DoubleRect clamped = r;
    internalValidatePixelRectFloat(clamped);
    return {clamped.left / 256.0, clamped.top / 256.0, clamped.right / 256.0, clamped.bottom / 256.0};
}

// --- tile -> * ----------------------------------------------------------------------------------

PointI ProjectionBasic256x256::tilePos2PixelPos(PointI t) const {
    internalValidateTilePos(t);
    return {t.x() * 256, t.y() * 256};
}
RectI ProjectionBasic256x256::tilePos2PixelRect(PointI t) const {
    internalValidateTilePosStrict(t);
    return internalTilePos2PixelRect(t);
}
DoubleRect ProjectionBasic256x256::tilePos2PixelRectFloat(PointI t) const {
    internalValidateTilePosStrict(t);
    return toDoubleRect(internalTilePos2PixelRect(t));
}
DoublePoint ProjectionBasic256x256::tilePos2Relative(PointI t) const {
    internalValidateTilePos(t);
    return internalTilePos2Relative(t);
}
DoubleRect ProjectionBasic256x256::tilePos2RelativeRect(PointI t) const {
    internalValidateTilePosStrict(t);
    return internalPixelRect2RelativeRect(internalTilePos2PixelRect(t));
}
LonLat ProjectionBasic256x256::tilePos2LonLat(PointI t) const {
    internalValidateTilePos(t);
    return projectionType_->relative2LonLat(internalTilePos2Relative(t));
}
LonLatRect ProjectionBasic256x256::tilePos2LonLatRect(PointI t) const {
    internalValidateTilePosStrict(t);
    const DoubleRect relative = internalPixelRect2RelativeRect(internalTilePos2PixelRect(t));
    return projectionType_->relativeRect2LonLatRect(relative);
}
DoublePoint ProjectionBasic256x256::tilePosFloat2PixelPosFloat(DoublePoint t) const {
    internalValidateTilePosFloat(t);
    return {t.x() * 256.0, t.y() * 256.0};
}
DoublePoint ProjectionBasic256x256::tilePosFloat2Relative(DoublePoint t) const {
    internalValidateTilePosFloat(t);
    return internalTilePosFloat2Relative(t);
}
LonLat ProjectionBasic256x256::tilePosFloat2LonLat(DoublePoint t) const {
    internalValidateTilePosFloat(t);
    return projectionType_->relative2LonLat(internalTilePosFloat2Relative(t));
}
RectI ProjectionBasic256x256::tileRect2PixelRect(const RectI& r) const {
    RectI clamped = r;
    internalValidateTileRect(clamped);
    // u_ProjectionBasic256x256.pas:1560-1578: Left/Top are NOT guarded, only Right/Bottom are --
    // reproduced exactly, including that asymmetry (docs/specs/geo.md §2.5.1).
    const int left = wrapInt32(static_cast<qint64>(clamped.left) * 256);
    const int top = wrapInt32(static_cast<qint64>(clamped.top) * 256);
    int right = wrapInt32(static_cast<qint64>(clamped.right) * 256);
    int bottom = wrapInt32(static_cast<qint64>(clamped.bottom) * 256);
    if (zoom_ == 23) {
        if (right < 0)
            right = kMaxInt;
        if (bottom < 0)
            bottom = kMaxInt;
    }
    return {left, top, right, bottom};
}
DoubleRect ProjectionBasic256x256::tileRect2RelativeRect(const RectI& r) const {
    RectI clamped = r;
    internalValidateTileRect(clamped);
    return internalTileRect2RelativeRect(clamped);
}
LonLatRect ProjectionBasic256x256::tileRect2LonLatRect(const RectI& r) const {
    RectI clamped = r;
    internalValidateTileRect(clamped);
    return projectionType_->relativeRect2LonLatRect(internalTileRect2RelativeRect(clamped));
}
DoubleRect ProjectionBasic256x256::tileRectFloat2PixelRectFloat(const DoubleRect& r) const {
    DoubleRect clamped = r;
    internalValidateTileRectFloat(clamped);
    return {clamped.left * 256.0, clamped.top * 256.0, clamped.right * 256.0, clamped.bottom * 256.0};
}
DoubleRect ProjectionBasic256x256::tileRectFloat2RelativeRect(const DoubleRect& r) const {
    DoubleRect clamped = r;
    internalValidateTileRectFloat(clamped);
    return internalTileRectFloat2RelativeRect(clamped);
}
LonLatRect ProjectionBasic256x256::tileRectFloat2LonLatRect(const DoubleRect& r) const {
    DoubleRect clamped = r;
    internalValidateTileRectFloat(clamped);
    return projectionType_->relativeRect2LonLatRect(internalTileRectFloat2RelativeRect(clamped));
}

// --- relative -> * ---------------------------------------------------------------------------

DoublePoint ProjectionBasic256x256::relative2PixelPosFloat(DoublePoint p) const {
    internalValidateRelativePos(p);
    return internalRelative2PixelPosFloat(p);
}
DoublePoint ProjectionBasic256x256::relative2TilePosFloat(DoublePoint p) const {
    internalValidateRelativePos(p);
    return internalRelative2TilePosFloat(p);
}
DoubleRect ProjectionBasic256x256::relativeRect2PixelRectFloat(const DoubleRect& r) const {
    DoubleRect clamped = r;
    internalValidateRelativeRect(clamped);
    return internalRelativeRect2PixelRectFloat(clamped);
}
DoubleRect ProjectionBasic256x256::relativeRect2TileRectFloat(const DoubleRect& r) const {
    DoubleRect clamped = r;
    internalValidateRelativeRect(clamped);
    return internalRelativeRect2TileRectFloat(clamped);
}

// --- lon/lat -> * ------------------------------------------------------------------------------

DoublePoint ProjectionBasic256x256::lonLat2PixelPosFloat(LonLat p) const {
    // u_ProjectionBasic256x256.pas:1211-1219: no clamp here, lonLat2Relative clamps internally.
    return internalRelative2PixelPosFloat(projectionType_->lonLat2Relative(p));
}
DoublePoint ProjectionBasic256x256::lonLat2TilePosFloat(LonLat p) const {
    return internalRelative2TilePosFloat(projectionType_->lonLat2Relative(p));
}
DoubleRect ProjectionBasic256x256::lonLatRect2PixelRectFloat(const LonLatRect& r) const {
    // u_ProjectionBasic256x256.pas:1231-1239: goes through the PUBLIC (re-clamping)
    // relativeRect2PixelRectFloat -- unlike the tile-rect overload below. Reproduced exactly.
    return relativeRect2PixelRectFloat(projectionType_->lonLatRect2RelativeRect(r));
}
DoubleRect ProjectionBasic256x256::lonLatRect2TileRectFloat(const LonLatRect& r) const {
    // u_ProjectionBasic256x256.pas:1241-1249: the INTERNAL (non-re-clamping) variant.
    return internalRelativeRect2TileRectFloat(projectionType_->lonLatRect2RelativeRect(r));
}

// --- clamp in place (public Validate*) --------------------------------------------------------

void ProjectionBasic256x256::validateTilePos(PointI& p, bool cycleMap) const {
    // u_ProjectionBasic256x256.pas:1815-1843
    if (p.x() < 0) {
        p.setX(cycleMap ? p.x() % tiles_ + tiles_ : 0);
    } else if (p.x() > tiles_) {
        p.setX(cycleMap ? p.x() % tiles_ : tiles_);
    }
    if (p.y() < 0) {
        p.setY(0);
    } else if (p.y() > tiles_) {
        p.setY(tiles_);
    }
}
void ProjectionBasic256x256::validateTilePosStrict(PointI& p, bool cycleMap) const {
    // u_ProjectionBasic256x256.pas:1845-1873
    if (p.x() < 0) {
        p.setX(cycleMap ? p.x() % tiles_ + tiles_ : 0);
    } else if (p.x() >= tiles_) {
        p.setX(cycleMap ? p.x() % tiles_ : tiles_ - 1);
    }
    if (p.y() < 0) {
        p.setY(0);
    } else if (p.y() >= tiles_) {
        p.setY(tiles_ - 1);
    }
}
void ProjectionBasic256x256::validateTileRect(RectI& r) const {
    // u_ProjectionBasic256x256.pas:1875-1908: clamps all four edges, never wraps.
    clampRectEdges(r, 0, tiles_);
}

void ProjectionBasic256x256::validatePixelPos(PointI& p, bool cycleMap) const {
    // u_ProjectionBasic256x256.pas:1627-1653.
    // *** Legacy bug D-1, reproduced on purpose (docs/specs/geo.md §2.5.3, §10 D-1): the Y branch
    // is missing its "> pixels_" test, so EVERY non-negative Y is replaced by pixels_. Do not fix
    // this -- ValidatePixelPosReproducesLegacyYBug pins it, and the port must move the map under
    // the cursor by the same (wrong) amount the Windows build does. ***
    if (p.x() < 0) {
        p.setX(cycleMap ? p.x() % pixels_ + pixels_ : 0);
    } else if (p.x() > pixels_) {
        p.setX(cycleMap ? p.x() % pixels_ : pixels_);
    }
    if (p.y() < 0) {
        p.setY(0);
    } else {
        p.setY(pixels_);
    }
}
void ProjectionBasic256x256::validatePixelPosStrict(PointI& p, bool cycleMap) const {
    // u_ProjectionBasic256x256.pas:1715-1743: correct (no D-1 bug here).
    if (p.x() < 0) {
        p.setX(cycleMap ? p.x() % pixels_ + pixels_ : 0);
    } else if (p.x() >= pixels_) {
        p.setX(cycleMap ? p.x() % pixels_ : pixels_ - 1);
    }
    if (p.y() < 0) {
        p.setY(0);
    } else if (p.y() >= pixels_) {
        p.setY(pixels_ - 1);
    }
}
void ProjectionBasic256x256::validatePixelPosFloat(DoublePoint& p, bool cycleMap) const {
    // u_ProjectionBasic256x256.pas:1655-1683. Int() is truncation toward zero: std::trunc, never
    // std::floor and never std::fmod (docs/specs/geo.md §2.5.3).
    if (p.x() < 0) {
        p.setX(cycleMap ? p.x() - std::trunc(p.x() / pixelsFloat_) * pixelsFloat_ + pixelsFloat_ : 0.0);
    } else if (p.x() > pixelsFloat_) {
        p.setX(cycleMap ? p.x() - std::trunc(p.x() / pixelsFloat_) * pixelsFloat_ : pixelsFloat_);
    }
    if (p.y() < 0) {
        p.setY(0.0);
    } else if (p.y() > pixelsFloat_) {
        p.setY(pixelsFloat_);
    }
}
void ProjectionBasic256x256::validatePixelPosFloatStrict(DoublePoint& p, bool cycleMap) const {
    // u_ProjectionBasic256x256.pas:1685-1713: same wrap, `>=` at the upper bound.
    if (p.x() < 0) {
        p.setX(cycleMap ? p.x() - std::trunc(p.x() / pixelsFloat_) * pixelsFloat_ + pixelsFloat_ : 0.0);
    } else if (p.x() >= pixelsFloat_) {
        p.setX(cycleMap ? p.x() - std::trunc(p.x() / pixelsFloat_) * pixelsFloat_ : pixelsFloat_);
    }
    if (p.y() < 0) {
        p.setY(0.0);
    } else if (p.y() >= pixelsFloat_) {
        p.setY(pixelsFloat_);
    }
}
void ProjectionBasic256x256::validatePixelRect(RectI& r) const {
    clampRectEdges(r, 0, pixels_);
}
void ProjectionBasic256x256::validatePixelRectFloat(DoubleRect& r) const {
    clampRectEdges(r, 0.0, pixelsFloat_);
}

// --- test without clamping (public Check*) -----------------------------------------------------

namespace {
// One in-range rule (T-0016 review finding 14), applied per axis/edge instead of transcribed once
// per Check* method.
template <class T> [[nodiscard]] bool inRange(T v, T lo, T hi) noexcept {
    return v >= lo && v <= hi;
}
template <class T> [[nodiscard]] bool inRangeStrict(T v, T lo, T hi) noexcept {
    return v >= lo && v < hi;
}
template <class Rect, class T> [[nodiscard]] bool rectInRange(const Rect& r, T lo, T hi) noexcept {
    return inRange(r.left, lo, hi) && inRange(r.top, lo, hi) && inRange(r.right, lo, hi) &&
           inRange(r.bottom, lo, hi);
}
} // namespace

bool ProjectionBasic256x256::checkTilePos(PointI p) const {
    return inRange(p.x(), 0, tiles_) && inRange(p.y(), 0, tiles_);
}
bool ProjectionBasic256x256::checkTilePosStrict(PointI p) const {
    return inRangeStrict(p.x(), 0, tiles_) && inRangeStrict(p.y(), 0, tiles_);
}
bool ProjectionBasic256x256::checkTileRect(const RectI& r) const {
    return rectInRange(r, 0, tiles_);
}
bool ProjectionBasic256x256::checkPixelPos(PointI p) const {
    return inRange(p.x(), 0, pixels_) && inRange(p.y(), 0, pixels_);
}
bool ProjectionBasic256x256::checkPixelPosStrict(PointI p) const {
    return inRangeStrict(p.x(), 0, pixels_) && inRangeStrict(p.y(), 0, pixels_);
}
bool ProjectionBasic256x256::checkPixelPosFloat(DoublePoint p) const {
    return inRange(p.x(), 0.0, pixelsFloat_) && inRange(p.y(), 0.0, pixelsFloat_);
}
bool ProjectionBasic256x256::checkPixelPosFloatStrict(DoublePoint p) const {
    return inRangeStrict(p.x(), 0.0, pixelsFloat_) && inRangeStrict(p.y(), 0.0, pixelsFloat_);
}
bool ProjectionBasic256x256::checkPixelRect(const RectI& r) const {
    return rectInRange(r, 0, pixels_);
}
bool ProjectionBasic256x256::checkPixelRectFloat(const DoubleRect& r) const {
    return rectInRange(r, 0.0, pixelsFloat_);
}

} // namespace cg::geo
