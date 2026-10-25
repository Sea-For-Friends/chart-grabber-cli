// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/config_provider_helpers.h"

#include "cg/geo/geo_types.h"

#include <QString>
#include <QStringView>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace cg::geo {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// The key spellings of u_ConfigProviderHelpers.pas:271-285, in the order the Pascal tries them
// (each spelling at index 0, then at index 1).
struct KeySpelling {
    QStringView lon;
    QStringView lat;
};
constexpr std::array<KeySpelling, 3> kKeySpellings{{
    {u"PointLon_", u"PointLat_"},
    {u"LLPointX_", u"LLPointY_"},
    {u"PointX_", u"PointY_"},
}};

// Where the key search (:271-297) found the first valid point.
struct FirstPoint {
    QString lon;
    QString lat;
    std::size_t index;
};

// u_ConfigProviderHelpers.pas:244-257 CheckIsValidPoint. Both values are read with default -10000;
// a missing key therefore reads as an invalid point.
[[nodiscard]] bool checkIsValidPoint(const core::IConfigProvider& section, QStringView lonPrefix,
                                     QStringView latPrefix, std::size_t index) {
    const QString suffix = QString::number(index);
    const DoublePoint point{section.readFloat(lonPrefix.toString() + suffix, -10000),
                            section.readFloat(latPrefix.toString() + suffix, -10000)};
    return !pointIsEmpty(point) && std::abs(point.x()) < 360.0 && std::abs(point.y()) < 360.0;
}

// u_ConfigProviderHelpers.pas:271-297: the first valid point in the spelling/index order above.
[[nodiscard]] std::optional<FirstPoint> findFirstPoint(const core::IConfigProvider& section) {
    for (const KeySpelling& spelling : kKeySpellings) {
        for (const std::size_t index : {std::size_t{0}, std::size_t{1}}) {
            if (checkIsValidPoint(section, spelling.lon, spelling.lat, index)) {
                return FirstPoint{spelling.lon.toString(), spelling.lat.toString(), index};
            }
        }
    }
    return std::nullopt;
}

// u_ConfigProviderHelpers.pas:328-346 WriteContour. startIndex is the Pascal var AStartIndex.
void writeContour(core::IConfigWriter& section, const IGeometryLonLatContour& contour,
                  std::size_t& startIndex) {
    const auto points = contour.points();
    for (std::size_t i = 0; i < points.size(); ++i) {
        const QString index = QString::number(startIndex + i);
        section.writeFloat(QStringLiteral("PointLon_") + index, points[i].lon);
        section.writeFloat(QStringLiteral("PointLat_") + index, points[i].lat);
    }
    startIndex += points.size();
}

// u_ConfigProviderHelpers.pas:348-371 WriteSinglePolygon.
void writeSinglePolygon(core::IConfigWriter& section, const IGeometryLonLatSinglePolygon& polygon,
                        std::size_t& startIndex) {
    writeContour(section, *polygon.outerBorder(), startIndex);
    for (std::size_t h = 0; h < polygon.holeCount(); ++h) {
        // DEVIATION D-158 (T-0272, L-260): the separator goes to the next free index; legacy writes it at
        // startIndex + h / + i (u_ConfigProviderHelpers.pas:363-365 / 389-391), which loses every later part
        // on read-back.
        const QString index = QString::number(startIndex);
        section.writeFloat(QStringLiteral("PointLon_") + index, kNaN);
        section.writeFloat(QStringLiteral("PointLat_") + index, -1);
        ++startIndex;
        writeContour(section, *polygon.holeBorder(h), startIndex);
    }
}

// u_ConfigProviderHelpers.pas:373-397 WriteMultiPolygon.
void writeMultiPolygon(core::IConfigWriter& section, const IGeometryLonLatMultiPolygon& polygon,
                       std::size_t& startIndex) {
    // Pascal Assert(Count > 0) (off in the shipped build). Unreachable in legacy: a multi-polygon is
    // only built by collapsePolygons with two or more items (geometry_factory.cpp:207); no register row.
    if (polygon.count() == 0) {
        return;
    }
    writeSinglePolygon(section, *polygon.item(0), startIndex);
    for (std::size_t i = 1; i < polygon.count(); ++i) {
        // DEVIATION D-158 (T-0272, L-260): the separator goes to the next free index; legacy writes it at
        // startIndex + h / + i (u_ConfigProviderHelpers.pas:363-365 / 389-391), which loses every later part
        // on read-back.
        const QString index = QString::number(startIndex);
        section.writeFloat(QStringLiteral("PointLon_") + index, kNaN);
        section.writeFloat(QStringLiteral("PointLat_") + index, kNaN);
        ++startIndex;
        writeSinglePolygon(section, *polygon.item(i), startIndex);
    }
}

} // namespace

// FEAT-GEO-024 (see config_provider_helpers.h). u_ConfigProviderHelpers.pas:239-326.
std::shared_ptr<const IGeometryLonLatPolygon> readPolygon(const core::IConfigProvider& section,
                                                          const GeometryLonLatFactory& factory) {
    const std::optional<FirstPoint> first = findFirstPoint(section);
    if (!first) {
        return nullptr;
    }

    GeometryLonLatPolygonBuilder builder = factory.makePolygonBuilder();
    std::vector<LonLat> contour; // VPointsAggregator
    bool isOuter = true;
    bool isFinish = false;
    std::size_t index = first->index;
    do {
        const QString suffix = QString::number(index);
        const double x = section.readFloat(first->lon + suffix, -10000);
        const double y = section.readFloat(first->lat + suffix, -10000);
        ++index;
        const bool validX = !std::isnan(x);
        const bool validY = !std::isnan(y);
        // |x| == 360 is not a stop here (only > 360 is); it is appended below. Kept literal.
        if ((validX && std::abs(x) > 360.0) || (validY && std::abs(y) > 360.0)) {
            isFinish = true;
        }
        if (!isFinish && validX && validY) {
            contour.push_back(LonLat{x, y});
        } else if (!contour.empty()) {
            // The flush also runs on the finishing pass, so the last contour is kept.
            LonLatPointsPtr points = std::make_shared<const std::vector<LonLat>>(std::move(contour));
            contour.clear();
            if (isOuter) {
                builder.addOuter(std::move(points));
            } else {
                builder.addHole(std::move(points));
            }
            isOuter = !validY;
        }
    } while (!isFinish);

    return std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(builder.makeStaticAndClear());
}

// FEAT-REG-006 (see config_provider_helpers.h). u_ConfigProviderHelpers.pas:400-419.
void writePolygon(core::IConfigWriter& section,
                  const std::shared_ptr<const IGeometryLonLatPolygon>& polygon) {
    if (!polygon) {
        return;
    }
    std::size_t index = 0;
    if (const auto single = std::dynamic_pointer_cast<const IGeometryLonLatSinglePolygon>(polygon)) {
        writeSinglePolygon(section, *single, index);
    } else if (const auto multi = std::dynamic_pointer_cast<const IGeometryLonLatMultiPolygon>(polygon)) {
        writeMultiPolygon(section, *multi, index);
    }
    // Neither single nor multi: legacy Assert(False) (:416, off in the shipped build), nothing written.
}

} // namespace cg::geo
