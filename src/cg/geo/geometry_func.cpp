// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/geometry_func.h"

#include "cg/geo/projection.h"
#include "cg/geo/projection_type.h"

#include <QtGlobal>

#include <optional>
#include <span>

namespace cg::geo {
namespace {

struct NearestResult {
    LonLat point;
    double sqDist = 0.0;
};

void combine(std::optional<NearestResult>& best, std::optional<NearestResult> candidate) {
    if (candidate && (!best || candidate->sqDist < best->sqDist)) {
        best = candidate;
    }
}

// GetGeometryLonLat{Point,Line,Contour}NearestPoint (u_GeometryFunc.pas:179-266): ValidateLonLatPos
// clamps/wraps each point before projecting, and the RETURNED point is the validated one, not the
// raw vertex.
[[nodiscard]] std::optional<NearestResult> nearestInPoints(std::span<const LonLat> points,
                                                           const IProjectionType& projType,
                                                           const IProjection& projection,
                                                           DoublePoint currentMapPixel) {
    std::optional<NearestResult> best;
    for (LonLat p : points) {
        projType.validateLonLatPos(p);
        const DoublePoint mapPoint = projection.lonLat2PixelPosFloat(p);
        const double dx = mapPoint.x() - currentMapPixel.x();
        const double dy = mapPoint.y() - currentMapPixel.y();
        const double sqDist = dx * dx + dy * dy;
        if (!best || sqDist < best->sqDist) {
            best = NearestResult{p, sqDist};
        }
    }
    return best;
}

// GetGeometryLonLatPolygonNearestPoint (u_GeometryFunc.pas:284-315): outer border, then every
// hole, minimum across all.
[[nodiscard]] std::optional<NearestResult> nearestInPolygon(const IGeometryLonLatSinglePolygon& polygon,
                                                            const IProjectionType& projType,
                                                            const IProjection& projection,
                                                            DoublePoint currentMapPixel) {
    std::optional<NearestResult> best =
        nearestInPoints(polygon.outerBorder()->points(), projType, projection, currentMapPixel);
    for (std::size_t i = 0; i < polygon.holeCount(); ++i) {
        combine(best,
                nearestInPoints(polygon.holeBorder(i)->points(), projType, projection, currentMapPixel));
    }
    return best;
}

[[nodiscard]] bool isValidContour(const IGeometryLonLatContour& contour) {
    return contour.points().size() > 2;
}

[[nodiscard]] bool isValidSinglePolygon(const IGeometryLonLatSinglePolygon& polygon) {
    return isValidContour(*polygon.outerBorder());
}

// GeometryLonLatSingleLineToPlainText (u_GeometryFunc.pas:1040-1055): join every vertex with
// `pointSep`; empty for a 0-point line (legacy's `if AGeometry.Count > 0` guard).
QString singleLineToPlainText(const IGeometryLonLatSingleLine& line, const CoordToStringConverter& converter,
                              const QString& pointSep) {
    const auto pts = line.points();
    if (pts.empty()) {
        return QString();
    }
    QString result = converter.lonLatConvert(pts[0]);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        result += pointSep + converter.lonLatConvert(pts[i]);
    }
    return result;
}

// GeometryLonLatLineToPlainText (u_GeometryFunc.pas:1067-1086): single-or-multi dispatch, joining
// multi items with `multiSep`.
QString lineToPlainText(const IGeometryLonLatLine& line, const CoordToStringConverter& converter,
                        const QString& pointSep, const QString& multiSep) {
    if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiLine*>(&line)) {
        if (multi->count() == 0) {
            return QString();
        }
        QString result = singleLineToPlainText(*multi->item(0), converter, pointSep);
        for (std::size_t i = 1; i < multi->count(); ++i) {
            result += multiSep + singleLineToPlainText(*multi->item(i), converter, pointSep);
        }
        return result;
    }
    if (const auto* single = dynamic_cast<const IGeometryLonLatSingleLine*>(&line)) {
        return singleLineToPlainText(*single, converter, pointSep);
    }
    return QString();
}

// GeometryLonLatContourToPlainText (u_GeometryFunc.pas:1088-1105): joins every vertex with
// `pointSep`, THEN appends `pointSep` + the first vertex again -- the display-only auto-close
// (points() itself stays open, geometry_lonlat.h; the WKB writer is the only other auto-closer,
// §3.7 rule 5). Empty for a 0-point contour.
QString contourToPlainText(const IGeometryLonLatContour& contour, const CoordToStringConverter& converter,
                           const QString& pointSep) {
    const auto pts = contour.points();
    if (pts.empty()) {
        return QString();
    }
    QString result = converter.lonLatConvert(pts[0]);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        result += pointSep + converter.lonLatConvert(pts[i]);
    }
    result += pointSep + converter.lonLatConvert(pts[0]);
    return result;
}

// GeometryLonLatSinglePolygonToPlainText (u_GeometryFunc.pas:1107-1120): outer border, then every
// hole, each joined by `contourSep`.
QString singlePolygonToPlainText(const IGeometryLonLatSinglePolygon& polygon,
                                 const CoordToStringConverter& converter, const QString& pointSep,
                                 const QString& contourSep) {
    QString result = contourToPlainText(*polygon.outerBorder(), converter, pointSep);
    for (std::size_t i = 0; i < polygon.holeCount(); ++i) {
        result += contourSep + contourToPlainText(*polygon.holeBorder(i), converter, pointSep);
    }
    return result;
}

// GeometryLonLatPolygonToPlainText (u_GeometryFunc.pas:1127-1148): single-or-multi dispatch,
// joining multi items with `multiSep`.
QString polygonToPlainText(const IGeometryLonLatPolygon& polygon, const CoordToStringConverter& converter,
                           const QString& pointSep, const QString& contourSep, const QString& multiSep) {
    if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiPolygon*>(&polygon)) {
        if (multi->count() == 0) {
            return QString();
        }
        QString result = singlePolygonToPlainText(*multi->item(0), converter, pointSep, contourSep);
        for (std::size_t i = 1; i < multi->count(); ++i) {
            result += multiSep + singlePolygonToPlainText(*multi->item(i), converter, pointSep, contourSep);
        }
        return result;
    }
    if (const auto* single = dynamic_cast<const IGeometryLonLatSinglePolygon*>(&polygon)) {
        return singlePolygonToPlainText(*single, converter, pointSep, contourSep);
    }
    return QString();
}

} // namespace

LonLat nearestPoint(const IGeometryLonLat& geometry, const IProjection& projection,
                    DoublePoint currentMapPixel, double maxDistInMapPixel) {
    // GetGeometryLonLatNearestPoint (u_GeometryFunc.pas:393-432): the dispatch order (point,
    // multi-line, multi-polygon, single line, single polygon) is legacy's; dynamic_cast makes the
    // order immaterial since the five shapes are mutually exclusive.
    const IProjectionType& projType = *projection.projectionType();
    std::optional<NearestResult> best;

    if (const auto* point = dynamic_cast<const IGeometryLonLatPoint*>(&geometry)) {
        const LonLat p = point->point();
        if (!pointIsEmpty(p.toPoint())) {
            best = nearestInPoints(std::span<const LonLat>(&p, 1), projType, projection, currentMapPixel);
        }
    } else if (const auto* multiLine = dynamic_cast<const IGeometryLonLatMultiLine*>(&geometry)) {
        for (std::size_t i = 0; i < multiLine->count(); ++i) {
            combine(best,
                    nearestInPoints(multiLine->item(i)->points(), projType, projection, currentMapPixel));
        }
    } else if (const auto* multiPolygon = dynamic_cast<const IGeometryLonLatMultiPolygon*>(&geometry)) {
        for (std::size_t i = 0; i < multiPolygon->count(); ++i) {
            combine(best, nearestInPolygon(*multiPolygon->item(i), projType, projection, currentMapPixel));
        }
    } else if (const auto* singleLine = dynamic_cast<const IGeometryLonLatSingleLine*>(&geometry)) {
        best = nearestInPoints(singleLine->points(), projType, projection, currentMapPixel);
    } else if (const auto* singlePolygon = dynamic_cast<const IGeometryLonLatSinglePolygon*>(&geometry)) {
        best = nearestInPolygon(*singlePolygon, projType, projection, currentMapPixel);
    } else {
        Q_ASSERT(false); // u_GeometryFunc.pas:429 Assert(False)
    }

    if (best && best->sqDist <= maxDistInMapPixel * maxDistInMapPixel) {
        return best->point;
    }
    return LonLat::fromPoint(emptyDoublePoint());
}

bool isValidLonLatLine(const IGeometryLonLatLine& geometry) {
    if (const auto* single = dynamic_cast<const IGeometryLonLatSingleLine*>(&geometry)) {
        return single->points().size() > 1;
    }
    if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiLine*>(&geometry)) {
        return multi->count() > 1 || (multi->count() > 0 && multi->item(0)->points().size() > 1);
    }
    return false;
}

bool isValidLonLatPolygon(const IGeometryLonLatPolygon& geometry) {
    if (const auto* single = dynamic_cast<const IGeometryLonLatSinglePolygon*>(&geometry)) {
        return isValidSinglePolygon(*single);
    }
    if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiPolygon*>(&geometry)) {
        return multi->count() > 1 || (multi->count() > 0 && isValidSinglePolygon(*multi->item(0)));
    }
    return false;
}

bool isLonLatPolygonSimpleRect(const IGeometryLonLatPolygon& polygon) {
    const auto* single = dynamic_cast<const IGeometryLonLatSinglePolygon*>(&polygon);
    if (single == nullptr || single->holeCount() != 0) {
        return false;
    }
    const auto pts = single->outerBorder()->points();
    if (pts.size() != 4) {
        return false;
    }
    PointI corners[4];
    for (int i = 0; i < 4; ++i) {
        corners[i] = roundPoint(pts[i].toPoint(), PointRounding::ToTopLeft);
    }
    return (corners[0].x() == corners[3].x() && corners[2].x() == corners[1].x() &&
            corners[0].y() == corners[1].y() && corners[2].y() == corners[3].y()) ||
           (corners[0].x() == corners[1].x() && corners[2].x() == corners[3].x() &&
            corners[3].y() == corners[0].y() && corners[1].y() == corners[2].y());
}

QString geometryToPlainText(const IGeometryLonLat& geometry, const CoordToStringConverter& converter,
                            const QString& pointSep, const QString& contourSep, const QString& multiSep) {
    // GeometryLonLatToPlainText (u_GeometryFunc.pas:1017-1034): Point, then Line, then Polygon --
    // the three Supports() checks are mutually exclusive, so dynamic_cast order does not matter,
    // same as nearestPoint above.
    if (const auto* point = dynamic_cast<const IGeometryLonLatPoint*>(&geometry)) {
        return converter.lonLatConvert(point->point());
    }
    if (const auto* line = dynamic_cast<const IGeometryLonLatLine*>(&geometry)) {
        return lineToPlainText(*line, converter, pointSep, multiSep);
    }
    if (const auto* polygon = dynamic_cast<const IGeometryLonLatPolygon*>(&geometry)) {
        return polygonToPlainText(*polygon, converter, pointSep, contourSep, multiSep);
    }
    return QString(); // legacy: none of the three Supports() checks matched -> Result := ''
}

} // namespace cg::geo
