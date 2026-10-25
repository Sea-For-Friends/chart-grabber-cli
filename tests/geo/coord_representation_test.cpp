// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-004; docs/specs/geo.md:1896-1968 (§6.1), test plan §11.1 item 13.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/geo/coord_representation.h"

#include <gtest/gtest.h>

#include <memory>

using cg::core::ConfigProviderIni;
using cg::core::IniFile;
using cg::geo::CoordRepresentationSettings;
using cg::geo::CoordSysInfoType;
using cg::geo::coordSysInfoTypeToInt;
using cg::geo::CoordSysType;
using cg::geo::coordSysTypeToInt;
using cg::geo::GeogCoordShowFormat;
using cg::geo::geogCoordShowFormatToInt;
using cg::geo::intToCoordSysInfoType;
using cg::geo::intToCoordSysType;
using cg::geo::intToGeogCoordShowFormat;
using cg::geo::intToMgrsCoordShowFormat;
using cg::geo::intToProjCoordShowFormat;
using cg::geo::MgrsCoordShowFormat;
using cg::geo::mgrsCoordShowFormatToInt;
using cg::geo::ProjCoordShowFormat;
using cg::geo::projCoordShowFormatToInt;

// --- persisted-id mapping (u_CoordRepresentation.pas:76-78,170-172) ------------------------------

TEST(CoordRepresentation, CoordSysTypeIdsMatchLegacyTable) {
    // u_CoordRepresentation.pas:76-78: CCoordSysTypeId = (0, 3, 4, 1, 2, 11, 12)
    EXPECT_EQ(coordSysTypeToInt(CoordSysType::Wgs84), 0);
    EXPECT_EQ(coordSysTypeToInt(CoordSysType::Utm), 3);
    EXPECT_EQ(coordSysTypeToInt(CoordSysType::Mgrs), 4);
    EXPECT_EQ(coordSysTypeToInt(CoordSysType::Sk42), 1);
    EXPECT_EQ(coordSysTypeToInt(CoordSysType::Sk42Gk), 2);
    EXPECT_EQ(coordSysTypeToInt(CoordSysType::Gsk2011), 11);
    EXPECT_EQ(coordSysTypeToInt(CoordSysType::Gsk2011Gk), 12);

    for (const auto v : {CoordSysType::Wgs84, CoordSysType::Utm, CoordSysType::Mgrs, CoordSysType::Sk42,
                         CoordSysType::Sk42Gk, CoordSysType::Gsk2011, CoordSysType::Gsk2011Gk}) {
        EXPECT_EQ(intToCoordSysType(coordSysTypeToInt(v)), v);
    }
}

TEST(CoordRepresentation, UnknownCoordSysTypeIdFallsBackToWgs84) {
    // IntegerToCoordSysType falls through the loop unmatched -> Low(TCoordSysType) = cstWGS84.
    EXPECT_EQ(intToCoordSysType(99), CoordSysType::Wgs84);
    EXPECT_EQ(intToCoordSysType(-1), CoordSysType::Wgs84);
}

TEST(CoordRepresentation, GeogCoordShowFormatIdsMatchLegacyTable) {
    // u_CoordRepresentation.pas:170-172: CGeogCoordShowFormatId = (0, 1, 2, 21, 3, 4, 5, 51)
    EXPECT_EQ(geogCoordShowFormatToInt(GeogCoordShowFormat::CharDegrMinSec), 0);
    EXPECT_EQ(geogCoordShowFormatToInt(GeogCoordShowFormat::CharDegrMin), 1);
    EXPECT_EQ(geogCoordShowFormatToInt(GeogCoordShowFormat::CharDegr), 2);
    EXPECT_EQ(geogCoordShowFormatToInt(GeogCoordShowFormat::CharDegr2), 21);
    EXPECT_EQ(geogCoordShowFormatToInt(GeogCoordShowFormat::SignDegrMinSec), 3);
    EXPECT_EQ(geogCoordShowFormatToInt(GeogCoordShowFormat::SignDegrMin), 4);
    EXPECT_EQ(geogCoordShowFormatToInt(GeogCoordShowFormat::SignDegr), 5);
    EXPECT_EQ(geogCoordShowFormatToInt(GeogCoordShowFormat::SignDegr2), 51);

    for (const auto v :
         {GeogCoordShowFormat::CharDegrMinSec, GeogCoordShowFormat::CharDegrMin,
          GeogCoordShowFormat::CharDegr, GeogCoordShowFormat::CharDegr2, GeogCoordShowFormat::SignDegrMinSec,
          GeogCoordShowFormat::SignDegrMin, GeogCoordShowFormat::SignDegr, GeogCoordShowFormat::SignDegr2}) {
        EXPECT_EQ(intToGeogCoordShowFormat(geogCoordShowFormatToInt(v)), v);
    }
}

TEST(CoordRepresentation, UnknownGeogCoordShowFormatIdFallsBackToCharDegrMinSec) {
    EXPECT_EQ(intToGeogCoordShowFormat(7), GeogCoordShowFormat::CharDegrMinSec);
    EXPECT_EQ(intToGeogCoordShowFormat(-5), GeogCoordShowFormat::CharDegrMinSec);
}

TEST(CoordRepresentation, ProjCoordShowFormatIdIsTheOrdinal) {
    EXPECT_EQ(projCoordShowFormatToInt(ProjCoordShowFormat::RoundedToWhole), 0);
    EXPECT_EQ(projCoordShowFormatToInt(ProjCoordShowFormat::RoundedToThousandths), 3);
    EXPECT_EQ(intToProjCoordShowFormat(2), ProjCoordShowFormat::RoundedToHundredths);
    // Out-of-range ordinal: legacy's IntegerToProjCoordShowFormat also falls back to Low(..).
    EXPECT_EQ(intToProjCoordShowFormat(4), ProjCoordShowFormat::RoundedToWhole);
    EXPECT_EQ(intToProjCoordShowFormat(-1), ProjCoordShowFormat::RoundedToWhole);
}

TEST(CoordRepresentation, MgrsAndCoordSysInfoOrdinalsValidateInsteadOfUncheckedCast) {
    // Legacy casts these with an *unchecked* `TFoo(Integer)` (u_CoordRepresentationConfig.pas
    // :142,144); the port validates the range and falls back to the first enumerator instead --
    // a strict improvement with no effect on any valid file (spec §6.1, §10 D-7).
    EXPECT_EQ(mgrsCoordShowFormatToInt(MgrsCoordShowFormat::Splitted), 0);
    EXPECT_EQ(mgrsCoordShowFormatToInt(MgrsCoordShowFormat::Joined), 1);
    EXPECT_EQ(intToMgrsCoordShowFormat(1), MgrsCoordShowFormat::Joined);
    EXPECT_EQ(intToMgrsCoordShowFormat(5), MgrsCoordShowFormat::Splitted);

    EXPECT_EQ(coordSysInfoTypeToInt(CoordSysInfoType::ShowForAll), 2);
    EXPECT_EQ(intToCoordSysInfoType(1), CoordSysInfoType::ShowExceptWgs84);
    EXPECT_EQ(intToCoordSysInfoType(9), CoordSysInfoType::DontShow);
}

// --- CoordRepresentationSettings::read/write (u_CoordRepresentationConfig.pas:106-160) -----------

namespace {
std::shared_ptr<cg::core::IConfigWriter> makeSection(std::shared_ptr<IniFile>& file) {
    file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    return root->orCreateSubItem(u"CoordFormats");
}
} // namespace

TEST(CoordRepresentationSettings, DefaultsMatchLegacyConstructor) {
    // TCoordRepresentationConfig.Create (u_CoordRepresentationConfig.pas:106-115)
    const CoordRepresentationSettings s;
    EXPECT_TRUE(s.isLatitudeFirst);
    EXPECT_EQ(s.geogFormat, GeogCoordShowFormat::CharDegrMinSec);
    EXPECT_EQ(s.projFormat, ProjCoordShowFormat::RoundedToWhole);
    EXPECT_EQ(s.mgrsFormat, MgrsCoordShowFormat::Splitted);
    EXPECT_EQ(s.coordSysType, CoordSysType::Wgs84);
    EXPECT_EQ(s.coordSysInfoType, CoordSysInfoType::ShowExceptWgs84);
}

TEST(CoordRepresentationSettings, ReadOnAbsentSectionKeepsDefaults) {
    std::shared_ptr<IniFile> file;
    const auto section = makeSection(file);
    const CoordRepresentationSettings s = CoordRepresentationSettings::read(*section);
    EXPECT_EQ(s, CoordRepresentationSettings{});
}

TEST(CoordRepresentationSettings, ReadWriteRoundTripAllKeys) {
    std::shared_ptr<IniFile> file;
    const auto section = makeSection(file);
    section->writeBool(u"FirstLat", false);
    section->writeInt(u"DegrisShowFormat", 51); // the legacy-spelled key; SignDegr2
    section->writeInt(u"ProjCoordShowFormat", 2);
    section->writeInt(u"MgrsCoordShowFormat", 1);
    section->writeInt(u"CoordSysType", 12); // Gsk2011Gk
    section->writeInt(u"CoordSysInfoType", 2);

    const CoordRepresentationSettings s = CoordRepresentationSettings::read(*section);
    EXPECT_FALSE(s.isLatitudeFirst);
    EXPECT_EQ(s.geogFormat, GeogCoordShowFormat::SignDegr2);
    EXPECT_EQ(s.projFormat, ProjCoordShowFormat::RoundedToHundredths);
    EXPECT_EQ(s.mgrsFormat, MgrsCoordShowFormat::Joined);
    EXPECT_EQ(s.coordSysType, CoordSysType::Gsk2011Gk);
    EXPECT_EQ(s.coordSysInfoType, CoordSysInfoType::ShowForAll);

    auto outFile = std::make_shared<IniFile>();
    const auto outRoot = ConfigProviderIni::wrap(outFile);
    const auto outSection = outRoot->orCreateSubItem(u"CoordFormats");
    s.write(*outSection);

    EXPECT_FALSE(outSection->readBool(u"FirstLat", true));
    EXPECT_EQ(outSection->readInt(u"DegrisShowFormat", -1), 51);
    EXPECT_EQ(outSection->readInt(u"ProjCoordShowFormat", -1), 2);
    EXPECT_EQ(outSection->readInt(u"MgrsCoordShowFormat", -1), 1);
    EXPECT_EQ(outSection->readInt(u"CoordSysType", -1), 12);
    EXPECT_EQ(outSection->readInt(u"CoordSysInfoType", -1), 2);

    const CoordRepresentationSettings roundTripped = CoordRepresentationSettings::read(*outSection);
    EXPECT_EQ(roundTripped, s);
}

TEST(CoordRepresentationSettings, ReadValidatesOutOfRangeMgrsAndCoordSysInfoOrdinals) {
    // The port's strict-improvement case (§10 D-7): legacy's unchecked cast would be UB here.
    std::shared_ptr<IniFile> file;
    const auto section = makeSection(file);
    section->writeInt(u"MgrsCoordShowFormat", 7);
    section->writeInt(u"CoordSysInfoType", -3);

    const CoordRepresentationSettings s = CoordRepresentationSettings::read(*section);
    EXPECT_EQ(s.mgrsFormat, MgrsCoordShowFormat::Splitted);
    EXPECT_EQ(s.coordSysInfoType, CoordSysInfoType::DontShow);
}
