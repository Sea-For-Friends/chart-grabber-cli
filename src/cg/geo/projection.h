// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_Projection.pas (all 60 members), u_ProjectionBasic256x256.pas (1,910 lines)
//
// One IProjection = one projection type at one zoom, over the legacy 256x256 tile pyramid
// (24 levels, 0..23). Every pixel/tile/relative/lon-lat conversion the app makes goes through
// this interface. Immutable snapshot, no locks (docs/specs/geo.md §8.1).

#include "cg/core/hash.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_type.h"

#include <QtGlobal>

#include <memory>

namespace cg::geo {

class IProjection {
public:
    virtual ~IProjection() = default;

    // --- identity -------------------------------------------------------------
    [[nodiscard]] virtual core::HashValue hash() const noexcept = 0;
    [[nodiscard]] virtual quint8 zoom() const noexcept = 0;
    [[nodiscard]] virtual const ProjectionTypePtr& projectionType() const noexcept = 0;
    [[nodiscard]] virtual bool isSame(const IProjection& other) const = 0;

    // --- extents ----------------------------------------------------------------
    [[nodiscard]] virtual RectI tileRect() const noexcept = 0;             // {0,0,1<<zoom,1<<zoom}
    [[nodiscard]] virtual RectI pixelRect() const noexcept = 0;            // {0,0,pixels,pixels}
    [[nodiscard]] virtual double pixelsFloat() const noexcept = 0;         // (1<<zoom) * 256.0
    [[nodiscard]] virtual int tileSplitCode() const noexcept = 0;          // always 1
    [[nodiscard]] virtual PointI tileSize(PointI tile) const noexcept = 0; // always {256,256}

    // --- pixel -> * ---------------------------------------------------------
    [[nodiscard]] virtual DoublePoint pixelPos2Relative(PointI p) const = 0;
    [[nodiscard]] virtual LonLat pixelPos2LonLat(PointI p) const = 0;
    [[nodiscard]] virtual DoublePoint pixelPos2TilePosFloat(PointI p) const = 0;
    [[nodiscard]] virtual DoublePoint pixelPosFloat2Relative(DoublePoint p) const = 0;
    [[nodiscard]] virtual LonLat pixelPosFloat2LonLat(DoublePoint p) const = 0;
    [[nodiscard]] virtual DoublePoint pixelPosFloat2TilePosFloat(DoublePoint p) const = 0;
    [[nodiscard]] virtual RectI pixelRect2TileRect(const RectI& r) const = 0;
    [[nodiscard]] virtual DoubleRect pixelRect2RelativeRect(const RectI& r) const = 0;
    [[nodiscard]] virtual LonLatRect pixelRect2LonLatRect(const RectI& r) const = 0;
    [[nodiscard]] virtual DoubleRect pixelRect2TileRectFloat(const RectI& r) const = 0;
    [[nodiscard]] virtual DoubleRect pixelRectFloat2RelativeRect(const DoubleRect& r) const = 0;
    [[nodiscard]] virtual LonLatRect pixelRectFloat2LonLatRect(const DoubleRect& r) const = 0;
    [[nodiscard]] virtual DoubleRect pixelRectFloat2TileRectFloat(const DoubleRect& r) const = 0;

    // --- tile -> * ----------------------------------------------------------
    [[nodiscard]] virtual PointI tilePos2PixelPos(PointI t) const = 0;
    [[nodiscard]] virtual RectI tilePos2PixelRect(PointI t) const = 0;
    [[nodiscard]] virtual DoubleRect tilePos2PixelRectFloat(PointI t) const = 0;
    [[nodiscard]] virtual DoublePoint tilePos2Relative(PointI t) const = 0;
    [[nodiscard]] virtual DoubleRect tilePos2RelativeRect(PointI t) const = 0;
    [[nodiscard]] virtual LonLat tilePos2LonLat(PointI t) const = 0;
    [[nodiscard]] virtual LonLatRect tilePos2LonLatRect(PointI t) const = 0;
    [[nodiscard]] virtual DoublePoint tilePosFloat2PixelPosFloat(DoublePoint t) const = 0;
    [[nodiscard]] virtual DoublePoint tilePosFloat2Relative(DoublePoint t) const = 0;
    [[nodiscard]] virtual LonLat tilePosFloat2LonLat(DoublePoint t) const = 0;
    [[nodiscard]] virtual RectI tileRect2PixelRect(const RectI& r) const = 0;
    [[nodiscard]] virtual DoubleRect tileRect2RelativeRect(const RectI& r) const = 0;
    [[nodiscard]] virtual LonLatRect tileRect2LonLatRect(const RectI& r) const = 0;
    [[nodiscard]] virtual DoubleRect tileRectFloat2PixelRectFloat(const DoubleRect& r) const = 0;
    [[nodiscard]] virtual DoubleRect tileRectFloat2RelativeRect(const DoubleRect& r) const = 0;
    [[nodiscard]] virtual LonLatRect tileRectFloat2LonLatRect(const DoubleRect& r) const = 0;

    // --- relative -> * --------------------------------------------------------
    [[nodiscard]] virtual DoublePoint relative2PixelPosFloat(DoublePoint p) const = 0;
    [[nodiscard]] virtual DoublePoint relative2TilePosFloat(DoublePoint p) const = 0;
    [[nodiscard]] virtual DoubleRect relativeRect2PixelRectFloat(const DoubleRect& r) const = 0;
    [[nodiscard]] virtual DoubleRect relativeRect2TileRectFloat(const DoubleRect& r) const = 0;

    // --- lon/lat -> * -----------------------------------------------------------
    [[nodiscard]] virtual DoublePoint lonLat2PixelPosFloat(LonLat p) const = 0;
    [[nodiscard]] virtual DoublePoint lonLat2TilePosFloat(LonLat p) const = 0;
    [[nodiscard]] virtual DoubleRect lonLatRect2PixelRectFloat(const LonLatRect& r) const = 0;
    [[nodiscard]] virtual DoubleRect lonLatRect2TileRectFloat(const LonLatRect& r) const = 0;

    // --- clamp in place -------------------------------------------------------
    virtual void validateTilePos(PointI& p, bool cycleMap) const = 0;
    virtual void validateTilePosStrict(PointI& p, bool cycleMap) const = 0;
    virtual void validateTileRect(RectI& r) const = 0;
    virtual void validatePixelPos(PointI& p, bool cycleMap) const = 0;
    virtual void validatePixelPosStrict(PointI& p, bool cycleMap) const = 0;
    virtual void validatePixelPosFloat(DoublePoint& p, bool cycleMap) const = 0;
    virtual void validatePixelPosFloatStrict(DoublePoint& p, bool cycleMap) const = 0;
    virtual void validatePixelRect(RectI& r) const = 0;
    virtual void validatePixelRectFloat(DoubleRect& r) const = 0;

    // --- test without clamping ------------------------------------------------
    [[nodiscard]] virtual bool checkTilePos(PointI p) const = 0;
    [[nodiscard]] virtual bool checkTilePosStrict(PointI p) const = 0;
    [[nodiscard]] virtual bool checkTileRect(const RectI& r) const = 0;
    [[nodiscard]] virtual bool checkPixelPos(PointI p) const = 0;
    [[nodiscard]] virtual bool checkPixelPosStrict(PointI p) const = 0;
    [[nodiscard]] virtual bool checkPixelPosFloat(DoublePoint p) const = 0;
    [[nodiscard]] virtual bool checkPixelPosFloatStrict(DoublePoint p) const = 0;
    [[nodiscard]] virtual bool checkPixelRect(const RectI& r) const = 0;
    [[nodiscard]] virtual bool checkPixelRectFloat(const DoubleRect& r) const = 0;
};
using ProjectionPtr = std::shared_ptr<const IProjection>;

// u_ProjectionBasic256x256.pas. `zoom < 24` and `tileSplitCode == 1` are the only two
// Q_ASSERT-worthy programmer errors here (docs/specs/geo.md §8.2); every coordinate is clamped,
// never asserted, because real maps do reach out-of-range paths through rounding (§2.5.3).
class ProjectionBasic256x256 final : public IProjection {
public:
    ProjectionBasic256x256(core::HashValue hash, ProjectionTypePtr projectionType, quint8 zoom);

    [[nodiscard]] core::HashValue hash() const noexcept override { return hash_; }
    [[nodiscard]] quint8 zoom() const noexcept override { return zoom_; }
    [[nodiscard]] const ProjectionTypePtr& projectionType() const noexcept override {
        return projectionType_;
    }
    [[nodiscard]] bool isSame(const IProjection& other) const override;

    [[nodiscard]] RectI tileRect() const noexcept override { return {0, 0, tiles_, tiles_}; }
    [[nodiscard]] RectI pixelRect() const noexcept override { return {0, 0, pixels_, pixels_}; }
    [[nodiscard]] double pixelsFloat() const noexcept override { return pixelsFloat_; }
    [[nodiscard]] int tileSplitCode() const noexcept override { return tileSplitCode_; }
    [[nodiscard]] PointI tileSize(PointI) const noexcept override { return {256, 256}; }

    [[nodiscard]] DoublePoint pixelPos2Relative(PointI p) const override;
    [[nodiscard]] LonLat pixelPos2LonLat(PointI p) const override;
    [[nodiscard]] DoublePoint pixelPos2TilePosFloat(PointI p) const override;
    [[nodiscard]] DoublePoint pixelPosFloat2Relative(DoublePoint p) const override;
    [[nodiscard]] LonLat pixelPosFloat2LonLat(DoublePoint p) const override;
    [[nodiscard]] DoublePoint pixelPosFloat2TilePosFloat(DoublePoint p) const override;
    [[nodiscard]] RectI pixelRect2TileRect(const RectI& r) const override;
    [[nodiscard]] DoubleRect pixelRect2RelativeRect(const RectI& r) const override;
    [[nodiscard]] LonLatRect pixelRect2LonLatRect(const RectI& r) const override;
    [[nodiscard]] DoubleRect pixelRect2TileRectFloat(const RectI& r) const override;
    [[nodiscard]] DoubleRect pixelRectFloat2RelativeRect(const DoubleRect& r) const override;
    [[nodiscard]] LonLatRect pixelRectFloat2LonLatRect(const DoubleRect& r) const override;
    [[nodiscard]] DoubleRect pixelRectFloat2TileRectFloat(const DoubleRect& r) const override;

    [[nodiscard]] PointI tilePos2PixelPos(PointI t) const override;
    [[nodiscard]] RectI tilePos2PixelRect(PointI t) const override;
    [[nodiscard]] DoubleRect tilePos2PixelRectFloat(PointI t) const override;
    [[nodiscard]] DoublePoint tilePos2Relative(PointI t) const override;
    [[nodiscard]] DoubleRect tilePos2RelativeRect(PointI t) const override;
    [[nodiscard]] LonLat tilePos2LonLat(PointI t) const override;
    [[nodiscard]] LonLatRect tilePos2LonLatRect(PointI t) const override;
    [[nodiscard]] DoublePoint tilePosFloat2PixelPosFloat(DoublePoint t) const override;
    [[nodiscard]] DoublePoint tilePosFloat2Relative(DoublePoint t) const override;
    [[nodiscard]] LonLat tilePosFloat2LonLat(DoublePoint t) const override;
    [[nodiscard]] RectI tileRect2PixelRect(const RectI& r) const override;
    [[nodiscard]] DoubleRect tileRect2RelativeRect(const RectI& r) const override;
    [[nodiscard]] LonLatRect tileRect2LonLatRect(const RectI& r) const override;
    [[nodiscard]] DoubleRect tileRectFloat2PixelRectFloat(const DoubleRect& r) const override;
    [[nodiscard]] DoubleRect tileRectFloat2RelativeRect(const DoubleRect& r) const override;
    [[nodiscard]] LonLatRect tileRectFloat2LonLatRect(const DoubleRect& r) const override;

    [[nodiscard]] DoublePoint relative2PixelPosFloat(DoublePoint p) const override;
    [[nodiscard]] DoublePoint relative2TilePosFloat(DoublePoint p) const override;
    [[nodiscard]] DoubleRect relativeRect2PixelRectFloat(const DoubleRect& r) const override;
    [[nodiscard]] DoubleRect relativeRect2TileRectFloat(const DoubleRect& r) const override;

    [[nodiscard]] DoublePoint lonLat2PixelPosFloat(LonLat p) const override;
    [[nodiscard]] DoublePoint lonLat2TilePosFloat(LonLat p) const override;
    [[nodiscard]] DoubleRect lonLatRect2PixelRectFloat(const LonLatRect& r) const override;
    [[nodiscard]] DoubleRect lonLatRect2TileRectFloat(const LonLatRect& r) const override;

    void validateTilePos(PointI& p, bool cycleMap) const override;
    void validateTilePosStrict(PointI& p, bool cycleMap) const override;
    void validateTileRect(RectI& r) const override;
    void validatePixelPos(PointI& p, bool cycleMap) const override;
    void validatePixelPosStrict(PointI& p, bool cycleMap) const override;
    void validatePixelPosFloat(DoublePoint& p, bool cycleMap) const override;
    void validatePixelPosFloatStrict(DoublePoint& p, bool cycleMap) const override;
    void validatePixelRect(RectI& r) const override;
    void validatePixelRectFloat(DoubleRect& r) const override;

    [[nodiscard]] bool checkTilePos(PointI p) const override;
    [[nodiscard]] bool checkTilePosStrict(PointI p) const override;
    [[nodiscard]] bool checkTileRect(const RectI& r) const override;
    [[nodiscard]] bool checkPixelPos(PointI p) const override;
    [[nodiscard]] bool checkPixelPosStrict(PointI p) const override;
    [[nodiscard]] bool checkPixelPosFloat(DoublePoint p) const override;
    [[nodiscard]] bool checkPixelPosFloatStrict(DoublePoint p) const override;
    [[nodiscard]] bool checkPixelRect(const RectI& r) const override;
    [[nodiscard]] bool checkPixelRectFloat(const DoubleRect& r) const override;

private:
    // "Internal" family (u_ProjectionBasic256x256.pas, private, `inline`): clamp silently, log
    // qCDebug (never Q_ASSERT -- §2.5.3), used by every public conversion above. Unlike the public
    // Validate* family they never cycle-wrap.
    void internalValidatePixelPos(PointI& p) const;
    void internalValidatePixelPosFloat(DoublePoint& p) const;
    void internalValidatePixelRect(RectI& r) const;
    void internalValidatePixelRectFloat(DoubleRect& r) const;
    void internalValidateTilePos(PointI& p) const;
    void internalValidateTilePosStrict(PointI& p) const;
    void internalValidateTilePosFloat(DoublePoint& p) const;
    void internalValidateTileRect(RectI& r) const;
    void internalValidateTileRectFloat(DoubleRect& r) const;
    void internalValidateRelativePos(DoublePoint& p) const;
    void internalValidateRelativeRect(DoubleRect& r) const;

    [[nodiscard]] DoublePoint internalPixelPos2Relative(PointI p) const noexcept;
    [[nodiscard]] DoublePoint internalPixelPosFloat2Relative(DoublePoint p) const noexcept;
    [[nodiscard]] DoubleRect internalPixelRect2RelativeRect(const RectI& r) const noexcept;
    [[nodiscard]] DoubleRect internalPixelRectFloat2RelativeRect(const DoubleRect& r) const noexcept;
    [[nodiscard]] RectI internalTilePos2PixelRect(PointI t) const noexcept; // the zoom-23 patch
    [[nodiscard]] DoublePoint internalTilePos2Relative(PointI t) const noexcept;
    [[nodiscard]] DoublePoint internalTilePosFloat2Relative(DoublePoint t) const noexcept;
    [[nodiscard]] DoubleRect internalTileRect2RelativeRect(const RectI& r) const noexcept;
    [[nodiscard]] DoubleRect internalTileRectFloat2RelativeRect(const DoubleRect& r) const noexcept;
    [[nodiscard]] DoublePoint internalRelative2PixelPosFloat(DoublePoint p) const noexcept;
    [[nodiscard]] DoubleRect internalRelativeRect2PixelRectFloat(const DoubleRect& r) const noexcept;
    [[nodiscard]] DoublePoint internalRelative2TilePosFloat(DoublePoint p) const noexcept;
    [[nodiscard]] DoubleRect internalRelativeRect2TileRectFloat(const DoubleRect& r) const noexcept;

    core::HashValue hash_;
    ProjectionTypePtr projectionType_;
    quint8 zoom_;
    int tiles_;
    int pixels_;
    double tilesFloat_;
    double pixelsFloat_;
    int tileSplitCode_;
};

} // namespace cg::geo
