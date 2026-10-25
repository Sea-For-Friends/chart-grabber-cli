// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/job_checks (new code; inherits fr_ExportMBTiles.pas:550-573 (texts), :375-418 (via
// region::directTilesCopy); fr_TilesDownload.pas:428-431 (via region::allowDownload)). No FEAT tag (T-0189:
// Features none).
#include "cg/app/app_context.h"
#include "cg/app/job_checks.h"
#include "cg/core/content_type.h"
#include "cg/core/path_config.h"
#include "cg/core/platform/test_support.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/zmp_info.h"
#include "cg/region/export_mbtiles.h"
#include "cg/storage/ram_tile_storage_type.h"
#include "cg/storage/tile_storage_of_map_type.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUuid>

#include <gtest/gtest.h>

#include <chrono>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unistd.h>
#include <vector>

#include "../maptype/maptype_test_util.h"

using cg::app::AppContext;
using cg::app::AppOptions;
using cg::app::CliError;
using cg::app::Command;
using cg::app::findSource;
using cg::app::Job;
using cg::app::refusalForDownload;
using cg::app::refusalForExport;
using cg::app::resolveJob;
using cg::maptype::IMapTypePtr;

using namespace Qt::StringLiterals;

namespace {

// The ZMP fixtures. Every map is a template-only ZMP folder (RAM cache, CacheType=9), as
// tests/app/app_context_test.cpp writes them. `guid(n)` is the n-th GUID.
enum Fx : int {
    Png = 1,
    JpgLayer,
    StoredWebp,
    Bmp,
    BmpGeo,
    Geo,
    Kml,
    Crop,
    Cut,
    NoDownload,
    ReadOnly,
    Apostrophe,
    Same1,
    Same2,
    FileNamedAsMap,
    FileNamedAsGuid,
    FileNamedAsUnknownGuid,
    DupFileLower,
    DupFileUpper,
    BingLike,
    EsriLike,
    ResponseWebp,
    Licensed,
    LicensedLayer,
    LicenseApostrophe,
    MaxTwo,
    MaxTwoLayer,
    MaxBad24,
    MaxBadX,
    Count = MaxBadX
};

QString guid(int n) {
    return QStringLiteral("{AB0000%1-0000-0000-0000-0000000000%1}").arg(n, 2, 16, QLatin1Char('0')).toUpper();
}
QString bare(int n) {
    QString s = guid(n);
    return s.mid(1, s.size() - 2);
}

struct Spec {
    int n;
    QString folder; // the relative file name of the ZMP
    QString name;
    QByteArray contentType;
    QByteArray ext;
    QByteArray extra; // more params.txt lines, each ending in CRLF
};

QList<Spec> specs() {
    const QByteArray png = "image/png", jpg = "image/jpg";
    return {
        {Png, u"png.zmp"_s, u"Plain PNG"_s, png, ".png", ""},
        {JpgLayer, u"jpglayer.zmp"_s, u"Город Слой"_s, jpg, ".jpg", "asLayer=1\r\n"},
        // stored as WebP although the response list says PNG
        {StoredWebp, u"webp.zmp"_s, u"WebP Map"_s, png, ".webp", ""},
        {Bmp, u"bmp.zmp"_s, u"Bmp Map"_s, "image/bmp", ".bmp", ""},
        {BmpGeo, u"bmpgeo.zmp"_s, u"Bmp Geo"_s, "image/bmp", ".bmp", "projection=3\r\n"},
        {Geo, u"geo.zmp"_s, u"Geo Map"_s, png, ".png", "projection=3\r\n"},
        {Kml, u"kml.zmp"_s, u"Vector Map"_s, "application/vnd.google-earth.kml+xml", ".kml",
         "projection=3\r\n"},
        {Crop, u"crop.zmp"_s, u"Crop Map"_s, png, ".png", "TileRRight=100\r\nTileRBottom=100\r\n"},
        {Cut, u"cut.zmp"_s, u"Cut Map"_s, png, ".png", "CutSizeX=100\r\n"},
        {NoDownload, u"nodl.zmp"_s, u"NoDl Map"_s, png, ".png", "UseDwn=0\r\n"},
        {ReadOnly, u"ro.zmp"_s, u"ReadOnly Map"_s, png, ".png", "IsReadOnly=1\r\n"},
        {Apostrophe, u"bob.zmp"_s, u"Bob's Map"_s, png, ".png", ""},
        {Same1, u"dup1.zmp"_s, u"Same Name"_s, png, ".png", ""},
        {Same2, u"dup2.zmp"_s, u"Same Name"_s, png, ".png", ""},
        // rule 2 against rule 3: the file stem equals the name of the map Png
        {FileNamedAsMap, u"Plain PNG.zmp"_s, u"Other File"_s, png, ".png", ""},
        // rule 1 against rule 2: the file stem is the GUID of the map Png
        {FileNamedAsGuid, QString(bare(Png).toLower() + u".zmp"_s), u"Alias A"_s, png, ".png", ""},
        // a valid GUID that no map has, as the stem of a file
        {FileNamedAsUnknownGuid, QStringLiteral("ab0000ff-0000-0000-0000-0000000000ff.zmp"), u"Alias B"_s,
         png, ".png", ""},
        {DupFileLower, u"dupfile.zmp"_s, u"Dup Lower"_s, png, ".png", ""},
        {DupFileUpper, u"DupFile.zmp"_s, u"Dup Upper"_s, png, ".png", ""},
        // response lists of Bing_Sat / Esri: the tiles are stored as PNG / JPEG
        {BingLike, u"bing.zmp"_s, u"Bing Like"_s, "image/jpeg; image/png", ".png", ""},
        {EsriLike, u"esri.zmp"_s, u"Esri Like"_s, "image/jpeg; image/png", ".jpg", ""},
        // the response list names WebP, the tiles are stored as PNG
        {ResponseWebp, u"respwebp.zmp"_s, u"Response WebP"_s, "image/webp", ".png", ""},
        // T-0222: the License text of the ZMP
        {Licensed, u"lic.zmp"_s, u"Lic Map"_s, png, ".png", "License=Лицензия CC-BY-SA\r\n"},
        {LicensedLayer, u"liclayer.zmp"_s, u"Lic Layer"_s, jpg, ".jpg",
         "asLayer=1\r\nLicense=Credit EMODnet\r\n"},
        {LicenseApostrophe, u"licapos.zmp"_s, u"Lic Apos"_s, png, ".png", "License=Joe's data\r\n"},
        // T-0245: MaxZoom (0-based)
        {MaxTwo, u"max2.zmp"_s, u"Max Two"_s, png, ".png", "MaxZoom=2\r\n"},
        {MaxTwoLayer, u"max2layer.zmp"_s, u"Max Layer"_s, jpg, ".jpg", "asLayer=1\r\nMaxZoom=2\r\n"},
        {MaxBad24, u"max24.zmp"_s, u"Max 24"_s, png, ".png", "MaxZoom=24\r\n"},
        {MaxBadX, u"maxx.zmp"_s, u"Max X"_s, png, ".png", "MaxZoom=x\r\n"},
    };
}

void writeZmp(const QString& mapsDir, const Spec& s) {
    QByteArray text = "[PARAMS]\r\nGUID=" + guid(s.n).toLatin1() + "\r\nname=" + s.name.toUtf8() +
                      "\r\nNameInCache=" +
                      (s.extra.contains("NameInCache=") ? QByteArray() : "cache" + QByteArray::number(s.n)) +
                      "\r\nDefURLBase=http://127.0.0.1:9/{z}/{x}/{y}" + s.ext +
                      "\r\nContentType=" + s.contentType + "\r\nExt=" + s.ext +
                      "\r\nprojection=" + QByteArray::number(s.extra.contains("projection=3") ? 3 : 1) +
                      "\r\nsradiusa=6378137\r\nsradiusb=6356752\r\nCacheType=9\r\n";
    text +=
        QByteArray(s.extra).replace("projection=3\r\n", ""); // written above; a NameInCache here replaces it
    const QString dir = QDir(mapsDir).filePath(s.folder);
    ASSERT_TRUE(QDir().mkpath(dir));
    QFile f(dir + u"/params.txt"_s);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(text);
}

class JobChecksTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        cg::maptype::test::ensureGuiApp();
        root_ = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(root_->isValid());
        const QString maps = root_->filePath(u"Maps"_s);
        ASSERT_TRUE(QDir().mkpath(maps));
        // T-0340: "dupfile.zmp" and "DupFile.zmp" are one file on a case-insensitive folder (NTFS): the
        // second map is not written there, and the tests that need both are skipped
        caseSensitive_ = cg::core::platform::directoryIsCaseSensitive(maps);
        for (const Spec& s : specs()) {
            if (s.n == DupFileUpper && !caseSensitive_) {
                continue;
            }
            writeZmp(maps, s);
        }
        AppOptions o;
        o.arguments = {u"--datadir="_s + root_->filePath(u"data"_s)};
        o.mapsDirs = {maps};
        o.warn = [](const QString&) {};
        auto ctx = AppContext::create(o);
        ASSERT_TRUE(ctx.has_value());
        app_ = std::move(*ctx);
        ASSERT_TRUE(app_->loadMaps().has_value());
        ASSERT_EQ(app_->fullMapsSet()->count(), static_cast<int>(specs().size()) - (caseSensitive_ ? 0 : 1));
    }
    static void TearDownTestSuite() {
        app_.reset();
        root_.reset();
    }

    void SetUp() override { ASSERT_TRUE(out_.isValid()); }

    [[nodiscard]] IMapTypePtr map(int n) const {
        return app_->fullMapsSet()->mapTypeByGuid(QUuid::fromString(guid(n)));
    }
    [[nodiscard]] QString outDir() const { return QFileInfo(out_.path()).absoluteFilePath(); }
    [[nodiscard]] QString outFile(const QString& name) const { return outDir() + u'/' + name; }

    // An export of the given source into <out>/o.mbtiles, zoom 5.
    [[nodiscard]] Job exportJob(int source) const {
        Job j;
        j.command = Command::Export;
        j.source = bare(source);
        j.zooms = {5};
        j.out = outFile(u"o.mbtiles"_s);
        return j;
    }

    struct Outcome {
        std::expected<std::vector<cg::app::ResolvedSource>, CliError> result;
        QStringList warnings;
    };
    [[nodiscard]] Outcome resolve(const Job& job) const {
        Outcome o{{}, {}};
        o.result = resolveJob(job, *app_, [&o](const QString& w) { o.warnings.append(w); });
        return o;
    }
    // Exit code 3 and exactly this text.
    static void expectRefused(const Outcome& o, const QString& text) {
        ASSERT_FALSE(o.result.has_value());
        EXPECT_EQ(o.result.error().exitCode, 3);
        EXPECT_EQ(o.result.error().message.toStdString(), text.toStdString());
    }

    static bool caseSensitive_;
    static std::unique_ptr<QTemporaryDir> root_;
    static std::unique_ptr<AppContext> app_;
    QTemporaryDir out_;
};
bool JobChecksTest::caseSensitive_ = true;
std::unique_ptr<QTemporaryDir> JobChecksTest::root_;
std::unique_ptr<AppContext> JobChecksTest::app_;

} // namespace

// ---- Proof 1: findSource (app-cli.md 1.4) ----

TEST_F(JobChecksTest, RuleOneGuidWithBracesWithoutBracesAndLowerCase) {
    const auto maps = app_->fullMapsSet();
    for (const QString& id : {guid(JpgLayer), bare(JpgLayer), bare(JpgLayer).toLower()}) {
        const auto found = findSource(*maps, id);
        ASSERT_TRUE(found.has_value()) << id.toStdString();
        EXPECT_EQ((*found)->zmp()->guid(), QUuid::fromString(guid(JpgLayer))) << id.toStdString();
    }
}

TEST_F(JobChecksTest, RuleOneBeatsRuleTwo) {
    // The file of FileNamedAsGuid is called like the GUID of Png.
    const auto found = findSource(*app_->fullMapsSet(), bare(Png));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ((*found)->zmp()->guid(), QUuid::fromString(guid(Png)));
}

TEST_F(JobChecksTest, ValidUnknownGuidDoesNotFallThroughToTheFileName) {
    // A file ab0000ff-....zmp exists; its GUID is another one.
    const QString id = QStringLiteral("AB0000FF-0000-0000-0000-0000000000FF");
    const auto found = findSource(*app_->fullMapsSet(), id);
    ASSERT_FALSE(found.has_value());
    EXPECT_EQ(found.error().exitCode, 3);
    EXPECT_EQ(found.error().message,
              QStringLiteral("no map source matches \"%1\"; run \"chart-grabber-cli sources\"").arg(id));
}

TEST_F(JobChecksTest, RuleTwoFileNameWithAndWithoutExtensionAnyCase) {
    const auto maps = app_->fullMapsSet();
    for (const QString& id : {u"png"_s, u"png.zmp"_s, u"PNG.ZMP"_s, u"Png"_s}) {
        const auto found = findSource(*maps, id);
        ASSERT_TRUE(found.has_value()) << id.toStdString();
        EXPECT_EQ((*found)->zmp()->guid(), QUuid::fromString(guid(Png))) << id.toStdString();
    }
}

TEST_F(JobChecksTest, RuleTwoBeatsRuleThree) {
    // "plain png" is the file of FileNamedAsMap and the name of Png.
    const auto found = findSource(*app_->fullMapsSet(), u"plain png"_s);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ((*found)->zmp()->guid(), QUuid::fromString(guid(FileNamedAsMap)));
}

TEST_F(JobChecksTest, RuleThreeNameInAnotherCase) {
    const auto found = findSource(*app_->fullMapsSet(), u"ГОРОД СЛОЙ"_s);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ((*found)->zmp()->guid(), QUuid::fromString(guid(JpgLayer)));
}

TEST_F(JobChecksTest, AmbiguityUnderRuleTwoListsBothCandidates) {
    if (!caseSensitive_) {
        GTEST_SKIP() << "the maps folder is case-insensitive: dupfile.zmp and DupFile.zmp cannot both exist";
    }
    const auto found = findSource(*app_->fullMapsSet(), u"dupfile"_s);
    ASSERT_FALSE(found.has_value());
    EXPECT_EQ(found.error().exitCode, 3);
    const QStringList lines = found.error().message.split(u'\n');
    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines[0], u"\"dupfile\" matches more than one map source:"_s);
    const QStringList candidates{lines[1], lines[2]};
    EXPECT_TRUE(candidates.contains(u"  {AB000012-0000-0000-0000-000000000012}  Dup Lower  dupfile.zmp"_s))
        << found.error().message.toStdString();
    EXPECT_TRUE(candidates.contains(u"  {AB000013-0000-0000-0000-000000000013}  Dup Upper  DupFile.zmp"_s))
        << found.error().message.toStdString();
}

TEST_F(JobChecksTest, AmbiguityUnderRuleThreeListsBothCandidates) {
    const auto found = findSource(*app_->fullMapsSet(), u"same name"_s);
    ASSERT_FALSE(found.has_value());
    EXPECT_EQ(found.error().exitCode, 3);
    const QStringList lines = found.error().message.split(u'\n');
    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines[0], u"\"same name\" matches more than one map source:"_s);
    const QStringList candidates{lines[1], lines[2]};
    EXPECT_TRUE(candidates.contains(u"  {AB00000D-0000-0000-0000-00000000000D}  Same Name  dup1.zmp"_s))
        << found.error().message.toStdString();
    EXPECT_TRUE(candidates.contains(u"  {AB00000E-0000-0000-0000-00000000000E}  Same Name  dup2.zmp"_s))
        << found.error().message.toStdString();
}

TEST_F(JobChecksTest, NothingMatches) {
    const auto found = findSource(*app_->fullMapsSet(), u"nosuchmap"_s);
    ASSERT_FALSE(found.has_value());
    EXPECT_EQ(found.error().exitCode, 3);
    EXPECT_EQ(found.error().message,
              u"no map source matches \"nosuchmap\"; run \"chart-grabber-cli sources\""_s);
}

// ---- Proof 2: the refusals ----

// S-1: u_MapTypesMainList.pas:378-379 MapTypesNoMapsError; loadMaps returns NotFound, the set stays null.
TEST_F(JobChecksTest, S1NoMapLoaded) {
    AppOptions o;
    o.arguments = {u"--datadir="_s + outFile(u"data"_s)};
    o.mapsDirs = {outFile(u"EmptyMaps"_s)};
    ASSERT_TRUE(QDir().mkpath(o.mapsDirs.front()));
    auto empty = AppContext::create(o);
    ASSERT_TRUE(empty.has_value());
    ASSERT_FALSE((*empty)->loadMaps().has_value());
    Job j = exportJob(Png);
    const auto r = resolveJob(j, **empty, nullptr);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().exitCode, 3);
    EXPECT_EQ(r.error().message,
              QStringLiteral("no map source is loaded from \"%1\"").arg(o.mapsDirs.front()));
    j.command = Command::Sources; // commands without sources do not look at the maps
    EXPECT_TRUE(resolveJob(j, **empty, nullptr).has_value());
}

// S-1 over two --maps folders (T-0291): every folder is named, quoted, `, `-separated, in the order given.
TEST_F(JobChecksTest, S1NamesEveryMapsFolder) {
    AppOptions o;
    o.arguments = {u"--datadir="_s + outFile(u"data"_s)};
    o.mapsDirs = {outFile(u"EmptyA"_s), outFile(u"EmptyB"_s)};
    ASSERT_TRUE(QDir().mkpath(o.mapsDirs[0]));
    ASSERT_TRUE(QDir().mkpath(o.mapsDirs[1]));
    auto empty = AppContext::create(o);
    ASSERT_TRUE(empty.has_value());
    ASSERT_FALSE((*empty)->loadMaps().has_value());
    const auto r = resolveJob(exportJob(Png), **empty, nullptr);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().exitCode, 3);
    EXPECT_EQ(
        r.error().message,
        QStringLiteral("no map source is loaded from \"%1\", \"%2\"").arg(o.mapsDirs[0], o.mapsDirs[1]));
}

// S-3: fr_ExportMBTiles.pas:554-570 (the order: file, zoom, source).
TEST_F(JobChecksTest, S3TheThreeFrameTextsInTheirOrder) {
    Job j;
    j.command = Command::Export;
    expectRefused(resolve(j), u"Output file name is not set or incorrect!"_s);
    j.out = outFile(u"o.mbtiles"_s);
    expectRefused(resolve(j), u"Please select at least one zoom"_s);
    j.zooms = {3};
    expectRefused(resolve(j), u"Please select at least one map or overlay layer"_s);
    j.command = Command::Estimate; // an estimate writes no file: the file text does not apply
    j.out.clear();
    expectRefused(resolve(j), u"Please select at least one map or overlay layer"_s);
    j.zooms.clear();
    expectRefused(resolve(j), u"Please select at least one zoom"_s);
}

TEST_F(JobChecksTest, S4NotBitmapTiles) {
    // Vector Map is also EPSG:4326 and not PNG / JPEG / WebP: S-4 is reported first
    expectRefused(resolve(exportJob(Kml)), u"\"Vector Map\" does not hold bitmap tiles"_s);
    Job j = exportJob(Kml);
    j.source.clear();
    j.overlay = bare(Kml);
    expectRefused(resolve(j), u"\"Vector Map\" does not hold bitmap tiles"_s);
}

TEST_F(JobChecksTest, S5ProjectionNamesTheEpsg) {
    expectRefused(
        resolve(exportJob(Geo)),
        u"the projection of \"Geo Map\" is EPSG:4326; only EPSG:3857 (Web Mercator) can be exported"_s);
}

TEST_F(JobChecksTest, S6ContentTypeBmpAndItsOrderBeforeTheProjection) {
    const QString bmp = u"the tiles of \"Bmp Map\" are stored as \"image/bmp\"; only PNG, JPEG and WebP can "
                        u"be exported"_s;
    expectRefused(resolve(exportJob(Bmp)), bmp);
    // EPSG:4326 as well: the content type is reported first (GetDirectTilesCopy order)
    expectRefused(resolve(exportJob(BmpGeo)), QString(bmp).replace(u"Bmp Map"_s, u"Bmp Geo"_s));
}

TEST_F(JobChecksTest, S6WebpOnlyWithTheFlag) {
    const QString text = u"WebP tiles are not shown by every OpenCPN version; pass --allow-webp to export "
                         u"them anyway"_s;
    Job j = exportJob(StoredWebp);
    expectRefused(resolve(j), text);
    j.allowWebp = true;
    const Outcome ok = resolve(j);
    ASSERT_TRUE(ok.result.has_value()) << ok.result.error().message.toStdString();
    // the refusal comes before the zoom check
    j.allowWebp = false;
    j.zooms = {22};
    expectRefused(resolve(j), text);
}

// Tech Lead ruling: the judgement is by the type the tiles are stored with (Ext / storage content type), not
// by the response types of the ZMP.
TEST_F(JobChecksTest, FormatIsJudgedByTheStorageTypeNotByTheResponseList) {
    for (const int n : {BingLike, EsriLike, ResponseWebp}) {
        const Outcome o = resolve(exportJob(n));
        EXPECT_TRUE(o.result.has_value())
            << n << ": " << (o.result ? "" : o.result.error().message.toStdString());
    }
}

TEST_F(JobChecksTest, S7ZoomAbove21) {
    Job j = exportJob(Png);
    j.zooms = {0, 21};
    EXPECT_TRUE(resolve(j).result.has_value());
    j.zooms = {5, 22, 3}; // unsorted on purpose
    expectRefused(resolve(j), u"zoom 22 is above 21 (0-based)"_s);
    j.zooms = {23};
    expectRefused(resolve(j), u"zoom 23 is above 21 (0-based)"_s);
}

TEST_F(JobChecksTest, ExportRefusalsApplyToEveryCommandThatNamesASource) {
    for (const Command c : {Command::Estimate, Command::Download, Command::Run}) {
        Job j = exportJob(Png);
        j.command = c;
        j.zooms = {22};
        expectRefused(resolve(j), u"zoom 22 is above 21 (0-based)"_s);
        j = exportJob(Kml);
        j.command = c;
        expectRefused(resolve(j), u"\"Vector Map\" does not hold bitmap tiles"_s);
    }
}

// T-0275 (D row, L-44): no shipped backend fails its build, so the facade is built over a type that throws or
// answers an error, as tests/storage/tile_storage_of_map_type_test.cpp does.
namespace {
class FailingType final : public cg::storage::ITileStorageType {
public:
    explicit FailingType(bool throws) : throws_(throws) {}
    cg::storage::TileStorageTypeAbilities abilities() const override { return {}; }
    cg::core::PathConfigPtr basePath() const override { return nullptr; }
    cg::storage::MapVersionFactoryPtr mapVersionFactory() const override { return nullptr; }
    cg::core::Result<cg::storage::ITileStoragePtr>
    build(const cg::storage::TileStorageAbilities&, cg::geo::ProjectionSetPtr,
          const cg::core::ContentTypeInfo&, cg::storage::TilePyramidNotifierPtr, const QString&,
          std::shared_ptr<cg::storage::TileInfoMemCache>) override {
        if (throws_) {
            throw std::runtime_error("disk exploded");
        }
        return cg::core::fail(cg::core::ErrorCode::IoError, u"disk is full"_s);
    }

private:
    bool throws_;
};

struct VersionSink final : cg::storage::IMapVersionFactoryChangeableInternal {
    void setFactory(const cg::storage::MapVersionFactoryPtr&) override {}
};

std::unique_ptr<cg::storage::TileStorageOfMapType>
facadeOver(cg::storage::ITileStorageTypePtr type, const QString& base,
           std::shared_ptr<cg::storage::TileInfoMemCache> cache = nullptr) {
    using namespace cg::storage;
    static const auto datums = std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{});
    static const cg::geo::ProjectionSetFactory factory{cg::geo::HashSeed{}, datums};
    auto global = std::make_shared<GlobalCacheConfig>(cg::core::PathConfig::create(QString(), base, nullptr));
    global->setDefCache(CacheTypeId::Ram);
    auto list = std::make_shared<TileStorageTypeList>(
        std::vector<ITileStorageTypeListItemPtr>{std::make_shared<TileStorageTypeListItem>(
            QUuid(), static_cast<int>(CacheTypeId::Ram), u"t"_s, std::move(type), true, true)});
    auto config =
        std::make_shared<SimpleTileStorageConfig>(std::make_shared<const SimpleTileStorageConfigStatic>(
            SimpleTileStorageConfigStatic{CacheTypeId::Ram, u"Город"_s, QByteArray("image/png"),
                                          QByteArray(".png"), TileStorageAbilities::readWrite()}));
    return std::make_unique<TileStorageOfMapType>(
        global, factory.byCode(cg::geo::kProjEpsgGoogle, cg::geo::kTileSplitQuadrate256x256), list,
        std::make_shared<VersionSink>(), config, std::move(cache),
        cg::core::ContentTypeRegistry::standard().infoByType("image/png"));
}
} // namespace

TEST(JobChecksCacheBuild, FailedFacadeIsRefusedWithPathAndCause) {
    QTemporaryDir base;
    ASSERT_TRUE(base.isValid());
    for (const auto& [throws, cause] :
         {std::pair{true, u"disk exploded"_s}, std::pair{false, u"disk is full"_s}}) {
        const auto facade = facadeOver(std::make_shared<FailingType>(throws), base.path());
        const std::optional<QString> reason = cg::app::refusalForCacheBuild(*facade, u"Город Map"_s);
        ASSERT_TRUE(reason.has_value());
        EXPECT_TRUE(reason->startsWith(u"\"Город Map\": cannot open the cache: "_s)) << qPrintable(*reason);
        EXPECT_TRUE(reason->contains(u"Город/ (cache type 9): "_s)) << qPrintable(*reason);
        EXPECT_TRUE(reason->endsWith(cause)) << qPrintable(*reason);
    }
}

// T-0275: the real way a build fails. A ZMP with CacheType=9, an empty NameInCache (RAM is kept) and
// UseMemCache=0 makes RamTileStorage throw; every command that resolves the source is refused with the cause.
TEST(JobChecksCacheBuild, RamMapWithoutMemCacheIsRefusedByEveryCommand) {
    cg::maptype::test::ensureGuiApp();
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString maps = root.filePath(u"Maps"_s);
    writeZmp(maps,
             {1, u"ram.zmp"_s, u"Ram Broken"_s, "image/png", ".png", "NameInCache=\r\nUseMemCache=0\r\n"});
    AppOptions o;
    o.arguments = {u"--datadir="_s + root.filePath(u"data"_s)};
    o.mapsDirs = {maps};
    o.warn = [](const QString&) {};
    auto ctx = AppContext::create(o);
    ASSERT_TRUE(ctx.has_value());
    ASSERT_TRUE((*ctx)->loadMaps().has_value());
    for (const Command c : {Command::Download, Command::Run, Command::Export, Command::Estimate}) {
        Job j;
        j.command = c;
        j.source = bare(1);
        j.zooms = {5};
        j.out = root.filePath(u"o.mbtiles"_s);
        const auto r = resolveJob(j, **ctx, nullptr);
        ASSERT_FALSE(r.has_value()) << static_cast<int>(c);
        EXPECT_EQ(r.error().exitCode, 3);
        EXPECT_TRUE(r.error().message.startsWith(u"\"Ram Broken\": cannot open the cache: "_s))
            << r.error().message.toStdString();
        EXPECT_TRUE(r.error().message.contains(u"(cache type 9)"_s)) << r.error().message.toStdString();
    }
}

TEST(JobChecksCacheBuild, WorkingFacadePasses) {
    QTemporaryDir base;
    ASSERT_TRUE(base.isValid());
    const auto ram =
        facadeOver(std::make_shared<cg::storage::RamTileStorageType>(
                       std::make_shared<cg::storage::MapVersionFactory>(), nullptr),
                   base.path(),
                   std::make_shared<cg::storage::TileInfoMemCache>(100, std::chrono::milliseconds(60'000),
                                                                   cg::storage::ClearStrategy::ByYoungest));
    EXPECT_FALSE(cg::app::refusalForCacheBuild(*ram, u"Map"_s).has_value());
}

TEST_F(JobChecksTest, S8CropAndCutAfterDownload) {
    for (const auto& [n, name] : {std::pair{Crop, u"Crop Map"_s}, std::pair{Cut, u"Cut Map"_s}}) {
        const QString text = QStringLiteral("\"%1\" has crop or cut after download switched on; this is not "
                                            "supported")
                                 .arg(name);
        for (const Command c : {Command::Download, Command::Run}) {
            Job j = exportJob(n);
            j.command = c;
            expectRefused(resolve(j), text);
        }
        // export and estimate do not download
        EXPECT_TRUE(resolve(exportJob(n)).result.has_value()) << n;
        Job est = exportJob(n);
        est.command = Command::Estimate;
        EXPECT_TRUE(resolve(est).result.has_value()) << n;
        ASSERT_TRUE(map(n));
        EXPECT_EQ(refusalForDownload(*map(n)), text);
    }
}

TEST_F(JobChecksTest, S9DownloadsDisabledAndReadOnlyCache) {
    Job j = exportJob(NoDownload);
    j.command = Command::Download;
    expectRefused(resolve(j), u"downloads are disabled for this map"_s);
    j.command = Command::Run;
    expectRefused(resolve(j), u"downloads are disabled for this map"_s);
    EXPECT_TRUE(resolve(exportJob(NoDownload)).result.has_value()); // an export reads the cache only

    j = exportJob(ReadOnly);
    j.command = Command::Download;
    expectRefused(resolve(j), u"the cache of this map is read-only"_s);
    EXPECT_TRUE(resolve(exportJob(ReadOnly)).result.has_value());
}

TEST_F(JobChecksTest, CleanJobsPassWithoutReason) {
    for (const Command c : {Command::Estimate, Command::Download, Command::Export, Command::Run}) {
        Job j = exportJob(Png);
        j.command = c;
        const Outcome o = resolve(j);
        ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
        EXPECT_TRUE(o.warnings.isEmpty());
        ASSERT_EQ(o.result->size(), 1U);
        EXPECT_FALSE((*o.result)[0].asOverlay);
        EXPECT_EQ((*o.result)[0].map->zmp()->guid(), QUuid::fromString(guid(Png)));
        EXPECT_EQ((*o.result)[0].zooms, (std::vector<quint8>{5})); // T-0245: no key, the job's list
    }
    EXPECT_FALSE(refusalForDownload(*map(Png)).has_value());
}

TEST_F(JobChecksTest, S10OutputName) {
    Job j = exportJob(Png);
    j.out = outFile(u"o.txt"_s);
    expectRefused(resolve(j),
                  QStringLiteral("the output file name \"%1\" does not end with .mbtiles").arg(j.out));
    j.out = outFile(u"o.MBTILES"_s); // any case
    EXPECT_TRUE(resolve(j).result.has_value());

    j.out = outFile(u"nodir/o.mbtiles"_s);
    expectRefused(resolve(j), QStringLiteral("the directory of \"%1\" does not exist").arg(j.out));

    QFile existing(outFile(u"taken.mbtiles"_s));
    ASSERT_TRUE(existing.open(QIODevice::WriteOnly));
    existing.close();
    j.out = existing.fileName();
    expectRefused(resolve(j), QStringLiteral("\"%1\" exists; pass --overwrite to replace it").arg(j.out));
    j.overwrite = true;
    EXPECT_TRUE(resolve(j).result.has_value());
}

TEST_F(JobChecksTest, S10DirectoryNotWritable) {
    if (cg::core::platform::directoryPermissionsIgnored()) {
        GTEST_SKIP() << "root can write to any directory";
    }
    const QString dir = outFile(u"ro"_s);
    ASSERT_TRUE(QDir().mkpath(dir));
    ASSERT_TRUE(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    Job j = exportJob(Png);
    j.out = dir + u"/o.mbtiles"_s;
    const Outcome o = resolve(j);
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    expectRefused(o, QStringLiteral("the directory of \"%1\" is not writable").arg(j.out));
}

TEST_F(JobChecksTest, S10AppliesToTheOverlayFileToo) {
    QFile existing(outFile(u"o-overlay.mbtiles"_s));
    ASSERT_TRUE(existing.open(QIODevice::WriteOnly));
    existing.close();
    Job j = exportJob(Png);
    j.overlay = bare(JpgLayer);
    expectRefused(resolve(j),
                  QStringLiteral("\"%1\" exists; pass --overwrite to replace it").arg(existing.fileName()));
    j.overwrite = true;
    EXPECT_TRUE(resolve(j).result.has_value());
}

TEST_F(JobChecksTest, S11GivenTextsWithAnApostropheAreRefused) {
    Job j = exportJob(Png);
    j.name = u"Bob's"_s;
    expectRefused(resolve(j), u"--name must not contain an apostrophe"_s);
    j = exportJob(Png);
    j.description = u"it's"_s;
    expectRefused(resolve(j), u"--description must not contain an apostrophe"_s);
    j = exportJob(Png);
    j.attribution = u"(c) Joe's"_s;
    expectRefused(resolve(j), u"--attribution must not contain an apostrophe"_s);
    // only an export writes the metadata
    j = exportJob(Png);
    j.command = Command::Download;
    j.name = u"Bob's"_s;
    EXPECT_TRUE(resolve(j).result.has_value());
}

TEST_F(JobChecksTest, S11DerivedNameLosesTheApostropheWithAWarning) {
    const Outcome o = resolve(exportJob(Apostrophe));
    ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
    ASSERT_EQ(o.result->size(), 1U);
    EXPECT_EQ((*o.result)[0].name, u"Bobs Map"_s);
    EXPECT_EQ(o.warnings, QStringList{u"the apostrophe is removed from the name of \"Bob's Map\""_s});
}

// T-0222: legacy leaves the attribution edit empty (fr_ExportMBTiles.dfm edtAttr has no Text); the CLI
// defaults it to the License text of the source, each output file its own.
TEST_F(JobChecksTest, AttributionDefaultsToTheLicenseTextPerSource) {
    Job j = exportJob(Licensed);
    j.overlay = bare(LicensedLayer);
    const Outcome o = resolve(j);
    ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
    ASSERT_EQ(o.result->size(), 2U);
    EXPECT_EQ((*o.result)[0].attribution, u"Лицензия CC-BY-SA"_s);
    EXPECT_EQ((*o.result)[1].attribution, u"Credit EMODnet"_s);
    EXPECT_TRUE(o.warnings.isEmpty());
}

TEST_F(JobChecksTest, GivenAttributionWinsAndIsTrimmed) {
    Job j = exportJob(Licensed);
    j.attribution = u"  Автор Y "_s;
    const Outcome o = resolve(j);
    ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
    EXPECT_EQ((*o.result)[0].attribution, u"Автор Y"_s);
}

TEST_F(JobChecksTest, NoLicenseGivesAnEmptyAttribution) {
    const Outcome o = resolve(exportJob(Png));
    ASSERT_TRUE(o.result.has_value());
    EXPECT_TRUE((*o.result)[0].attribution.isEmpty());
}

TEST_F(JobChecksTest, S11DerivedAttributionLosesTheApostropheWithAWarning) {
    const Outcome o = resolve(exportJob(LicenseApostrophe));
    ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
    EXPECT_EQ((*o.result)[0].attribution, u"Joes data"_s);
    EXPECT_EQ(o.warnings, QStringList{u"the apostrophe is removed from the attribution of \"Lic Apos\""_s});
}

TEST_F(JobChecksTest, MetadataNames) {
    Job j = exportJob(Png);
    j.overlay = bare(JpgLayer);
    j.name = u"  My Chart "_s;
    const Outcome o = resolve(j);
    ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
    ASSERT_EQ(o.result->size(), 2U);
    EXPECT_EQ((*o.result)[0].name, u"My Chart"_s);   // --name trimmed, base file
    EXPECT_EQ((*o.result)[1].name, u"Город Слой"_s); // the overlay keeps its own name
    Job plain = exportJob(Png);
    EXPECT_EQ((*resolve(plain).result)[0].name, u"Plain PNG"_s);
}

// ---- Proof 3: output names (app-cli.md 1.5) ----

TEST_F(JobChecksTest, OutDirectoryGivesTheZmpStem) {
    Job j = exportJob(Png);
    j.out = outDir() + u'/'; // trailing slash
    const Outcome o = resolve(j);
    ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
    EXPECT_EQ((*o.result)[0].outFile, outFile(u"png.mbtiles"_s));
    j.out = outDir(); // an existing directory without the slash
    EXPECT_EQ((*resolve(j).result)[0].outFile, outFile(u"png.mbtiles"_s));
    j.out = outFile(u"given.mbtiles"_s); // a file name stays as typed
    EXPECT_EQ((*resolve(j).result)[0].outFile, outFile(u"given.mbtiles"_s));
}

// T-0362: a *.source.json source gives <name>.mbtiles in a directory, like a .zmp (not
// <name>.source.json.mbtiles). Its own context: the shared one holds only ZMP folders.
TEST_F(JobChecksTest, OutDirectoryGivesTheSourceJsonStem) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString maps = tmp.filePath(u"Maps"_s);
    ASSERT_TRUE(QDir().mkpath(maps));
    QFile in(cg::maptype::test::fixtureDir() + u"/maptype/json/ex63.source.json"_s);
    ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    QFile out(maps + u"/Topo-Карта.SOURCE.JSON"_s); // upper case and non-ASCII: the match is case-insensitive
    ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    out.write(in.readAll());
    out.close();
    AppOptions o;
    o.arguments = {u"--datadir="_s + tmp.filePath(u"data"_s)};
    o.mapsDirs = {maps};
    o.warn = [](const QString&) {};
    auto ctx = AppContext::create(o);
    ASSERT_TRUE(ctx.has_value());
    ASSERT_TRUE((*ctx)->loadMaps().has_value());
    Job j;
    j.command = Command::Export;
    j.source = u"51C1079B-B55D-4DF7-A410-ADC118F53AB1"_s;
    j.zooms = {5};
    j.out = outDir() + u'/';
    const auto r = resolveJob(j, **ctx, [](const QString&) {});
    ASSERT_TRUE(r.has_value()) << r.error().message.toStdString();
    EXPECT_EQ((*r)[0].outFile, outFile(u"Topo-Карта.mbtiles"_s));
}

TEST_F(JobChecksTest, OverlayDefaultsBesideTheBaseFile) {
    ASSERT_TRUE(QDir().mkpath(outFile(u"sub"_s)));
    Job j = exportJob(Png);
    j.overlay = bare(JpgLayer);
    j.out = outFile(u"sub/custom.mbtiles"_s); // neither the zmp stem nor the directory of --out
    const Outcome o = resolve(j);
    ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
    ASSERT_EQ(o.result->size(), 2U);
    EXPECT_FALSE((*o.result)[0].asOverlay);
    EXPECT_EQ((*o.result)[0].outFile, outFile(u"sub/custom.mbtiles"_s));
    EXPECT_TRUE((*o.result)[1].asOverlay);
    EXPECT_EQ((*o.result)[1].outFile, outFile(u"sub/custom-overlay.mbtiles"_s));

    j.out = outDir() + u'/'; // base from a directory
    EXPECT_EQ((*resolve(j).result)[1].outFile, outFile(u"png-overlay.mbtiles"_s));

    j.overlayOut = outFile(u"sub/ov.mbtiles"_s);
    EXPECT_EQ((*resolve(j).result)[1].outFile, outFile(u"sub/ov.mbtiles"_s));
}

TEST_F(JobChecksTest, OverlayOnlyJobUsesOut) {
    Job j = exportJob(JpgLayer);
    j.source.clear();
    j.overlay = bare(JpgLayer);
    j.out = outFile(u"only.mbtiles"_s);
    Outcome o = resolve(j);
    ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
    ASSERT_EQ(o.result->size(), 1U);
    EXPECT_TRUE((*o.result)[0].asOverlay);
    EXPECT_EQ((*o.result)[0].outFile, outFile(u"only.mbtiles"_s));
    j.out = outDir() + u'/';
    EXPECT_EQ((*resolve(j).result)[0].outFile, outFile(u"jpglayer.mbtiles"_s));
}

// ---- Proof 4: the slot warnings (app-cli.md 1.4) ----

TEST_F(JobChecksTest, SourceNamingALayerAndOverlayNamingAMapAreAcceptedWithAWarningEach) {
    Job j = exportJob(JpgLayer);
    j.overlay = bare(Png);
    const Outcome o = resolve(j);
    ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
    ASSERT_EQ(o.result->size(), 2U);
    EXPECT_EQ((*o.result)[0].map->zmp()->guid(), QUuid::fromString(guid(JpgLayer)));
    EXPECT_FALSE((*o.result)[0].asOverlay);
    EXPECT_EQ((*o.result)[1].map->zmp()->guid(), QUuid::fromString(guid(Png)));
    EXPECT_TRUE((*o.result)[1].asOverlay);
    ASSERT_EQ(o.warnings.size(), 2);
    EXPECT_EQ(o.warnings[0],
              QStringLiteral("--source \"%1\" is a layer in its ZMP; it is exported as a base map")
                  .arg(bare(JpgLayer)));
    EXPECT_EQ(o.warnings[1],
              QStringLiteral("--overlay \"%1\" is not a layer in its ZMP; it is exported as an "
                             "overlay")
                  .arg(bare(Png)));
    // the right slots give no warning
    Job right = exportJob(Png);
    right.overlay = bare(JpgLayer);
    EXPECT_TRUE(resolve(right).warnings.isEmpty());
}

TEST_F(JobChecksTest, UnknownOverlayIsRefusedLikeASource) {
    Job j = exportJob(Png);
    j.overlay = u"nosuchmap"_s;
    expectRefused(resolve(j), u"no map source matches \"nosuchmap\"; run \"chart-grabber-cli sources\""_s);
}

// ---- Proof 5: agreement with region::directTilesCopy ----

TEST_F(JobChecksTest, NoStructuralReasonExactlyWhenDirectTilesCopyAgrees) {
    // The expected value is a literal per fixture: stored as PNG / JPEG / WebP, bitmap, EPSG:3857.
    const struct {
        int n;
        bool direct;
    } expected[] = {{Png, true},
                    {JpgLayer, true},
                    {StoredWebp, true},
                    {Bmp, false},
                    {BmpGeo, false},
                    {Geo, false},
                    {Kml, false},
                    {Crop, true},
                    {Cut, true},
                    {NoDownload, true},
                    {ReadOnly, true},
                    {Apostrophe, true},
                    {Same1, true},
                    {Same2, true},
                    {FileNamedAsMap, true},
                    {FileNamedAsGuid, true},
                    {FileNamedAsUnknownGuid, true},
                    {DupFileLower, true},
                    {DupFileUpper, true},
                    {BingLike, true},
                    {EsriLike, true},
                    {ResponseWebp, true},
                    {Licensed, true},
                    {LicensedLayer, true},
                    {LicenseApostrophe, true},
                    {MaxTwo, true},
                    {MaxTwoLayer, true},
                    {MaxBad24, true},
                    {MaxBadX, true}};
    ASSERT_EQ(std::size(expected), static_cast<std::size_t>(Count));
    for (const auto& e : expected) {
        if (e.n == DupFileUpper && !caseSensitive_) {
            continue; // its file does not exist on this folder
        }
        const IMapTypePtr m = map(e.n);
        ASSERT_TRUE(m) << e.n;
        for (const bool asOverlay : {false, true}) {
            cg::region::ExportMbtilesParams p;
            (asOverlay ? p.overlay : p.map) = m;
            EXPECT_EQ(cg::region::directTilesCopy(p), e.direct) << e.n;
            Job j;
            j.zooms = {5};
            j.allowWebp = true; // the WebP flag is not part of directTilesCopy
            const auto reason = refusalForExport(*m, j, asOverlay);
            EXPECT_EQ(!reason.has_value(), e.direct) << e.n << ": " << (reason ? reason->toStdString() : "");
        }
    }
}

// T-0220 (app-cli.md 1.6.2, test 9): termsOfUseUrl reads the key of [PARAMS], trimmed.
// ---- T-0245: MaxZoom (weather-sources.md 6; project key, legacy has none: deviation row) ----

TEST_F(JobChecksTest, MaxZoomSkipsTheZoomsAboveItWithOneWarning) {
    Job j = exportJob(MaxTwo);
    j.zooms = {1, 2, 3, 4};
    const Outcome o = resolve(j);
    ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
    ASSERT_EQ(o.result->size(), 1U);
    EXPECT_EQ((*o.result)[0].zooms, (std::vector<quint8>{1, 2}));
    EXPECT_EQ(o.warnings, QStringList{u"\"Max Two\": zooms 3,4 skipped, the source stops at zoom 2"_s});
}

TEST_F(JobChecksTest, MaxZoomEqualToTheLastChosenZoomSkipsNothing) {
    Job j = exportJob(MaxTwo);
    j.zooms = {0, 2};
    const Outcome o = resolve(j);
    ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
    EXPECT_EQ((*o.result)[0].zooms, (std::vector<quint8>{0, 2}));
    EXPECT_TRUE(o.warnings.isEmpty());
}

TEST_F(JobChecksTest, MaxZoomLimitsOnlyItsOwnSource) {
    Job j = exportJob(Png);
    j.command = Command::Estimate;
    j.overlay = bare(MaxTwoLayer);
    j.zooms = {1, 2, 3};
    const Outcome o = resolve(j);
    ASSERT_TRUE(o.result.has_value()) << o.result.error().message.toStdString();
    ASSERT_EQ(o.result->size(), 2U);
    EXPECT_EQ((*o.result)[0].zooms, (std::vector<quint8>{1, 2, 3})); // no key: all of them
    EXPECT_EQ((*o.result)[1].zooms, (std::vector<quint8>{1, 2}));
    EXPECT_EQ(o.warnings, QStringList{u"\"Max Layer\": zooms 3 skipped, the source stops at zoom 2"_s});
}

TEST_F(JobChecksTest, MaxZoomBelowEveryChosenZoomRefusesTheJob) {
    Job j = exportJob(MaxTwo);
    j.zooms = {3, 4};
    const Outcome o = resolve(j);
    expectRefused(o, u"\"Max Two\": every chosen zoom is above its maximum zoom 2"_s);
    EXPECT_TRUE(o.warnings.isEmpty());
}

TEST_F(JobChecksTest, InvalidMaxZoomIsRefusedForEveryCommandAndNamedInTheVerdict) {
    for (const Command c : {Command::Estimate, Command::Download, Command::Export, Command::Run}) {
        Job j = exportJob(MaxBad24);
        j.command = c;
        expectRefused(resolve(j), u"\"Max 24\": MaxZoom=24 is not a zoom from 0 to 23"_s);
        j.source = bare(MaxBadX);
        expectRefused(resolve(j), u"\"Max X\": MaxZoom=x is not a zoom from 0 to 23"_s);
    }
    EXPECT_EQ(cg::app::refusalForSnapshot(*map(MaxBadX)),
              std::optional<QString>(u"\"Max X\": MaxZoom=x is not a zoom from 0 to 23"_s));
    EXPECT_FALSE(cg::app::refusalForSnapshot(*map(MaxTwo)).has_value());
}

TEST(TermsOfUseUrl, KeyPresentGivesTheTrimmedUrlAbsentEmptyOrBlankGiveNothing) {
    cg::maptype::test::ensureGuiApp();
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString maps = root.filePath(u"Maps"_s);
    ASSERT_TRUE(QDir().mkpath(maps));
    writeZmp(maps, {1, u"a.zmp"_s, u"Grey A"_s, "image/png", ".png",
                    "TermsOfUseWarning=  https://example.org/Ter ms  \r\n"});
    writeZmp(maps, {2, u"b.zmp"_s, u"Plain B"_s, "image/png", ".png", ""});
    writeZmp(maps, {3, u"c.zmp"_s, u"Empty C"_s, "image/png", ".png", "TermsOfUseWarning=\r\n"});
    writeZmp(maps, {4, u"d.zmp"_s, u"Blank D"_s, "image/png", ".png", "TermsOfUseWarning=   \r\n"});
    AppOptions o;
    o.arguments = {u"--datadir="_s + root.filePath(u"data"_s)};
    o.mapsDirs = {maps};
    o.warn = [](const QString&) {};
    auto ctx = AppContext::create(o);
    ASSERT_TRUE(ctx.has_value());
    ASSERT_TRUE((*ctx)->loadMaps().has_value());
    const auto map = [&](int n) { return (*ctx)->fullMapsSet()->mapTypeByGuid(QUuid::fromString(guid(n))); };

    const auto url = cg::app::termsOfUseUrl(*map(1));
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(*url, u"https://example.org/Ter ms"_s);
    EXPECT_FALSE(cg::app::termsOfUseUrl(*map(2)).has_value());
    EXPECT_FALSE(cg::app::termsOfUseUrl(*map(3)).has_value());
    EXPECT_FALSE(cg::app::termsOfUseUrl(*map(4)).has_value());
}

// T-0285 (app-cli.md 1.6.5): navigationWarning reads the key of [PARAMS]; only 1 / true (any case, trimmed).
TEST(NavigationWarning, OneAndTrueAnyCaseTrimmedAreTrueEverythingElseFalse) {
    cg::maptype::test::ensureGuiApp();
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString maps = root.filePath(u"Maps"_s);
    ASSERT_TRUE(QDir().mkpath(maps));
    const std::vector<std::pair<const char*, bool>> cases = {
        {"NavigationWarning=1\r\n", true},      {"NavigationWarning=true\r\n", true},
        {"NavigationWarning= TRUE \r\n", true}, {"NavigationWarning=0\r\n", false},
        {"NavigationWarning=\r\n", false},      {"NavigationWarning=yes\r\n", false},
        {"NavigationWarning=2\r\n", false},     {"", false},
    };
    for (size_t i = 0; i < cases.size(); ++i) {
        writeZmp(maps, {static_cast<int>(i + 1), u"m%1.zmp"_s.arg(i), u"Map %1"_s.arg(i), "image/png", ".png",
                        cases[i].first});
    }
    AppOptions o;
    o.arguments = {u"--datadir="_s + root.filePath(u"data"_s)};
    o.mapsDirs = {maps};
    o.warn = [](const QString&) {};
    auto ctx = AppContext::create(o);
    ASSERT_TRUE(ctx.has_value());
    ASSERT_TRUE((*ctx)->loadMaps().has_value());
    for (size_t i = 0; i < cases.size(); ++i) {
        const auto map =
            (*ctx)->fullMapsSet()->mapTypeByGuid(QUuid::fromString(guid(static_cast<int>(i + 1))));
        ASSERT_NE(map, nullptr) << i;
        EXPECT_EQ(cg::app::navigationWarning(*map), cases[i].second) << cases[i].first;
    }
}

// T-0277 (app-cli.md 1.7 S-16; legacy L-18): the bounding tile rectangle of the area at a chosen zoom must
// hold at most 2^31-1 tiles. The whole world (Web Mercator, template ZMP `Png`) holds 2^30 tiles at zoom 15
// and 2^32 at 16.
TEST_F(JobChecksTest, AreaSizeRefusesOnlyTheZoomWhoseBoundingTileRectangleExceedsTheLegacyCount) {
    const auto world = app_->lonLatFactory()->createPolygonByRect(
        cg::geo::LonLatRect{-180, 85.0511287798, 180, -85.0511287798});
    const auto box = app_->lonLatFactory()->createPolygonByRect(cg::geo::LonLatRect{0, 10, 10, 0});
    const std::vector<quint8> at15{15}, at16{16}, at3And16{3, 16};
    const QString refusal16 =
        u"the area is too large at zoom 16 (0-based): more than 2147483647 tiles; choose a "
        u"smaller area or lower zooms"_s;
    EXPECT_FALSE(cg::app::refusalForAreaSize(*map(Png), *world, at15).has_value());
    EXPECT_EQ(cg::app::refusalForAreaSize(*map(Png), *world, at16), std::optional<QString>(refusal16));
    EXPECT_FALSE(cg::app::refusalForAreaSize(*map(Png), *box, at16).has_value());
    EXPECT_EQ(cg::app::refusalForAreaSize(*map(Png), *world, at3And16), std::optional<QString>(refusal16));
}
