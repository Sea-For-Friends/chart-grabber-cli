// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for DownloadSession::save / load (Src/RegionProcess/Download/u_DownloadSession.pas, Save :230-284,
// Load :286-478 with _CheckZoom :293-304 and _ReadZoom :305-324). T-0165, RD-4.
// FEAT-REG-017 (save) and FEAT-REG-019 (load): contribute only (docs/parity/held-open.md).
//
// No legacy-written .sls file exists on this box (spec region-download.md 8.3, OQ-7). Every INI text that is
// LOADED below is written inline from the key rules of the Pascal, NOT produced by save. Only the round-trip
// tests go through save. All expectations are literals taken from the Pascal.
//
// Time zone convention: TileDate is a naive local wall clock (storage/tile_date.h). save writes
// checkTileDate().date(), the date of that wall clock, without any zone conversion; load builds that date at
// 00:00 on the same naive clock. Both directions agree, so no zone is involved.
//
// processedFromLastSuccessfulPoint has no getter. Two observations are used:
//  - savedFromLast(): save() right after the call under test (save zeroes it when the two points are equal);
//  - fromLastViaSetter(): setLastProcessedPoint to a point that differs from the last successful one adds 1
//    (T-0164 setter), so the saved key minus 1 is the member.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/map_type_set.h"
#include "cg/maptype/map_version_factory_changeable.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/map_version_request_config.h"
#include "cg/net/global_download_config.h"
#include "cg/region/download_session.h"

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QUuid>

#include <gtest/gtest.h>

#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "region_fake_map_type.h"

using cg::core::ConfigProviderIni;
using cg::core::IniFile;
using cg::geo::GeometryLonLatFactory;
using cg::geo::IGeometryLonLatMultiPolygon;
using cg::geo::IGeometryLonLatPolygon;
using cg::geo::IGeometryLonLatSinglePolygon;
using cg::geo::LonLat;
using cg::geo::PointI;
using cg::region::DownloadSession;
using cg::region::PolygonPtr;
using cg::region::test::FakeMapType;
using cg::storage::TileDate;

namespace {

using Pts = std::vector<LonLat>;
using Single = std::vector<Pts>;   // outer, then the holes
using Shape = std::vector<Single>; // one entry per single polygon

const QString kGuidText = QStringLiteral("{0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9}");
const QString kOtherGuidText = QStringLiteral("{AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE}");

const Pts kOuter{{10.5, 20.25}, {30.5, 40.25}, {50.5, 60.25}};
const Pts kHole1{{11.5, 21.25}, {12.5, 22.25}, {13.5, 23.25}};
const Pts kHole2{{14.5, 24.25}, {15.5, 25.25}, {16.5, 26.25}};

TileDate dt(int y, int m, int d, int h, int min) {
    return TileDate(QDate(y, m, d), QTime(h, min));
}

// --- INI text -------------------------------------------------------------------------------------------

using Lines = std::vector<std::pair<QString, QString>>;

Lines put(Lines lines, const QString& key, const QString& value) {
    for (auto& line : lines) {
        if (line.first == key) {
            line.second = value;
            return lines;
        }
    }
    lines.emplace_back(key, value);
    return lines;
}

Lines drop(Lines lines, const QString& key) {
    std::erase_if(lines, [&key](const auto& line) { return line.first == key; });
    return lines;
}

// The smallest file that loads: map GUID (lower case on purpose), Zoom 5 and a three-point polygon.
Lines minimal() {
    return {{"MapGUID", kGuidText.toLower()}, {"Zoom", "5"},          {"PointLon_0", "10.5"},
            {"PointLat_0", "20.25"},          {"PointLon_1", "30.5"}, {"PointLat_1", "40.25"},
            {"PointLon_2", "50.5"},           {"PointLat_2", "60.25"}};
}

// Every key of the table of spec 2.3.1 with a distinct non-default value.
Lines full() {
    Lines l = minimal();
    l = put(l, "VersionDownload", "dl-v7");
    l = put(l, "VersionCheck", "chk-v9");
    l = put(l, "VersionCheckOther", "1");
    l = put(l, "Zoom", "11");
    l = put(l, "ZoomArr", "9-12");
    l = put(l, "ReplaceExistTiles", "1");
    l = put(l, "CheckExistTileSize", "0");
    l = put(l, "CheckExistTileDate", "0");
    l = put(l, "CheckTileDate", "13.06.2024");
    l = put(l, "SecondLoadTNE", "1");
    l = put(l, "LoadTneOlderDate", "02.03.2021");
    l = put(l, "ProcessedTileCount", "701");
    l = put(l, "Processed", "500");
    l = put(l, "ProcessedFromLastSuccessfulPoint", "40");
    l = put(l, "LastProcessedCount", "480");
    l = put(l, "ProcessedSize", "12.5");
    l = put(l, "StartX", "1234");
    l = put(l, "StartY", "5678");
    l = put(l, "LastSuccessfulStartX", "1230");
    l = put(l, "LastSuccessfulStartY", "5670");
    l = put(l, "ElapsedTime", "1.25");
    l = put(l, "AutoCloseAtFinish", "1");
    l = put(l, "AutoSaveInterval", "15");
    l = put(l, "AutoSavePrefix", "Город-pre");
    l = put(l, "WorkersCount", "4");
    l = put(l, "WorkerIndex", "2");
    return l;
}

cg::core::IConfigProviderPtr sectionOf(const Lines& lines) {
    QByteArray bytes("\xEF\xBB\xBF[Session]\n");
    for (const auto& line : lines) {
        bytes += (line.first + QLatin1Char('=') + line.second + QLatin1Char('\n')).toUtf8();
    }
    auto parsed = IniFile::fromBytes(bytes);
    EXPECT_TRUE(parsed.has_value());
    auto root = ConfigProviderIni::wrap(std::make_shared<IniFile>(std::move(*parsed)));
    return root->subItem(u"Session");
}

// --- the world a session loads from ---------------------------------------------------------------------

cg::geo::ProjectionSetPtr googleProjectionSet() {
    static const auto datums = std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{});
    static const cg::geo::ProjectionSetFactory factory{cg::geo::HashSeed{}, datums};
    return factory.byCode(cg::geo::kProjEpsgGoogle, cg::geo::kTileSplitQuadrate256x256);
}

struct Rig {
    std::shared_ptr<cg::maptype::MapVersionFactoryChangeable> factory =
        std::make_shared<cg::maptype::MapVersionFactoryChangeable>(
            std::make_shared<cg::storage::MapVersionFactory>());
    std::shared_ptr<FakeMapType> map = std::make_shared<FakeMapType>();
    cg::maptype::IMapTypeSetPtr mapSet;
    cg::net::GlobalDownloadConfig config;
    GeometryLonLatFactory lonLat;

    Rig() {
        map->guidValue = QUuid::fromString(kGuidText);
        map->projectionSetValue = googleProjectionSet(); // zooms 0..23
        map->versionFactoryValue = factory;
        map->versionRequestValue = std::make_shared<cg::maptype::MapVersionRequestChangeable>(
            std::make_shared<cg::maptype::MapVersionRequestConfig>(QStringLiteral("base-ver")), factory);
        auto builder = cg::maptype::MapTypeSetBuilderFactory().build(false);
        builder->add(map);
        mapSet = builder->makeAndClear();
    }

    cg::storage::MapVersionInfoPtr version(const QString& text) const {
        return factory->getStatic()->byStoreString(text);
    }

    void load(DownloadSession& s, const Lines& lines, bool useLastSuccess) {
        config.setUseSessionLastSuccess(useLastSuccess);
        s.load(sectionOf(lines), *mapSet, config, lonLat);
    }
};

// A session whose every member is known and differs from anything the tests load.
DownloadSession prefilled(Rig& rig) {
    auto other = std::make_shared<FakeMapType>();
    other->guidValue = QUuid::fromString(kOtherGuidText);
    auto request = std::make_shared<const cg::storage::MapVersionRequest>(
        cg::storage::MapVersionRequest{rig.version(QStringLiteral("pre-ck")), true});
    DownloadSession s(other, request, rig.version(QStringLiteral("pre-dl")), 3, {1, 2, 3},
                      rig.lonLat.createPolygonByRect({1.0, 4.0, 3.0, 2.0}), true, dt(2001, 2, 3, 4, 5), true,
                      true, true, dt(2010, 11, 12, 13, 14), true, 31, QStringLiteral("pre"), 6, 5);
    s.setLastSuccessfulPoint({70, 71});
    s.setLastProcessedPoint({72, 73});
    s.setProcessed(900);
    s.setDownloadedCount(901);
    s.setDownloadedSize(902);
    s.setElapsedTime(0.5);
    return s;
}

// --- what save wrote ------------------------------------------------------------------------------------

struct Saved {
    std::shared_ptr<IniFile> file = std::make_shared<IniFile>();

    [[nodiscard]] QString get(const QString& key) const { return file->readString(u"Session", key); }
    [[nodiscard]] bool has(const QString& key) const { return file->hasKey(u"Session", key); }
    [[nodiscard]] QStringList keys() const { return file->keyNames(u"Session"); }
    [[nodiscard]] QStringList lines() const {
        QStringList out;
        for (const QString& key : keys()) {
            out << key + QLatin1Char('=') + get(key);
        }
        return out;
    }
};

Saved saveOf(DownloadSession& s) {
    Saved out;
    auto root = ConfigProviderIni::wrap(out.file);
    const auto section = root->orCreateSubItem(u"Session");
    s.save(*section);
    return out;
}

int savedFromLast(DownloadSession& s) {
    return saveOf(s).get(QStringLiteral("ProcessedFromLastSuccessfulPoint")).toInt();
}

// The member itself: a point that differs from every point the tests use adds exactly 1 (T-0164 setter).
int fromLastViaSetter(DownloadSession& s) {
    s.setLastProcessedPoint({-77, -78});
    return savedFromLast(s) - 1;
}

// --- polygons -------------------------------------------------------------------------------------------

cg::geo::LonLatPointsPtr ptr(Pts p) {
    return std::make_shared<const Pts>(std::move(p));
}

PolygonPtr makeSingle(const Pts& outer, const std::vector<Pts>& holes) {
    GeometryLonLatFactory factory;
    auto b = factory.makePolygonBuilder();
    b.addOuter(ptr(outer));
    for (const Pts& h : holes) {
        b.addHole(ptr(h));
    }
    return std::dynamic_pointer_cast<const IGeometryLonLatPolygon>(b.makeStaticAndClear());
}

Shape shapeOf(const PolygonPtr& polygon) {
    auto single = [](const IGeometryLonLatSinglePolygon& s) {
        Single out;
        const auto o = s.outerBorder()->points();
        out.emplace_back(o.begin(), o.end());
        for (std::size_t h = 0; h < s.holeCount(); ++h) {
            const auto p = s.holeBorder(h)->points();
            out.emplace_back(p.begin(), p.end());
        }
        return out;
    };
    Shape shape;
    if (const auto s = std::dynamic_pointer_cast<const IGeometryLonLatSinglePolygon>(polygon)) {
        shape.push_back(single(*s));
    } else if (const auto m = std::dynamic_pointer_cast<const IGeometryLonLatMultiPolygon>(polygon)) {
        for (std::size_t i = 0; i < m->count(); ++i) {
            shape.push_back(single(*m->item(i)));
        }
    }
    return shape;
}

// --- the session the save tests start from --------------------------------------------------------------

struct Flags {
    bool replaceExist;
    bool checkSize;
    bool checkDate;
    bool secondLoadTne;
    bool autoClose;
};

// Five bools, each with its own 3-bit code over the three rows, so a swap of any two is seen in one row.
const Flags kFlagRows[3] = {
    {true, false, false, true, true}, {false, true, false, true, false}, {false, false, true, false, true}};

DownloadSession makeSession(Rig& rig, const Flags& f, const PolygonPtr& polygon,
                            const TileDate& replaceTneOlderDate = dt(2021, 3, 2, 17, 45)) {
    auto request = std::make_shared<const cg::storage::MapVersionRequest>(
        cg::storage::MapVersionRequest{rig.version(QStringLiteral("chk-v9")), true});
    DownloadSession s(rig.map, request, rig.version(QStringLiteral("dl-v7")), 10, {0, 1, 2, 4}, polygon,
                      f.secondLoadTne, replaceTneOlderDate, f.replaceExist, f.checkSize, f.checkDate,
                      dt(2024, 6, 13, 22, 30), f.autoClose, 15, QStringLiteral("Город-pre"), 4, 2);
    s.setLastSuccessfulPoint({1230, 5670});
    // three calls with a point that differs from the last successful one: fromLast 3, lastProcessedCount 3
    s.setLastProcessedPoint({1, 1});
    s.setLastProcessedPoint({2, 2});
    s.setLastProcessedPoint({1234, 5678});
    s.setProcessed(500);
    s.setDownloadedCount(701);
    s.setDownloadedSize(12800);
    s.setElapsedTime(1.25);
    return s;
}

// A saveable session with the constructor's initial points (-1, -1) and nothing set by setters.
DownloadSession bare(Rig& rig) {
    return DownloadSession(rig.map, nullptr, rig.version(QStringLiteral("dl-v7")), 10, {10},
                           makeSingle(kOuter, {}), false, TileDate(), false, false, false,
                           dt(2024, 6, 13, 1, 0), false, 0, QString(), 1, 0);
}

QString messageOf(const std::function<void()>& call) {
    try {
        call();
    } catch (const std::runtime_error& e) {
        return QString::fromUtf8(e.what());
    } catch (...) {
        return QStringLiteral("<other exception>");
    }
    return QStringLiteral("<no exception>");
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Proof 1: load
// ---------------------------------------------------------------------------------------------------------

// Load :356-440 with every key present and the rollback off: each member equals its value.
TEST(DownloadSessionSls, LoadEveryKeyOfTheTable) {
    Rig rig;
    DownloadSession s;
    rig.load(s, full(), false);

    EXPECT_EQ(s.mapType(), rig.map);
    ASSERT_NE(s.versionForDownload(), nullptr);
    EXPECT_EQ(s.versionForDownload()->storeString(), QStringLiteral("dl-v7"));
    ASSERT_NE(s.versionForCheck(), nullptr);
    ASSERT_NE(s.versionForCheck()->baseVersion, nullptr);
    EXPECT_EQ(s.versionForCheck()->baseVersion->storeString(), QStringLiteral("chk-v9"));
    EXPECT_TRUE(s.versionForCheck()->showOtherVersions);
    EXPECT_EQ(s.zoom(), 10); // file value 11, 1-based
    EXPECT_EQ(s.zoomArr(), (std::vector<quint8>{8, 9, 10, 11}));
    EXPECT_TRUE(s.replaceExistTiles());
    EXPECT_FALSE(s.checkExistTileSize());
    EXPECT_FALSE(s.checkExistTileDate());
    EXPECT_EQ(s.checkTileDate(), dt(2024, 6, 13, 0, 0)); // the date at 00:00
    EXPECT_TRUE(s.secondLoadTne());
    EXPECT_EQ(s.replaceTneOlderDate(), dt(2021, 3, 2, 0, 0));
    EXPECT_EQ(s.downloadedCount(), 701);
    EXPECT_EQ(s.processed(), 500);
    EXPECT_EQ(s.lastProcessedCount(), 480);
    EXPECT_EQ(s.downloadedSize(), 12800u); // 12.5 KiB
    EXPECT_EQ(s.lastProcessedPoint(), (PointI{1234, 5678}));
    EXPECT_EQ(s.lastSuccessfulPoint(), (PointI{1230, 5670}));
    EXPECT_EQ(s.elapsedTime(), 1.25);
    EXPECT_TRUE(s.autoCloseAtFinish());
    EXPECT_EQ(s.autosaveInterval(), 15);
    EXPECT_EQ(s.autosavePrefix(), QStringLiteral("Город-pre"));
    EXPECT_EQ(s.workersCount(), 4);
    EXPECT_EQ(s.workerIndex(), 2);
    EXPECT_EQ(shapeOf(s.polygon()), (Shape{{kOuter}}));
    EXPECT_EQ(fromLastViaSetter(s), 40);
}

// Load :402-418 and :420-424 with only the keys the file cannot do without: every default of step 7.
TEST(DownloadSessionSls, LoadDefaultsOfAbsentKeys) {
    Rig rig;
    DownloadSession s;
    rig.load(s, minimal(), false);

    EXPECT_EQ(s.versionForDownload(), rig.map->versionRequestValue->getStatic()->baseVersion);
    EXPECT_EQ(s.zoom(), 4);
    EXPECT_EQ(s.zoomArr(), (std::vector<quint8>{4}));
    EXPECT_FALSE(s.replaceExistTiles());
    EXPECT_FALSE(s.checkExistTileSize());
    EXPECT_FALSE(s.checkExistTileDate());
    EXPECT_FALSE(s.secondLoadTne());
    EXPECT_FALSE(s.autoCloseAtFinish());
    EXPECT_FALSE(s.replaceTneOlderDate().isValid()); // NaN
    EXPECT_EQ(s.processed(), 0);
    EXPECT_EQ(s.downloadedCount(), 0);
    EXPECT_EQ(s.lastProcessedCount(), 0);
    EXPECT_EQ(s.downloadedSize(), 0u);
    EXPECT_EQ(s.elapsedTime(), 0.0);
    EXPECT_EQ(s.autosaveInterval(), 0);
    EXPECT_EQ(s.autosavePrefix(), QString());
    EXPECT_EQ(s.workersCount(), 1);
    EXPECT_EQ(s.workerIndex(), 0);
    EXPECT_EQ(s.lastProcessedPoint(), (PointI{-1, -1}));
    EXPECT_EQ(s.lastSuccessfulPoint(), (PointI{-1, -1}));
    EXPECT_EQ(fromLastViaSetter(s), 0);
}

// Load :405 ReadDate('CheckTileDate', Now): an absent or unparsable key keeps Now WITH its time of day
// (ConfigData/u_ConfigDataProviderByIniFileSection.pas:165-184); spec 2.3.1 step 7 ("at 00:00") is wrong,
// ticket finding a. Bound: one minute (fails only within a minute of local midnight).
TEST(DownloadSessionSls, LoadAbsentOrUnparsableCheckTileDateIsNowWithTimeOfDay) {
    Rig rig;
    for (const Lines& lines : {minimal(), put(minimal(), "CheckTileDate", "garbage")}) {
        DownloadSession s;
        rig.load(s, lines, false);
        ASSERT_TRUE(s.checkTileDate().isValid());
        EXPECT_LE(std::abs(s.checkTileDate().secsTo(cg::storage::tileDateNow())), 60);
        EXPECT_EQ(s.checkTileDate().date(), cg::storage::tileDateNow().date());
    }
}

// Load :402-404, :411, :414: each bool key reaches its own member.
TEST(DownloadSessionSls, LoadBoolKeysReachTheirOwnMember) {
    Rig rig;
    for (const Flags& f : kFlagRows) {
        Lines l = minimal();
        l = put(l, "ReplaceExistTiles", f.replaceExist ? "1" : "0");
        l = put(l, "CheckExistTileSize", f.checkSize ? "1" : "0");
        l = put(l, "CheckExistTileDate", f.checkDate ? "1" : "0");
        l = put(l, "SecondLoadTNE", f.secondLoadTne ? "1" : "0");
        l = put(l, "AutoCloseAtFinish", f.autoClose ? "1" : "0");
        DownloadSession s;
        rig.load(s, l, false);
        EXPECT_EQ(s.replaceExistTiles(), f.replaceExist);
        EXPECT_EQ(s.checkExistTileSize(), f.checkSize);
        EXPECT_EQ(s.checkExistTileDate(), f.checkDate);
        EXPECT_EQ(s.secondLoadTne(), f.secondLoadTne);
        EXPECT_EQ(s.autoCloseAtFinish(), f.autoClose);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Proof 2: error texts, order of the checks, the session stays untouched
// ---------------------------------------------------------------------------------------------------------

TEST(DownloadSessionSls, LoadErrorsKeepTheTextOrderAndTheSession) {
    struct Row {
        const char* name;
        Lines lines;
        QString message;
    };
    const QString unknownGuid = QStringLiteral("{FFFFFFFF-0000-0000-0000-000000000001}");
    const Lines noPolygon =
        drop(drop(drop(drop(drop(drop(minimal(), "PointLon_0"), "PointLat_0"), "PointLon_1"), "PointLat_1"),
                  "PointLon_2"),
             "PointLat_2");
    // Rows with two defects name the one that must win: GUID before map, map before zoom, zoom before
    // polygon.
    const Row rows[] = {
        {"guid absent, zoom and polygon also bad", drop(drop(noPolygon, "MapGUID"), "Zoom"),
         QStringLiteral("Map GUID is empty")},
        {"guid empty text", put(minimal(), "MapGUID", ""), QStringLiteral("Map GUID is empty")},
        {"guid not a guid", put(minimal(), "MapGUID", "not-a-guid"), QStringLiteral("Map GUID is empty")},
        {"guid unknown, zoom and polygon also bad",
         put(drop(noPolygon, "Zoom"), "MapGUID", unknownGuid.toLower()),
         QStringLiteral("Map with GUID = {FFFFFFFF-0000-0000-0000-000000000001} not found")},
        {"zoom absent, polygon also bad", drop(noPolygon, "Zoom"), QStringLiteral("Unknown zoom: 0")},
        {"zoom 0", put(minimal(), "Zoom", "0"), QStringLiteral("Unknown zoom: 0")},
        {"zoom above the projection set", put(minimal(), "Zoom", "25"), QStringLiteral("Unknown zoom: 24")},
        {"zoom narrowed to a byte", put(minimal(), "Zoom", "300"), QStringLiteral("Unknown zoom: 43")},
        {"array element above the set", put(put(minimal(), "Zoom", "7"), "ZoomArr", "5,26"),
         QStringLiteral("Unknown zoom: 25")},
        {"zoom bad and array bad: zoom wins", put(put(minimal(), "Zoom", "25"), "ZoomArr", "27"),
         QStringLiteral("Unknown zoom: 24")},
        {"array element 0", put(put(minimal(), "Zoom", "7"), "ZoomArr", "0,5"),
         QStringLiteral("Unknown zoom: 0")},
        {"polygon absent", noPolygon, QStringLiteral("Empty polygon")},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        Rig rig;
        DownloadSession s = prefilled(rig);
        const auto before = saveOf(s).lines();
        const auto mapBefore = s.mapType();
        const auto checkBefore = s.versionForCheck();
        const auto polygonBefore = s.polygon();
        EXPECT_EQ(messageOf([&] { rig.load(s, row.lines, true); }), row.message);
        EXPECT_EQ(saveOf(s).lines(), before);
        EXPECT_EQ(s.mapType(), mapBefore);
        EXPECT_EQ(s.versionForCheck(), checkBefore);
        EXPECT_EQ(s.polygon(), polygonBefore);
    }
}

TEST(DownloadSessionSls, LoadNullSectionThrowsAndKeepsTheSession) {
    Rig rig;
    DownloadSession s = prefilled(rig);
    const auto before = saveOf(s).lines();
    EXPECT_EQ(messageOf([&] { s.load(nullptr, *rig.mapSet, rig.config, rig.lonLat); }),
              QStringLiteral("No SLS data"));
    EXPECT_EQ(saveOf(s).lines(), before);
}

// ---------------------------------------------------------------------------------------------------------
// Proof 3: zoom rules (:305-324)
// ---------------------------------------------------------------------------------------------------------

TEST(DownloadSessionSls, LoadZoomAndZoomArrRules) {
    struct Row {
        const char* name;
        Lines lines;
        quint8 zoom;
        std::vector<quint8> arr;
    };
    const Row rows[] = {
        {"ZoomArr absent: the zoom alone", put(minimal(), "Zoom", "7"), 6, {6}},
        {"ZoomArr unparsable: the zoom alone", put(put(minimal(), "Zoom", "7"), "ZoomArr", "abc"), 6, {6}},
        {"Zoom is not a member of ZoomArr: loads",
         put(put(minimal(), "Zoom", "7"), "ZoomArr", "1-3"),
         6,
         {0, 1, 2}},
        {"every element is decremented", put(put(minimal(), "Zoom", "2"), "ZoomArr", "1,3,10"), 1, {0, 2, 9}},
        {"the last zoom of the set", put(put(minimal(), "Zoom", "24"), "ZoomArr", "24"), 23, {23}},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        Rig rig;
        DownloadSession s;
        rig.load(s, row.lines, false);
        EXPECT_EQ(s.zoom(), row.zoom);
        EXPECT_EQ(s.zoomArr(), row.arr);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Proof 4: version fields (:373-398)
// ---------------------------------------------------------------------------------------------------------

TEST(DownloadSessionSls, LoadVersionFields) {
    struct Row {
        const char* name;
        const char* download; // "" = key absent
        const char* check;
        const char* other;
        const char* expectedDownload;
        const char* expectedCheck;
        bool expectedOther;
    };
    const Row rows[] = {
        {"download text, check empty, other on", "dl-v7", "", "1", "dl-v7", "dl-v7", true},
        {"download empty, check text, other off", "", "chk-v9", "0", "base-ver", "chk-v9", false},
        {"both texts, other on", "dl-v7", "chk-v9", "1", "dl-v7", "chk-v9", true},
        {"both empty, other absent", "", "", "", "base-ver", "base-ver", false},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        Rig rig;
        Lines l = minimal();
        if (*row.download) {
            l = put(l, "VersionDownload", row.download);
        }
        if (*row.check) {
            l = put(l, "VersionCheck", row.check);
        }
        if (*row.other) {
            l = put(l, "VersionCheckOther", row.other);
        }
        DownloadSession s;
        rig.load(s, l, false);
        ASSERT_NE(s.versionForDownload(), nullptr);
        EXPECT_EQ(s.versionForDownload()->storeString(), QString::fromLatin1(row.expectedDownload));
        ASSERT_NE(s.versionForCheck(), nullptr);
        ASSERT_NE(s.versionForCheck()->baseVersion, nullptr);
        EXPECT_EQ(s.versionForCheck()->baseVersion->storeString(), QString::fromLatin1(row.expectedCheck));
        EXPECT_EQ(s.versionForCheck()->showOtherVersions, row.expectedOther);
        // the objects come from the map's factory (interned) or are the map's own base version
        if (*row.download) {
            EXPECT_EQ(s.versionForDownload(), rig.version(QString::fromLatin1(row.download)));
        } else {
            EXPECT_EQ(s.versionForDownload(), rig.map->versionRequestValue->getStatic()->baseVersion);
        }
        if (!*row.check) {
            EXPECT_EQ(s.versionForCheck()->baseVersion, s.versionForDownload()); // the same object
        }
    }
}

// ---------------------------------------------------------------------------------------------------------
// Proof 5: rollback to the last successful point (:426-440)
// ---------------------------------------------------------------------------------------------------------

TEST(DownloadSessionSls, LoadRollbackRows) {
    struct Row {
        const char* name;
        bool useLastSuccess;
        const char* processed;
        const char* fromLast;
        const char* lastCount;
        bool equalPoints;
        qint64 expectedProcessed;
        qint64 expectedLastCount;
        int expectedFromLast; // the member, seen through the setter
        PointI expectedProcessedPoint;
    };
    const PointI last{1230, 5670};
    const PointI start{1234, 5678};
    const Row rows[] = {
        {"on, both counters, points differ", true, "500", "40", "480", false, 460, 440, 0, last},
        {"on, fromLast zero: nothing to subtract", true, "500", "0", "480", false, 500, 480, 0, last},
        {"on, processed zero: nothing to subtract, fromLast kept", true, "0", "40", "480", false, 0, 480, 40,
         last},
        {"on, equal points: step 8 zeroes fromLast first", true, "500", "40", "480", true, 500, 480, 0, last},
        {"off, points differ: nothing changes", false, "500", "40", "480", false, 500, 480, 40, start},
        {"off, equal points: step 8", false, "500", "40", "480", true, 500, 480, 0, last},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        Rig rig;
        Lines l = minimal();
        l = put(l, "Processed", row.processed);
        l = put(l, "ProcessedFromLastSuccessfulPoint", row.fromLast);
        l = put(l, "LastProcessedCount", row.lastCount);
        l = put(l, "LastSuccessfulStartX", "1230");
        l = put(l, "LastSuccessfulStartY", "5670");
        l = put(l, "StartX", row.equalPoints ? "1230" : "1234");
        l = put(l, "StartY", row.equalPoints ? "5670" : "5678");
        DownloadSession s;
        rig.load(s, l, row.useLastSuccess);
        EXPECT_EQ(s.processed(), row.expectedProcessed);
        EXPECT_EQ(s.lastProcessedCount(), row.expectedLastCount);
        EXPECT_EQ(s.lastProcessedPoint(), row.expectedProcessedPoint);
        EXPECT_EQ(s.lastSuccessfulPoint(), last);
        EXPECT_EQ(fromLastViaSetter(s), row.expectedFromLast);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Proof 7: processedFromLastSuccessfulPoint bookkeeping (T-0164 setters; save :247-249)
// ---------------------------------------------------------------------------------------------------------

// setLastProcessedPoint: a point that differs from the last successful one adds 1, an equal one zeroes.
TEST(DownloadSessionSls, FromLastSetLastProcessedPoint) {
    Rig rig;
    DownloadSession s = bare(rig);
    s.setLastSuccessfulPoint({1, 2});
    s.setLastProcessedPoint({3, 4});
    s.setLastProcessedPoint({5, 6});
    EXPECT_EQ(savedFromLast(s), 2);
    s.setLastProcessedPoint({1, 2}); // equal: zero
    s.setLastProcessedPoint({3, 4});
    EXPECT_EQ(savedFromLast(s), 1);
}

// setLastSuccessfulPoint: to the last processed point zeroes; to any other point it keeps the value.
TEST(DownloadSessionSls, FromLastSetLastSuccessfulPoint) {
    Rig rig;
    DownloadSession s = bare(rig);
    s.setLastSuccessfulPoint({1, 2});
    s.setLastProcessedPoint({3, 4});
    s.setLastProcessedPoint({5, 6});
    s.setLastSuccessfulPoint({7, 8}); // another point: kept
    EXPECT_EQ(savedFromLast(s), 2);
    s.setLastSuccessfulPoint({5, 6}); // the last processed point: zero
    EXPECT_EQ(fromLastViaSetter(s), 0);
}

// setZoom: another zoom zeroes it (and resets the points); the same zoom keeps it.
TEST(DownloadSessionSls, FromLastSetZoom) {
    Rig rig;
    DownloadSession s = bare(rig);
    s.setLastSuccessfulPoint({1, 2});
    s.setLastProcessedPoint({3, 4});
    s.setLastProcessedPoint({5, 6});
    s.setZoom(10); // the same zoom as the constructor's
    EXPECT_EQ(savedFromLast(s), 2);
    s.setZoom(9);
    EXPECT_EQ(fromLastViaSetter(s), 0);
}

// Save :247-249: equal points write 0 whatever the member holds. Reached through a load that leaves the
// member at 40 with equal points (rollback on, processed 0: nothing is subtracted, the member is kept).
TEST(DownloadSessionSls, SaveZeroesFromLastWhenThePointsAreEqual) {
    Rig rig;
    Lines l = minimal();
    l = put(l, "Processed", "0");
    l = put(l, "ProcessedFromLastSuccessfulPoint", "40");
    l = put(l, "LastSuccessfulStartX", "1230");
    l = put(l, "LastSuccessfulStartY", "5670");
    l = put(l, "StartX", "1234");
    l = put(l, "StartY", "5678");
    {
        DownloadSession s;
        rig.load(s, l, true);
        ASSERT_EQ(s.lastProcessedPoint(), s.lastSuccessfulPoint());
        EXPECT_EQ(savedFromLast(s), 0);
    }
    {
        DownloadSession s; // the same load, the member observed without a save first
        rig.load(s, l, true);
        EXPECT_EQ(fromLastViaSetter(s), 40);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Proof 6: save
// ---------------------------------------------------------------------------------------------------------

TEST(DownloadSessionSls, SaveWritesTheKeysOfTheTableInOrder) {
    Rig rig;
    DownloadSession s = makeSession(rig, kFlagRows[0], makeSingle(kOuter, {}));
    const Saved saved = saveOf(s);
    const QStringList expected{
        "MapGUID={0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9}",
        "VersionDownload=dl-v7",
        "VersionCheck=chk-v9",
        "VersionCheckOther=1",
        "Zoom=11", // 1-based
        "ZoomArr=1-3,5",
        "ReplaceExistTiles=1",
        "CheckExistTileSize=0",
        "CheckExistTileDate=0",
        "CheckTileDate=13.06.2024", // the date only
        "SecondLoadTNE=1",
        "LoadTneOlderDate=02.03.2021",
        "ProcessedTileCount=701",
        "Processed=500",
        "ProcessedFromLastSuccessfulPoint=3",
        "LastProcessedCount=3",
        "ProcessedSize=12.5", // KiB
        "StartX=1234",
        "StartY=5678",
        "LastSuccessfulStartX=1230",
        "LastSuccessfulStartY=5670",
        "ElapsedTime=1.25",
        "AutoCloseAtFinish=1",
        "AutoSaveInterval=15",
        "AutoSavePrefix=Город-pre",
        "WorkersCount=4",
        "WorkerIndex=2",
    };
    const QStringList lines = saved.lines();
    ASSERT_GE(lines.size(), expected.size());
    EXPECT_EQ(lines.mid(0, expected.size()), expected);
    // the polygon keys follow: PointLon_N / PointLat_N, three points
    QStringList polygonKeys = saved.keys().mid(static_cast<qsizetype>(expected.size()));
    polygonKeys.sort();
    EXPECT_EQ(polygonKeys, (QStringList{"PointLat_0", "PointLat_1", "PointLat_2", "PointLon_0", "PointLon_1",
                                        "PointLon_2"}));
    EXPECT_EQ(saved.get(QStringLiteral("PointLon_1")), QStringLiteral("30.5"));
    EXPECT_EQ(saved.get(QStringLiteral("PointLat_2")), QStringLiteral("60.25"));
}

TEST(DownloadSessionSls, SaveBoolKeysComeFromTheirOwnMember) {
    Rig rig;
    for (const Flags& f : kFlagRows) {
        DownloadSession s = makeSession(rig, f, makeSingle(kOuter, {}));
        const Saved saved = saveOf(s);
        EXPECT_EQ(saved.get(QStringLiteral("ReplaceExistTiles")), f.replaceExist ? "1" : "0");
        EXPECT_EQ(saved.get(QStringLiteral("CheckExistTileSize")), f.checkSize ? "1" : "0");
        EXPECT_EQ(saved.get(QStringLiteral("CheckExistTileDate")), f.checkDate ? "1" : "0");
        EXPECT_EQ(saved.get(QStringLiteral("SecondLoadTNE")), f.secondLoadTne ? "1" : "0");
        EXPECT_EQ(saved.get(QStringLiteral("AutoCloseAtFinish")), f.autoClose ? "1" : "0");
    }
}

// Save :263-265: LoadTneOlderDate is written only when the date is valid.
TEST(DownloadSessionSls, SaveLeavesOutLoadTneOlderDateWhenInvalid) {
    Rig rig;
    DownloadSession s = makeSession(rig, kFlagRows[0], makeSingle(kOuter, {}), TileDate());
    const Saved saved = saveOf(s);
    EXPECT_FALSE(saved.has(QStringLiteral("LoadTneOlderDate")));
    EXPECT_TRUE(saved.has(QStringLiteral("SecondLoadTNE")));
    EXPECT_TRUE(saved.has(QStringLiteral("ProcessedTileCount"))); // the key after it is still written
}

// Save :240-245: VersionCheck is "" for a request without base version and for no request at all.
TEST(DownloadSessionSls, SaveVersionCheckEmptyWithoutBaseVersionOrRequest) {
    Rig rig;
    const auto noBase =
        std::make_shared<const cg::storage::MapVersionRequest>(cg::storage::MapVersionRequest{nullptr, true});
    DownloadSession withRequest(rig.map, noBase, rig.version(QStringLiteral("dl-v7")), 10, {10},
                                makeSingle(kOuter, {}), false, TileDate(), false, false, false,
                                dt(2024, 6, 13, 1, 0), false, 0, QString(), 1, 0);
    const Saved a = saveOf(withRequest);
    EXPECT_TRUE(a.has(QStringLiteral("VersionCheck")));
    EXPECT_EQ(a.get(QStringLiteral("VersionCheck")), QString());
    EXPECT_EQ(a.get(QStringLiteral("VersionCheckOther")), QStringLiteral("1"));
    EXPECT_EQ(a.get(QStringLiteral("VersionDownload")), QStringLiteral("dl-v7"));

    DownloadSession noRequest(rig.map, nullptr, rig.version(QStringLiteral("dl-v7")), 10, {10},
                              makeSingle(kOuter, {}), false, TileDate(), false, false, false,
                              dt(2024, 6, 13, 1, 0), false, 0, QString(), 1, 0);
    const Saved b = saveOf(noRequest);
    EXPECT_TRUE(b.has(QStringLiteral("VersionCheck")));
    EXPECT_EQ(b.get(QStringLiteral("VersionCheck")), QString());
    EXPECT_EQ(b.get(QStringLiteral("VersionCheckOther")), QStringLiteral("0"));
}

// Save :267-270: Int64 into WriteInteger(Integer), range checks off: the low 32 bits. ProcessedSize is a
// float and is not narrowed.
TEST(DownloadSessionSls, SaveNarrowsTheCountersTo32Bits) {
    Rig rig;
    DownloadSession s = makeSession(rig, kFlagRows[0], makeSingle(kOuter, {}));
    s.setDownloadedCount(2147483653); // 2^31 + 5
    s.setProcessed(4294967303);       // 2^32 + 7
    s.setDownloadedSize(6442450944);  // 6 GiB
    const Saved saved = saveOf(s);
    EXPECT_EQ(saved.get(QStringLiteral("ProcessedTileCount")), QStringLiteral("-2147483643"));
    EXPECT_EQ(saved.get(QStringLiteral("Processed")), QStringLiteral("7"));
    EXPECT_EQ(saved.get(QStringLiteral("ProcessedSize")), QStringLiteral("6291456"));
}

// ---------------------------------------------------------------------------------------------------------
// Proof 8: round trip
// ---------------------------------------------------------------------------------------------------------

// load(save(x)) gives x for every member except the time of day of the two dates, which the file loses.
TEST(DownloadSessionSls, RoundTripKeepsEveryMemberButTheTimeOfDay) {
    Rig rig;
    DownloadSession x = makeSession(rig, kFlagRows[2], makeSingle(kOuter, {kHole1}));
    const Saved saved = saveOf(x);

    Rig target;
    DownloadSession y;
    // load reads the file text; feed it the saved keys
    Lines lines;
    for (const QString& key : saved.keys()) {
        lines.emplace_back(key, saved.get(key));
    }
    target.load(y, lines, false);

    EXPECT_EQ(y.mapType(), target.map);
    EXPECT_EQ(y.versionForDownload()->storeString(), QStringLiteral("dl-v7"));
    EXPECT_EQ(y.versionForCheck()->baseVersion->storeString(), QStringLiteral("chk-v9"));
    EXPECT_TRUE(y.versionForCheck()->showOtherVersions);
    EXPECT_EQ(y.zoom(), x.zoom());
    EXPECT_EQ(y.zoomArr(), (std::vector<quint8>{0, 1, 2, 4}));
    EXPECT_EQ(y.replaceExistTiles(), x.replaceExistTiles());
    EXPECT_EQ(y.checkExistTileSize(), x.checkExistTileSize());
    EXPECT_EQ(y.checkExistTileDate(), x.checkExistTileDate());
    EXPECT_EQ(y.secondLoadTne(), x.secondLoadTne());
    EXPECT_EQ(y.checkTileDate(), dt(2024, 6, 13, 0, 0));      // x had 22:30
    EXPECT_EQ(y.replaceTneOlderDate(), dt(2021, 3, 2, 0, 0)); // x had 17:45
    EXPECT_EQ(y.downloadedCount(), 701);
    EXPECT_EQ(y.processed(), 500);
    EXPECT_EQ(y.lastProcessedCount(), 3);
    EXPECT_EQ(y.downloadedSize(), 12800u);
    EXPECT_EQ(y.lastProcessedPoint(), (PointI{1234, 5678}));
    EXPECT_EQ(y.lastSuccessfulPoint(), (PointI{1230, 5670}));
    EXPECT_EQ(y.elapsedTime(), 1.25);
    EXPECT_EQ(y.autoCloseAtFinish(), x.autoCloseAtFinish());
    EXPECT_EQ(y.autosaveInterval(), 15);
    EXPECT_EQ(y.autosavePrefix(), QStringLiteral("Город-pre"));
    EXPECT_EQ(y.workersCount(), 4);
    EXPECT_EQ(y.workerIndex(), 2);
    EXPECT_EQ(shapeOf(y.polygon()), (Shape{{kOuter, kHole1}})); // one hole survives
    EXPECT_EQ(fromLastViaSetter(y), 3);
}

// T-0272 (L-260 fixed, DEVIATION D-158; legacy ConfigData/u_ConfigProviderHelpers.pas :363-365 wrote the
// second hole at a wrong index): the file reads back as outer + both holes.
TEST(DownloadSessionSls, RoundTripKeepsTheSecondHole) {
    Rig rig;
    DownloadSession x = makeSession(rig, kFlagRows[0], makeSingle(kOuter, {kHole1, kHole2}));
    const Saved saved = saveOf(x);
    Lines lines;
    for (const QString& key : saved.keys()) {
        lines.emplace_back(key, saved.get(key));
    }
    DownloadSession y;
    rig.load(y, lines, false);
    EXPECT_EQ(shapeOf(y.polygon()), (Shape{{kOuter, kHole1, kHole2}}));
}
