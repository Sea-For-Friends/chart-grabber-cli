// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-001 (contributes; held open).
// Ports u_ZmpFileNamesIteratorFactory.pas (+ the three Src/Basic iterators), u_ZmpInfoSet.pas,
// i_ZmpInfoSet.pas; docs/specs/maptype.md 2.3, 2.4, 4, 7.2, 7.4, 8.2 (LD-11). Fixtures: the 514-ZMP stock
// corpus tests/fixtures/zmp/sas.maps with Maps.ini (written by legacy), index.csv, projection_expected.csv
// and corpus_aggregates.csv (tools/gen_zmp_corpus_aggregates.py); the hand-made duplicate/, hidden/, broken/
// and zipped/ folders (their READMEs name the source).
#include "cg/core/ini_file.h"
#include "cg/core/language_manager.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/zmp_file_names_iterator_factory.h"
#include "cg/maptype/zmp_info_set.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "maptype_test_util.h"

using namespace Qt::StringLiterals;
using cg::core::ContentTypeRegistry;
using cg::core::LanguageManager;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::ProjectionSetFactory;
using cg::maptype::IZmpInfoPtr;
using cg::maptype::ZmpConfig;
using cg::maptype::zmpFileNames;
using cg::maptype::ZmpInfoSet;
using cg::maptype::test::ensureGuiApp;
using cg::maptype::test::fixtureDir;

namespace {

constexpr int kEn = 0, kRu = 4; // en + the stock .mo names in name order: es fa fr ru tr uk

QString zmpDir() {
    return fixtureDir() + u"/zmp"_s;
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    return f.readAll();
}

void writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
}

using Env = cg::maptype::test::ZmpEnv; // the loader shared with download_checker_stuped_test.cpp

std::vector<QStringList> csvRows(const QString& path) {
    std::vector<QStringList> rows;
    const QStringList lines = QString::fromUtf8(readFile(path)).split(u'\n', Qt::SkipEmptyParts);
    for (qsizetype i = 1; i < lines.size(); ++i) { // the header is skipped
        rows.push_back(lines[i].trimmed().split(u','));
    }
    return rows;
}

// --- zmpFileNames: the walk rules of spec 2.3 ---------------------------------------------------------

TEST(ZmpFileNames, WalkRules) {
    QTemporaryDir tmp;
    const QString root = tmp.path();
    const QStringList kept = {u"a.zmp"_s,
                              u"b.ZMP"_s,
                              u".dot.zmp"_s,
                              u"sub/c.zmp"_s,
                              u"l1/m.zmp"_s,
                              u"l1/l2/m.zmp"_s,
                              u"l1/l2/l3/m.zmp"_s,
                              u"l1/l2/l3/l4/m.zmp"_s,
                              u"l1/l2/l3/l4/l5/m.zmp"_s,
                              u"l1/l2/l3/l4/l5/l6/m.zmp"_s};
    for (const QString& name : kept) {
        if (name == u"a.zmp"_s) {
            ASSERT_TRUE(QDir().mkpath(root + u"/a.zmp/inner"_s)); // a ZMP folder: yielded, never entered
        } else {
            writeFile(root + u'/' + name, {});
        }
    }
    writeFile(root + u"/a.zmp/inner/x.zmp"_s, {});          // inside a ZMP folder
    writeFile(root + u"/.hid/y.zmp"_s, {});                 // inside a dot folder
    writeFile(root + u"/l1/l2/l3/l4/l5/l6/l7/m.zmp"_s, {}); // the eighth level: past the limit
    writeFile(root + u"/sub/notzmp.txt"_s, {});
    QStringList got = zmpFileNames(root);
    QStringList want = kept;
    got.sort();
    want.sort();
    EXPECT_EQ(got, want);
}

// --- the corpus proof, spec 7.2 --------------------------------------------------------------------

TEST(ZmpInfoSetCorpus, EveryStockZmpLoadsAndMatchesTheOracles) {
    CG_SKIP_IF_NO_CORPUS();
    const Env env;
    const QString root = zmpDir() + u"/sas.maps"_s;
    const QStringList names = zmpFileNames(root);
    ASSERT_EQ(names.size(), 514);
    QStringList errors;
    const auto set = env.load(root, names, errors);
    // 1. everything loads
    EXPECT_TRUE(errors.isEmpty()) << errors.join(u"\n"_s).toStdString();
    ASSERT_EQ(set->guids().size(), 514u);
    EXPECT_EQ(QSet<QUuid>(set->guids().begin(), set->guids().end()).size(), 514);

    // 2. identity and names against the Maps.ini written by legacy
    auto mapsIni = cg::core::IniFile::fromFile(zmpDir() + u"/Maps.ini"_s);
    ASSERT_TRUE(mapsIni.has_value());
    QSet<QString> loaded;
    for (const QUuid& guid : set->guids()) {
        const QString section = guid.toString(QUuid::WithBraces).toUpper();
        loaded.insert(section);
        const auto zmp = set->zmpByGuid(guid);
        ASSERT_TRUE(zmp != nullptr);
        EXPECT_EQ(zmp->gui()->name()->getString(kEn), mapsIni->readString(section, u"MapName_", u"<none>"_s))
            << section.toStdString();
    }
    const QStringList sectionList = mapsIni->sectionNames();
    EXPECT_EQ(loaded, QSet<QString>(sectionList.begin(), sectionList.end()));
    EXPECT_EQ(sectionList.size(), 514);

    // the single CP1251 file decodes its Cyrillic name
    const auto beeline =
        std::ranges::find_if(names, [](const QString& n) { return n.endsWith(u"/Beeline_IoT.zmp"_s); });
    ASSERT_NE(beeline, names.end());
    for (const QUuid& guid : set->guids()) {
        if (set->zmpByGuid(guid)->fileName() == *beeline) {
            EXPECT_EQ(set->zmpByGuid(guid)->gui()->name()->getString(kRu), u"Билайн - IoT"_s);
        }
    }

    // 3. projection
    QHash<QString, IZmpInfoPtr> byName;
    for (const QUuid& guid : set->guids()) {
        byName.insert(set->zmpByGuid(guid)->fileName(), set->zmpByGuid(guid));
    }
    for (const QStringList& row : csvRows(zmpDir() + u"/projection_expected.csv"_s)) {
        const auto zmp = byName.value(row[0]);
        ASSERT_TRUE(zmp != nullptr) << row[0].toStdString();
        const auto type = zmp->projectionSet()->zoom(0)->projectionType();
        EXPECT_EQ(type->projectionEpsg(), row[2].toInt()) << row[0].toStdString();
        EXPECT_EQ(type->datum()->epsg(), row[3].toInt()) << row[0].toStdString();
    }

    // 4. script presence
    int withScript = 0;
    for (const QStringList& row : csvRows(zmpDir() + u"/index.csv"_s)) {
        const auto zmp = byName.value(QString(row[0]).remove(u'"'));
        ASSERT_TRUE(zmp != nullptr) << row[0].toStdString();
        const bool has = !zmp->dataProvider()->binary(u"GetUrlScript.txt").isEmpty();
        EXPECT_EQ(has, row[1] == u"yes"_s) << row[0].toStdString();
        withScript += has;
    }
    EXPECT_EQ(withScript, 350);

    // 5. typed fields against the independent scan
    QHash<QString, int> exts;
    QHash<int, int> cacheTypes, maxConn;
    int detectMime = 0, useDwnOff = 0, inScript = 0, subRect88 = 0, memCache = 0, banSamples = 0;
    int rows = 0, layers = 0, separators = 0, viewInfos = 0, emptySamples = 0, own18 = 0, infos = 0;
    for (const QStringList& r : csvRows(zmpDir() + u"/corpus_aggregates.csv"_s)) {
        ASSERT_EQ(r.size(), 18) << r.join(u',').toStdString();
        const auto zmp = byName.value(r[0]);
        ASSERT_TRUE(zmp != nullptr) << r[0].toStdString();
        const std::string n = r[0].toStdString();
        const auto storage = zmp->storageConfig();
        const auto downloader = zmp->tileDownloaderConfig();
        EXPECT_EQ(zmp->guid(), QUuid::fromString(r[1])) << n;
        EXPECT_EQ(zmp->isLayer(), r[2] == u"1"_s) << n;
        EXPECT_EQ(QString::fromLatin1(storage->tileFileExt), r[3]) << n;
        EXPECT_EQ(static_cast<int>(storage->cacheType), r[4].toInt()) << n;
        EXPECT_EQ(downloader->detectMimeType, r[5] == u"1"_s) << n;
        EXPECT_EQ(downloader->enabled, r[6] == u"1"_s) << n;
        EXPECT_EQ(zmp->abilities()->useDownload, r[6] == u"1"_s) << n;
        EXPECT_EQ(zmp->tileDownloadRequestBuilderConfig()->isUseDownloader, r[7] == u"1"_s) << n;
        EXPECT_EQ(downloader->maxConnectToServerCount, r[8].toUInt()) << n;
        EXPECT_EQ(downloader->iteratorSubRectSize, QPoint(r[9].toInt(), r[10].toInt())) << n;
        EXPECT_EQ(storage->useMemCache, r[11] == u"1"_s) << n;
        EXPECT_EQ(zmp->gui()->separator(), r[12] == u"1"_s) << n;
        const int viewEpsg = r[13].toInt();
        if (viewEpsg != 0) {
            EXPECT_TRUE(zmp->viewProjectionSet()->isSame(*env.projections.byCode(viewEpsg, 1))) << n;
            ++viewInfos;
        } else {
            EXPECT_TRUE(zmp->viewProjectionSet()->isSame(*zmp->projectionSet())) << n;
        }
        EXPECT_EQ(zmp->emptyTileSamples().size(), static_cast<size_t>(r[14].toInt())) << n;
        EXPECT_EQ(zmp->banTileSamples().size(), static_cast<size_t>(r[15].toInt())) << n;
        const bool has18 = r[16] == u"1"_s;
        if (has18) { // read from its own file: the size is the one in the BMP header (width at 18, height at
                     // 22)
            const QByteArray bmp = readFile(root + u'/' + r[0] + u"/18.bmp"_s);
            ASSERT_GE(bmp.size(), 26) << n; // the header read below reaches byte 25
            const QByteArrayView hdr = QByteArrayView(bmp).sliced(18, 8);
            const QSize size(qFromLittleEndian<qint32>(hdr.data()),
                             std::abs(qFromLittleEndian<qint32>(hdr.data() + 4)));
            EXPECT_EQ(zmp->gui()->bmp18().size(), size) << n;
        } else {
            EXPECT_EQ(zmp->gui()->bmp18(), zmp->gui()->bmp24()) << n;
        }
        EXPECT_EQ(!zmp->gui()->infoUrl()->getString(kEn).isEmpty(), r[17] == u"1"_s) << n;
        ++rows;
        ++exts[QString::fromLatin1(storage->tileFileExt)];
        ++cacheTypes[static_cast<int>(storage->cacheType)];
        ++maxConn[static_cast<int>(downloader->maxConnectToServerCount)];
        detectMime += downloader->detectMimeType;
        useDwnOff += !downloader->enabled;
        inScript += zmp->tileDownloadRequestBuilderConfig()->isUseDownloader;
        subRect88 += downloader->iteratorSubRectSize == QPoint(8, 8);
        memCache += storage->useMemCache;
        banSamples += !zmp->banTileSamples().empty();
        layers += zmp->isLayer();
        separators += zmp->gui()->separator();
        emptySamples += !zmp->emptyTileSamples().empty();
        own18 += has18;
        infos += r[17] == u"1"_s;
    }
    EXPECT_EQ(rows, 514);
    EXPECT_EQ(exts[u".png"_s], 339);
    EXPECT_EQ(exts[u".jpg"_s], 156);
    EXPECT_EQ(exts[u".gif"_s], 17);
    EXPECT_EQ(exts[u".kml"_s], 2);
    EXPECT_EQ(cacheTypes[9], 14);
    EXPECT_EQ(cacheTypes[5], 4);
    EXPECT_EQ(cacheTypes[8], 1);
    EXPECT_EQ(cacheTypes[0], 495);
    EXPECT_EQ(detectMime, 145);
    EXPECT_EQ(useDwnOff, 5);
    EXPECT_EQ(inScript, 45);
    EXPECT_EQ(maxConn[1], 65);
    EXPECT_EQ(maxConn[6], 1);
    EXPECT_EQ(maxConn[4], 448);
    EXPECT_EQ(subRect88, 14);
    EXPECT_EQ(memCache, 514);
    EXPECT_EQ(banSamples, 0);
    EXPECT_EQ(layers, 190);
    EXPECT_EQ(separators, 15);
    EXPECT_EQ(viewInfos, 6);
    EXPECT_EQ(emptySamples, 35);
    EXPECT_EQ(own18, 444);
    EXPECT_EQ(infos, 361);
}

// --- spec 7.4 fixtures ----------------------------------------------------------------------------------

TEST(ZmpInfoSet, DuplicateGuidKeepsTheFirstAndNamesTheSecondByFullPath) {
    CG_SKIP_IF_NO_FIXTURE(zmpDir() + u"/duplicate"_s);
    const Env env;
    QStringList errors;
    const QString root = zmpDir() + u"/duplicate"_s;
    const auto set = env.loadAll(root, errors);
    ASSERT_EQ(set->guids().size(), 1u);
    EXPECT_EQ(set->zmpByGuid(set->guids()[0])->fileName(), u"a.zmp"_s);
    // LD-11: the first is relative, the second is the full path
    EXPECT_EQ(errors,
              QStringList{u"Files a.zmp and %1 have the same GUID"_s.arg(QDir(root).filePath(u"b.zmp"_s))});
}

TEST(ZmpInfoSet, HiddenNamesAreDroppedAfterTheWalk) {
    CG_SKIP_IF_NO_FIXTURE(zmpDir() + u"/hidden"_s);
    const Env env;
    QStringList errors;
    const QString root = zmpDir() + u"/hidden"_s;
    QStringList walked = zmpFileNames(root);
    walked.sort();
    EXPECT_EQ(walked,
              (QStringList{u".hidden.zmp"_s, u"visible.zmp"_s})); // the walk yields it, IsHiddenZmp drops it
    const auto set = env.loadAll(root, errors);
    EXPECT_TRUE(errors.isEmpty());
    ASSERT_EQ(set->guids().size(), 1u);
    EXPECT_EQ(set->zmpByGuid(set->guids()[0])->fileName(), u"visible.zmp"_s);
    EXPECT_EQ(set->zmpByGuid(QUuid::fromString(u"{D643C5A1-3333-4387-9BE2-DDDF185C117F}"_s)), nullptr);
}

TEST(ZmpInfoSet, RejectedZmpsAreReportedAndDoNotConsumeAPnum) {
    CG_SKIP_IF_NO_CORPUS();
    const Env env;
    QTemporaryDir tmp;
    writeFile(tmp.filePath(u"z.zmp/params.txt"_s),
              "[PARAMS]\r\nGUID={11111111-2222-3333-4444-555555555555}\r\n");
    writeFile(tmp.filePath(u"z.zmp/24.bmp"_s), readFile(zmpDir() + u"/sas.maps/_osm/4umaps.eu.zmp/24.bmp"_s));
    QStringList errors;
    // the set sorts the names: the five broken ones come before z.zmp, so z.zmp is the first to load
    const QStringList names = {u"z.zmp"_s,
                               u"broken/no_guid.zmp"_s,
                               u"broken/no_params.zmp"_s,
                               u"broken/no_params_section.zmp"_s,
                               u"broken/bad_iterator_one_item.zmp"_s,
                               u"broken/unknown_ext.zmp"_s};
    for (const char* n :
         {"no_guid", "no_params", "no_params_section", "bad_iterator_one_item", "unknown_ext"}) {
        const QString src = zmpDir() + u"/broken/"_s + QLatin1String(n) + u".zmp"_s;
        for (const QString& f : QDir(src).entryList(QDir::Files)) {
            writeFile(tmp.filePath(u"broken/"_s + QLatin1String(n) + u".zmp/"_s + f),
                      readFile(src + u'/' + f));
        }
    }
    const auto set = env.load(tmp.path(), names, errors);
    EXPECT_EQ(
        errors,
        (QStringList{
            u"List index out of bounds (1)"_s, u"There is an error Empty GUID in map broken/no_guid.zmp"_s,
            u"There is an error Not found \"params.txt\" in zmp in map broken/no_params.zmp"_s,
            u"There is an error Not found PARAMS section in zmp in map broken/no_params_section.zmp"_s,
            u"Unknown tile content type \"\" / extension \".xyz\" in map broken/unknown_ext.zmp"_s}));
    ASSERT_EQ(set->guids().size(), 1u);
    EXPECT_EQ(set->zmpByGuid(set->guids()[0])->gui()->name()->getString(kEn), u"map#0"_s);
}

TEST(ZmpInfoSet, NamesAreSortedNaturallyWithTheLegacyBackslashKey) {
    CG_SKIP_IF_NO_CORPUS();
    const Env env;
    QTemporaryDir tmp;
    const QByteArray icon = readFile(zmpDir() + u"/sas.maps/_osm/4umaps.eu.zmp/24.bmp"_s);
    const QStringList names = {u"m10.zmp"_s, u"a/b.zmp"_s, u"m2.zmp"_s, u"a;b.zmp"_s};
    int n = 1;
    for (const QString& name : names) { // each ZMP has a GUID and no name: the loaded name is map#<pnum>
        writeFile(tmp.filePath(name + u"/params.txt"_s),
                  "[PARAMS]\r\nGUID={11111111-2222-3333-4444-55555555555" + QByteArray::number(n++) +
                      "}\r\n");
        writeFile(tmp.filePath(name + u"/24.bmp"_s), icon);
    }
    QStringList errors;
    const auto set = env.load(tmp.path(), names, errors);
    EXPECT_TRUE(errors.isEmpty());
    // '/' is compared as '\\' (0x5C), so ';' (0x3B) sorts before it; m2 before m10 (digit runs are numbers)
    const QStringList want = {u"a;b.zmp"_s, u"a/b.zmp"_s, u"m2.zmp"_s, u"m10.zmp"_s};
    ASSERT_EQ(set->guids().size(), 4u);
    for (size_t i = 0; i < 4; ++i) {
        const auto zmp = set->zmpByGuid(set->guids()[i]);
        EXPECT_EQ(zmp->fileName(), want[static_cast<qsizetype>(i)]);
        EXPECT_EQ(zmp->gui()->name()->getString(kEn), u"map#%1"_s.arg(i));
    }
}

// --- the zip branch ----------------------------------------------------------------------------------------

TEST(ZmpInfoSet, ZippedZmpLoadsLikeAFolderAndItsTwinIsADuplicate) {
    CG_SKIP_IF_NO_FIXTURE(zmpDir() + u"/zipped_nodwn/Gps_tiles_nodwn.zmp"_s);
    const Env env;
    QStringList errors;
    const QString root = zmpDir() + u"/zipped"_s;
    const auto set = env.loadAll(root, errors);
    ASSERT_EQ(set->guids().size(), 1u);
    const auto zmp = set->zmpByGuid(set->guids()[0]);
    EXPECT_EQ(zmp->fileName(), u"Gps_tiles_deflated.zmp"_s);
    EXPECT_FALSE(zmp->dataProvider()->binary(u"GetUrlScript.txt").isEmpty());
    EXPECT_EQ(errors, QStringList{u"Files Gps_tiles_deflated.zmp and %1 have the same GUID"_s.arg(
                          QDir(root).filePath(u"Gps_tiles_stored.zmp"_s))});
}

// UseDwn=0: legacy never reads the script of that map at start-up, so neither does the set.
TEST(ZmpInfoSet, ZippedZmpWithDownloaderOffLoadsDespiteADamagedScript) {
    CG_SKIP_IF_NO_CORPUS();
    const Env env;
    QTemporaryDir tmp;
    QByteArray zip = readFile(zmpDir() + u"/zipped_nodwn/Gps_tiles_nodwn.zmp"_s);
    const qsizetype script =
        zip.indexOf(readFile(zmpDir() + u"/sas.maps/_osm/Gps_tiles.zmp/GetUrlScript.txt"_s));
    ASSERT_GT(script, 0);
    zip[script + 5] = static_cast<char>(zip[script + 5] ^ 0x5A);
    writeFile(tmp.filePath(u"x.zmp"_s), zip);
    QStringList errors;
    const auto set = env.loadAll(tmp.path(), errors);
    EXPECT_TRUE(errors.isEmpty()) << errors.join(u"\n"_s).toStdString();
    EXPECT_EQ(set->guids().size(), 1u);
}

TEST(ZmpInfoSet, DamagedZipsAreDroppedAndReported) {
    CG_SKIP_IF_NO_CORPUS();
    const Env env;
    const QByteArray good = readFile(zmpDir() + u"/zipped/Gps_tiles_stored.zmp"_s);
    QTemporaryDir tmp;
    writeFile(tmp.filePath(u"notzip.zmp"_s), "this is not a zip file");
    // a flipped byte inside the stored GetUrlScript.txt: ZmpInfo does not read the script, so only the
    // check after reading the script catches it
    QByteArray scriptDamaged = good;
    const qsizetype script =
        scriptDamaged.indexOf(readFile(zmpDir() + u"/sas.maps/_osm/Gps_tiles.zmp/GetUrlScript.txt"_s));
    ASSERT_GT(script, 0);
    scriptDamaged[script + 5] = static_cast<char>(scriptDamaged[script + 5] ^ 0x5A);
    writeFile(tmp.filePath(u"script.zmp"_s), scriptDamaged);
    // a flipped run inside the first member (24.bmp, read by ZmpInfo): caught by the check after the load
    QByteArray iconDamaged = readFile(zmpDir() + u"/zipped/Gps_tiles_deflated.zmp"_s);
    for (qsizetype i = 40; i < 90; ++i) {
        iconDamaged[i] = static_cast<char>(iconDamaged[i] ^ 0x5A);
    }
    writeFile(tmp.filePath(u"icon.zmp"_s), iconDamaged);
    QStringList errors;
    const auto set = env.loadAll(tmp.path(), errors);
    EXPECT_TRUE(set->guids().empty());
    ASSERT_EQ(errors.size(), 3);
    EXPECT_TRUE(errors[0].contains(u"24.bmp"_s))
        << errors[0].toStdString(); // icon.zmp (sorted: icon, notzip, script)
    EXPECT_TRUE(errors[2].contains(u"GetUrlScript.txt"_s)) << errors[2].toStdString(); // script.zmp
}

} // namespace
