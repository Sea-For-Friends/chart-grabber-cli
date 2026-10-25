// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/projection_type_factory.h"

#include <QLatin1StringView>

#include <cmath>

namespace cg::geo {
namespace {
constexpr double kToleranceMetres = 1.0;
} // namespace

ProjectionTypeFactory::ProjectionTypeFactory(HashSeed hash, std::shared_ptr<const DatumFactory> datums)
    : hash_(hash), datums_(std::move(datums)) {
    // u_ProjectionTypeFactorySimple.pas:67-107. Each hash is CalcHashByInteger(seed);
    // UpdateHashByHash(datum.hash); UpdateHashByInteger(projectionEpsg) -- i.e. hash_(seed, datum, epsg).
    const DatumPtr googleDatum = datums_->byCode(kDatumEpsgGoogleSphere);
    google_ = std::make_shared<const MercatorSphereProjectionType>(hash_(1, googleDatum, kProjEpsgGoogle),
                                                                   googleDatum, kProjEpsgGoogle);

    const DatumPtr esriDatum = datums_->byCode(kDatumEpsgEsriSphere);
    esri53004_ = std::make_shared<const MercatorSphereProjectionType>(
        hash_(1, esriDatum, kProjEpsgEsriSphere), esriDatum, kProjEpsgEsriSphere);

    const DatumPtr wgs84Datum = datums_->byCode(kDatumEpsgWgs84);
    yandex_ = std::make_shared<const MercatorEllipsoidProjectionType>(hash_(2, wgs84Datum, kProjEpsgYandex),
                                                                      wgs84Datum, kProjEpsgYandex);

    geographic_ = std::make_shared<const GeographicProjectionType>(hash_(3, wgs84Datum, kProjEpsgGeographic),
                                                                   wgs84Datum, kProjEpsgGeographic);
}

ProjectionTypePtr ProjectionTypeFactory::byCode(int projectionEpsg) const {
    // u_ProjectionTypeFactorySimple.pas:109-131
    switch (projectionEpsg) {
    case kProjEpsgGoogle:
    case kProjEpsgGoogleOld:
        return google_;
    case kProjEpsgEsriSphere:
        return esri53004_;
    case kProjEpsgYandex:
        return yandex_;
    case kProjEpsgGeographic:
        return geographic_;
    default:
        throw UnexpectedProjectionType(projectionEpsg);
    }
}

ProjectionTypePtr ProjectionTypeFactory::byConfig(const core::IConfigProvider* config) const {
    // u_ProjectionTypeFactorySimple.pas:133-219, verbatim.
    int epsg = 0;
    int projection = 1;
    double radiusA = 6378137.0;
    double radiusB = radiusA;

    if (config != nullptr) {
        epsg = config->readInt(u"EPSG", epsg);
        projection = config->readInt(u"projection", projection);
        radiusA = config->readFloat(u"sradiusa", radiusA);
        radiusB = config->readFloat(u"sradiusb", radiusA); // default IS sradiusa, not a constant
    }

    if (epsg == 0) {
        switch (projection) {
        case 1:
            if (std::abs(radiusA - 6378137.0) < kToleranceMetres) {
                epsg = kProjEpsgGoogle;
            } else if (std::abs(radiusA - 6371000.0) < kToleranceMetres) {
                epsg = kProjEpsgEsriSphere;
            }
            break;
        case 2:
            if (std::abs(radiusA - 6378137.0) < kToleranceMetres &&
                std::abs(radiusB - 6356752.0) < kToleranceMetres) {
                epsg = kProjEpsgYandex;
            }
            break;
        case 3:
            if (std::abs(radiusA - 6378137.0) < kToleranceMetres &&
                std::abs(radiusB - 6356752.0) < kToleranceMetres) {
                epsg = kProjEpsgGeographic;
            }
            break;
        default:
            throw UnexpectedProjectionType(projection);
        }
    }

    ProjectionTypePtr result;
    if (epsg != 0) {
        try {
            result = byCode(epsg);
        } catch (const UnexpectedProjectionType&) {
            result = nullptr; // an unknown non-zero EPSG falls through, it does not throw
        }
    }

    if (result) {
        return result;
    }

    switch (projection) {
    case 1: {
        // projection=1 passes radiusA as BOTH radii, ignoring sradiusb entirely (rule 4).
        const DatumPtr d = datums_->byRadius(radiusA, radiusA);
        if (d->epsg() == kDatumEpsgGoogleSphere)
            return google_;
        if (d->epsg() == kDatumEpsgEsriSphere)
            return esri53004_; // unreachable, see datum.cpp
        return std::make_shared<const MercatorSphereProjectionType>(hash_(1, d, 0), d, 0);
    }
    case 2: {
        const DatumPtr d = datums_->byRadius(radiusA, radiusB);
        if (d->epsg() == kDatumEpsgWgs84)
            return yandex_;
        return std::make_shared<const MercatorEllipsoidProjectionType>(hash_(2, d, 0), d, 0);
    }
    case 3: {
        const DatumPtr d = datums_->byRadius(radiusA, radiusB);
        if (d->epsg() == kDatumEpsgWgs84)
            return geographic_;
        return std::make_shared<const GeographicProjectionType>(hash_(3, d, 0), d, 0);
    }
    default:
        throw UnexpectedProjectionType(projection);
    }
}

} // namespace cg::geo
