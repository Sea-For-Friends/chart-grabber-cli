// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/projection_set.h"

#include <QtGlobal>

#include <cmath>
#include <limits>
#include <utility>

namespace cg::geo {

ProjectionSetSimple::ProjectionSetSimple(core::HashValue hash, std::vector<ProjectionPtr> zooms)
    : hash_(hash), zooms_(std::move(zooms)) {
    Q_ASSERT(!zooms_.empty());
}

bool ProjectionSetSimple::isSame(const IProjectionSet& other) const {
    // u_ProjectionSetSimple.pas:121-149
    if (this == &other)
        return true;
    if (hash_ != core::kNoHash && other.hash() != core::kNoHash && hash_ != other.hash())
        return false;
    if (zoomCount() != other.zoomCount())
        return false;
    for (quint8 i = 0; i < zoomCount(); ++i) {
        if (!zoom(i)->isSame(*other.zoom(i)))
            return false;
    }
    return true;
}

void ProjectionSetSimple::validateZoom(quint8& zoom) const noexcept {
    if (zoom >= zoomCount()) {
        zoom = static_cast<quint8>(zoomCount() - 1);
    }
}
bool ProjectionSetSimple::checkZoom(quint8 zoom) const noexcept {
    return zoom < zoomCount();
}

bool ProjectionSetSimple::isProjectionFromThisSet(const IProjection& other) const {
    // u_ProjectionSetSimple.pas:106-119
    const quint8 z = other.zoom();
    return z < zoomCount() && zoom(z)->isSame(other);
}

quint8 ProjectionSetSimple::suitableZoom(const IProjection& other) const {
    // u_ProjectionSetSimple.pas:82-94 (marked unfinished in legacy) reduces to other.zoom() for
    // every set reachable today (all four built-ins share the 256x256 pyramid); the general form
    // below (the nearest-pixelsFloat() level) is implemented anyway so a future non-256 split
    // code does not silently mis-stitch (docs/specs/geo.md §2.6).
    if (isProjectionFromThisSet(other)) {
        return other.zoom();
    }
    quint8 best = 0;
    double bestDiff = std::numeric_limits<double>::infinity();
    for (quint8 i = 0; i < zoomCount(); ++i) {
        const double diff = std::abs(zoom(i)->pixelsFloat() - other.pixelsFloat());
        if (diff < bestDiff) {
            bestDiff = diff;
            best = i;
        }
    }
    return best;
}

ProjectionPtr ProjectionSetSimple::suitableProjection(const IProjection& other) const {
    return zoom(suitableZoom(other));
}

ProjectionSetFactory::ProjectionSetFactory(HashSeed hash, std::shared_ptr<const DatumFactory> datums)
    : hash_(hash), types_(hash, std::move(datums)) {
    // u_ProjectionSetFactorySimple.pas:74-96
    google_ = createProjectionSet(types_.byCode(kProjEpsgGoogle));
    esri53004_ = createProjectionSet(types_.byCode(kProjEpsgEsriSphere));
    yandex_ = createProjectionSet(types_.byCode(kProjEpsgYandex));
    geographic_ = createProjectionSet(types_.byCode(kProjEpsgGeographic));
}

ProjectionSetPtr ProjectionSetFactory::createProjectionSet(const ProjectionTypePtr& type) const {
    // u_ProjectionSetFactorySimple.pas:98-121
    std::vector<ProjectionPtr> zooms;
    zooms.reserve(24);
    for (int i = 0; i < 24; ++i) {
        zooms.push_back(
            std::make_shared<const ProjectionBasic256x256>(hash_(type, i), type, static_cast<quint8>(i)));
    }
    return std::make_shared<const ProjectionSetSimple>(type->hash(), std::move(zooms));
}

ProjectionSetPtr ProjectionSetFactory::byCode(int projectionEpsg, int tileSplitCode) const {
    Q_ASSERT(tileSplitCode == kTileSplitQuadrate256x256);
    const ProjectionTypePtr type = types_.byCode(projectionEpsg); // may throw
    switch (type->projectionEpsg()) {
    case kProjEpsgGoogle:
        return google_;
    case kProjEpsgYandex:
        return yandex_;
    case kProjEpsgEsriSphere:
        return esri53004_;
    case kProjEpsgGeographic:
        return geographic_;
    default:
        return createProjectionSet(type); // unreachable: byCode never returns anything else
    }
}

ProjectionSetPtr ProjectionSetFactory::byConfig(const core::IConfigProvider* config) const {
    const ProjectionTypePtr type = types_.byConfig(config); // may throw
    switch (type->projectionEpsg()) {
    case kProjEpsgGoogle:
        return google_;
    case kProjEpsgYandex:
        return yandex_;
    case kProjEpsgEsriSphere:
        return esri53004_;
    case kProjEpsgGeographic:
        return geographic_;
    default:
        return createProjectionSet(type); // the ad-hoc EPSG-0 path
    }
}

std::vector<ProjectionSetListEntry> standardProjectionSetList(const ProjectionSetFactory& factory) {
    // u_ProjectionSetListStaticSimple.pas:45-58 -- three entries, this exact order.
    return {
        {factory.byCode(kProjEpsgGoogle, kTileSplitQuadrate256x256),
         QStringLiteral("Spherical Mercator (GoogleMaps, OSM, Bing) / EPSG:3857")},
        {factory.byCode(kProjEpsgYandex, kTileSplitQuadrate256x256),
         QStringLiteral("World Mercator (Yandex) / EPSG:3395")},
        {factory.byCode(kProjEpsgGeographic, kTileSplitQuadrate256x256),
         QStringLiteral("Geographic (GoogleEarth) / EPSG:4326")},
    };
}

} // namespace cg::geo
