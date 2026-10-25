// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). See job_area.h.
#include "cg/app/job_area.h"

#include "cg/app/gpx_route_reader.h"
#include "cg/core/config_provider_ini.h"
#include "cg/geo/config_provider_helpers.h"
#include "cg/geo/point_stream.h"

#include <QFile>

#include <algorithm>
#include <cmath>
#include <vector>

namespace cg::app {

namespace {

constexpr double kMaxLat = 85.0511;

std::unexpected<CliError> refuse(const QString& text) {
    return std::unexpected(CliError{3, text});
}

std::expected<std::shared_ptr<const geo::IGeometryLonLatPolygon>, CliError>
rectangleArea(const Job& job, const geo::GeometryLonLatFactory& factory) {
    const geo::LonLatRect& box = job.bbox; // left = west, top = north, right = east, bottom = south
    const struct {
        double value;
        double limit;
        const char* what;
    } checks[] = {{box.left, 180.0, "west"},
                  {box.right, 180.0, "east"},
                  {box.bottom, kMaxLat, "south"},
                  {box.top, kMaxLat, "north"}};
    for (const auto& check : checks) {
        if (!(std::abs(check.value) <= check.limit)) {
            return refuse(QStringLiteral("bounding box: %1 value %2 is outside +-%3")
                              .arg(QLatin1String(check.what))
                              .arg(check.value)
                              .arg(check.limit));
        }
    }
    if (!(box.left < box.right) || !(box.bottom < box.top)) {
        return refuse(QStringLiteral("bounding box: west must be below east and south below north (an area "
                                     "across the 180° meridian is not supported)"));
    }
    return factory.createPolygonByRect(box);
}

std::expected<std::shared_ptr<const geo::IGeometryLonLatPolygon>, CliError>
polygonFileArea(const Job& job, const geo::GeometryLonLatFactory& factory) {
    auto ini = core::ConfigProviderIni::open(job.polygonFile);
    if (!ini) {
        return refuse(ini.error().what());
    }
    const auto section = (*ini)->subItem(u"HIGHLIGHTING");
    if (!section) {
        return refuse(QStringLiteral("no [HIGHLIGHTING] section in %1").arg(job.polygonFile));
    }
    auto polygon = geo::readPolygon(*section, factory);
    if (!polygon) { // readPolygon gives null when no first point is found
        return refuse(QStringLiteral("no polygon in %1").arg(job.polygonFile));
    }
    return polygon;
}

std::expected<std::shared_ptr<const geo::IGeometryLonLatPolygon>, CliError>
routeArea(const Job& job, const geo::GeometryLonLatFactory& factory, const geo::IProjection& projection,
          const std::function<void(const QString&)>& warn) {
    QFile file(job.routeFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return refuse(QStringLiteral("%1: %2").arg(job.routeFile, file.errorString()));
    }
    auto read = readGpxRoutes(file);
    if (!read) {
        return std::unexpected(CliError{2, read.error().what()});
    }
    const GpxRoute* route = nullptr;
    if (job.routeName.isEmpty()) {
        if (!read->routes.empty()) {
            route = &read->routes.front();
        }
    } else {
        QStringList names;
        for (const GpxRoute& candidate : read->routes) {
            if (candidate.name == job.routeName) {
                route = &candidate;
                break;
            }
            names.push_back(candidate.name);
        }
        if (!route) {
            return refuse(QStringLiteral("no route named \"%1\" in %2; found: %3")
                              .arg(job.routeName, job.routeFile, names.join(QStringLiteral(", "))));
        }
    }
    if (read->skippedPoints > 0 || read->droppedParts > 0) {
        warn(
            QStringLiteral("%1: %2 point(s) with an invalid position skipped, %3 part(s) with a single point "
                           "dropped")
                .arg(job.routeFile)
                .arg(read->skippedPoints)
                .arg(read->droppedParts));
    }
    if (!route) {
        return refuse(QStringLiteral("no route or track in %1").arg(job.routeFile));
    }
    std::size_t total = 0;
    double west = 0;
    double east = 0;
    for (const auto& part : route->parts) {
        for (const geo::LonLat& point : part) {
            west = total == 0 ? point.lon : std::min(west, point.lon);
            east = total == 0 ? point.lon : std::max(east, point.lon);
            ++total;
        }
    }
    if (total < 2) {
        return refuse(QStringLiteral("the route in %1 has fewer than two points").arg(job.routeFile));
    }
    if (east - west > 180.0) {
        return refuse(QStringLiteral("the route in %1 spans more than 180° of longitude (an area across the "
                                     "180° meridian is not supported)")
                          .arg(job.routeFile));
    }
    if (route->parts.size() > 1) {
        warn(QStringLiteral(
                 "the route has %1 parts; a resumed session covers the first part only (legacy .sls "
                 "format)")
                 .arg(route->parts.size()));
    }
    const auto line = std::dynamic_pointer_cast<const geo::IGeometryLonLatLine>(route->toLine(factory));
    if (!line) {
        return refuse(QStringLiteral("the route in %1 has fewer than two points").arg(job.routeFile));
    }
    auto polygon = corridorByLine(*line, job.corridorNm, projection, factory);
    if (!polygon) {
        return refuse(QStringLiteral("no polygon from the route in %1").arg(job.routeFile));
    }
    return polygon;
}

} // namespace

std::expected<std::shared_ptr<const geo::IGeometryLonLatPolygon>, CliError>
buildArea(const Job& job, const geo::GeometryLonLatFactory& factory,
          const geo::IProjection& corridorProjection, const std::function<void(const QString&)>& warn) {
    switch (job.areaKind) {
    case AreaKind::BBox:
        return rectangleArea(job, factory);
    case AreaKind::PolygonFile:
        return polygonFileArea(job, factory);
    case AreaKind::Route:
        return routeArea(job, factory, corridorProjection, warn);
    case AreaKind::None:
        break;
    }
    return refuse(QStringLiteral("no area given")); // parseJob refuses this earlier (job.cpp, S-3)
}

// frm_Main.pas:7624-7636: TLonLatPointFilterLine2Poly fed to CreateLonLatPolygonByLonLatPathAndFilter.
std::shared_ptr<const geo::IGeometryLonLatPolygon> corridorByLine(const geo::IGeometryLonLatLine& line,
                                                                  double widthNm,
                                                                  const geo::IProjection& projection,
                                                                  const geo::GeometryLonLatFactory& factory) {
    const double radiusMetres = widthNm * 1852.0 / 2.0;
    std::vector<geo::DoublePoint> scratch;
    const geo::LonLatPointFilter filter = [&](geo::LonLatStream source) {
        return geo::LonLatStream(
            geo::line2Poly(std::move(source).release(), radiusMetres, projection, scratch));
    };
    return std::dynamic_pointer_cast<const geo::IGeometryLonLatPolygon>(
        factory.createPolygonByLineAndFilter(line, filter));
}

} // namespace cg::app
