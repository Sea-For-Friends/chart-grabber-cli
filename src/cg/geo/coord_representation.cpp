// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_CoordRepresentation.pas (persisted-id mapping), u_CoordRepresentationConfig.pas (the
// value half). docs/specs/geo.md:1896-1968. FEAT-GEO-004.
#include "cg/geo/coord_representation.h"

#include <cstddef>

namespace cg::geo {

namespace {

// u_CoordRepresentation.pas:76-78
constexpr int kCoordSysTypeId[] = {
    0,  // Wgs84
    3,  // Utm
    4,  // Mgrs
    1,  // Sk42
    2,  // Sk42Gk
    11, // Gsk2011
    12, // Gsk2011Gk
};

// u_CoordRepresentation.pas:170-172
constexpr int kGeogCoordShowFormatId[] = {0, 1, 2, 21, 3, 4, 5, 51};

template <typename Enum, std::size_t N> Enum firstMatching(const int (&table)[N], int id, Enum fallback) {
    for (std::size_t i = 0; i < N; ++i) {
        if (table[i] == id) {
            return static_cast<Enum>(i);
        }
    }
    return fallback;
}

} // namespace

int coordSysTypeToInt(CoordSysType v) noexcept {
    return kCoordSysTypeId[static_cast<int>(v)];
}

CoordSysType intToCoordSysType(int v) noexcept {
    return firstMatching(kCoordSysTypeId, v, CoordSysType::Wgs84);
}

int geogCoordShowFormatToInt(GeogCoordShowFormat v) noexcept {
    return kGeogCoordShowFormatId[static_cast<int>(v)];
}

GeogCoordShowFormat intToGeogCoordShowFormat(int v) noexcept {
    return firstMatching(kGeogCoordShowFormatId, v, GeogCoordShowFormat::CharDegrMinSec);
}

int projCoordShowFormatToInt(ProjCoordShowFormat v) noexcept {
    return static_cast<int>(v);
}

ProjCoordShowFormat intToProjCoordShowFormat(int v) noexcept {
    if (v >= 0 && v <= static_cast<int>(ProjCoordShowFormat::RoundedToThousandths)) {
        return static_cast<ProjCoordShowFormat>(v);
    }
    return ProjCoordShowFormat::RoundedToWhole;
}

int mgrsCoordShowFormatToInt(MgrsCoordShowFormat v) noexcept {
    return static_cast<int>(v);
}

MgrsCoordShowFormat intToMgrsCoordShowFormat(int v) noexcept {
    if (v >= 0 && v <= static_cast<int>(MgrsCoordShowFormat::Joined)) {
        return static_cast<MgrsCoordShowFormat>(v);
    }
    return MgrsCoordShowFormat::Splitted;
}

int coordSysInfoTypeToInt(CoordSysInfoType v) noexcept {
    return static_cast<int>(v);
}

CoordSysInfoType intToCoordSysInfoType(int v) noexcept {
    if (v >= 0 && v <= static_cast<int>(CoordSysInfoType::ShowForAll)) {
        return static_cast<CoordSysInfoType>(v);
    }
    return CoordSysInfoType::DontShow;
}

CoordRepresentationSettings CoordRepresentationSettings::read(const core::IConfigProvider& src) {
    CoordRepresentationSettings s; // defaults mirror TCoordRepresentationConfig.Create
    s.isLatitudeFirst = src.readBool(u"FirstLat", s.isLatitudeFirst);
    s.geogFormat =
        intToGeogCoordShowFormat(src.readInt(u"DegrisShowFormat", geogCoordShowFormatToInt(s.geogFormat)));
    s.projFormat =
        intToProjCoordShowFormat(src.readInt(u"ProjCoordShowFormat", projCoordShowFormatToInt(s.projFormat)));
    s.mgrsFormat =
        intToMgrsCoordShowFormat(src.readInt(u"MgrsCoordShowFormat", mgrsCoordShowFormatToInt(s.mgrsFormat)));
    s.coordSysType = intToCoordSysType(src.readInt(u"CoordSysType", coordSysTypeToInt(s.coordSysType)));
    s.coordSysInfoType =
        intToCoordSysInfoType(src.readInt(u"CoordSysInfoType", coordSysInfoTypeToInt(s.coordSysInfoType)));
    return s;
}

void CoordRepresentationSettings::write(core::IConfigWriter& dst) const {
    dst.writeBool(u"FirstLat", isLatitudeFirst);
    dst.writeInt(u"DegrisShowFormat", geogCoordShowFormatToInt(geogFormat));
    dst.writeInt(u"ProjCoordShowFormat", projCoordShowFormatToInt(projFormat));
    dst.writeInt(u"MgrsCoordShowFormat", mgrsCoordShowFormatToInt(mgrsFormat));
    dst.writeInt(u"CoordSysType", coordSysTypeToInt(coordSysType));
    dst.writeInt(u"CoordSysInfoType", coordSysInfoTypeToInt(coordSysInfoType));
}

} // namespace cg::geo
