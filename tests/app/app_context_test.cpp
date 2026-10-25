// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/app_context (new code; inherits Src/u_GlobalState.pas:442-974, 1155-1210, 1280-1289).
// FEAT-NET-001 (load map definitions): contributes through AppContext::loadMaps; the row stays in
// held-open.md.
#include "cg/app/app_context.h"
#include "cg/core/language_list_static.h"
#include "cg/core/platform/test_support.h"
#include "cg/maptype/map_type.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/storage/cache_type.h"
#include "cg/storage/tile_info.h"
#include "cg/storage/tile_storage.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QUuid>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "../maptype/maptype_test_util.h"

using cg::app::AppContext;
using cg::app::AppOptions;
using cg::core::ErrorCode;

namespace {

const QByteArray kGuidA = "{AB0000A1-0000-0000-0000-0000000000A1}";
const QByteArray kGuidB = "{AB0000B2-0000-0000-0000-0000000000B2}";
const QByteArray kGuidC = "{AB0000D4-0000-0000-0000-0000000000D4}";

// One synthetic template-only ZMP folder (a RAM cache, CacheType=9, no disk), the shape of
// tests/maptype/map_types_main_list_test.cpp Env::makeSet.
void writeZmp(const QString& mapsDir, const QString& folder, const QByteArray& guid, const QByteArray& name,
              int cacheNumber, const QByteArray& cacheLines = "CacheType=9\r\n") {
    const QByteArray nameInCache =
        cacheNumber < 0 ? QByteArray() : "\r\nNameInCache=cache" + QByteArray::number(cacheNumber);
    const QByteArray text = "[PARAMS]\r\nGUID=" + guid + "\r\nname=" + name + nameInCache +
                            "\r\nDefURLBase=http://127.0.0.1:9/{z}/{x}/{y}.png\r\nContentType=image/png"
                            "\r\nExt=.png\r\nprojection=1\r\nsradiusa=6378137\r\nsradiusb=6356752"
                            "\r\n" +
                            cacheLines;
    const QString dir = QDir(mapsDir).filePath(folder);
    ASSERT_TRUE(QDir().mkpath(dir));
    QFile f(dir + QStringLiteral("/params.txt"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(text);
}

// A ZMP whose params.txt has no GUID: ZmpInfo reports "Empty GUID".
void writeBrokenZmp(const QString& mapsDir, const QString& folder) {
    const QString dir = QDir(mapsDir).filePath(folder);
    ASSERT_TRUE(QDir().mkpath(dir));
    QFile f(dir + QStringLiteral("/params.txt"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("[PARAMS]\r\nname=Broken\r\n");
}

void writeFile(const QString& path, const QByteArray& text) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(text);
}

QUuid uuid(const QByteArray& braced) {
    return QUuid::fromString(QString::fromLatin1(braced));
}

int threadCount() {
    return cg::core::platform::processThreadCount();
}

class AppContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        cg::maptype::test::ensureGuiApp();
        ASSERT_TRUE(root_.isValid());
    }

    [[nodiscard]] QString dataDir() const { return root_.filePath(QStringLiteral("data")); }
    [[nodiscard]] QString defaultMapsDir() const { return dataDir() + QStringLiteral("/Maps"); }

    [[nodiscard]] AppOptions options() {
        AppOptions o;
        o.arguments = {QStringLiteral("--datadir=") + dataDir()};
        o.mapsDirs = {defaultMapsDir()}; // T-0291: a --maps folder is required; the tests keep their folder
        o.warn = [this](const QString& message) {
            const std::lock_guard<std::mutex> lock(warningsMutex_);
            warnings_.append(message);
        };
        return o;
    }

    [[nodiscard]] QStringList warnings() {
        const std::lock_guard<std::mutex> lock(warningsMutex_);
        return warnings_;
    }

    QTemporaryDir root_;
    std::mutex warningsMutex_;
    QStringList warnings_;
};

} // namespace

// Proof 1. The program is read-only towards its configuration: no chart-grabber.conf appears. LoadMaps
// :1174-1195 with no map: MapTypesNoMapsError, naming the --maps folder (T-0291).
TEST_F(AppContextTest, EmptyMapsDirectoryGivesNotFoundAndWritesNoConfiguration) {
    ASSERT_TRUE(QDir().mkpath(defaultMapsDir()));
    auto ctx = AppContext::create(options());
    ASSERT_TRUE(ctx.has_value());

    const cg::core::Result<void> loaded = ctx.value()->loadMaps();
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().code(), ErrorCode::NotFound);
    EXPECT_TRUE(loaded.error().what().contains(defaultMapsDir()));
    EXPECT_EQ(ctx.value()->fullMapsSet(), nullptr);

    EXPECT_FALSE(QFileInfo::exists(dataDir() + QStringLiteral("/chart-grabber.conf")));
    EXPECT_EQ(QDir(dataDir()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
              QStringList{QStringLiteral("Maps")});
}

// Proof 2. Three ZMP folders, one without GUID: the two good maps load, the broken one is reported once.
TEST_F(AppContextTest, BrokenZmpIsReportedAndTheOthersLoad) {
    ASSERT_TRUE(QDir().mkpath(defaultMapsDir()));
    writeZmp(defaultMapsDir(), QStringLiteral("a.zmp"), kGuidA, "Map A", 1);
    writeBrokenZmp(defaultMapsDir(), QStringLiteral("broken.zmp"));
    writeZmp(defaultMapsDir(), QStringLiteral("c.zmp"), kGuidC, "Map C", 3);

    auto ctx = AppContext::create(options());
    ASSERT_TRUE(ctx.has_value());
    ASSERT_TRUE(ctx.value()->loadMaps().has_value());

    const cg::maptype::IMapTypeSetPtr maps = ctx.value()->fullMapsSet();
    ASSERT_NE(maps, nullptr);
    EXPECT_EQ(maps->count(), 2);
    EXPECT_NE(maps->mapTypeByGuid(uuid(kGuidA)), nullptr);
    EXPECT_EQ(maps->mapTypeByGuid(uuid(kGuidB)), nullptr);
    EXPECT_NE(maps->mapTypeByGuid(uuid(kGuidC)), nullptr);
    // u_ZmpInfoSet message of the port (tests/maptype/zmp_info_set_test.cpp): the file is named in it
    EXPECT_EQ(warnings(), QStringList{QStringLiteral("There is an error Empty GUID in map broken.zmp")});
    EXPECT_EQ(ctx.value()->zmpErrorCount(), 1);
}

// Proof 3. --maps pointing at a missing directory: not created, the S-1 (no maps) error.
TEST_F(AppContextTest, MapsOptionWithMissingDirectoryIsNotCreated) {
    const QString missing = root_.filePath(QStringLiteral("nowhere/Maps"));
    AppOptions o = options();
    o.mapsDirs = {missing};
    auto ctx = AppContext::create(o);
    ASSERT_TRUE(ctx.has_value());
    EXPECT_FALSE(QFileInfo::exists(missing));
    // the configured default is not created either (T-0291)
    EXPECT_FALSE(QFileInfo::exists(defaultMapsDir()));

    const cg::core::Result<void> loaded = ctx.value()->loadMaps();
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().code(), ErrorCode::NotFound);
    EXPECT_TRUE(loaded.error().what().contains(missing));
    EXPECT_FALSE(QFileInfo::exists(missing));
}

// Proof 5. No `lang` directory next to the executable of the test: the language list holds `en` only.
TEST_F(AppContextTest, MissingLanguageDirectoryLeavesEnglishOnly) {
    auto ctx = AppContext::create(options());
    ASSERT_TRUE(ctx.has_value());
    const cg::core::ILanguageListStaticPtr list = ctx.value()->languages().languageList();
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 1);
    EXPECT_EQ(list->code(0), QStringLiteral("en"));
}

// Proof 6. requestClose is idempotent; appClosing fires.
TEST_F(AppContextTest, RequestCloseIsIdempotent) {
    auto ctx = AppContext::create(options());
    ASSERT_TRUE(ctx.has_value());
    const std::stop_token token = ctx.value()->appClosing();
    EXPECT_TRUE(token.stop_possible());
    EXPECT_FALSE(token.stop_requested());
    ctx.value()->requestClose();
    ctx.value()->requestClose();
    EXPECT_TRUE(token.stop_requested());
    EXPECT_TRUE(ctx.value()->appClosing().stop_requested());
}

// Proof 6. Destruction after loadMaps: ~AppContext (u_GlobalState.pas:1280-1289) fires appClosing even when
// nobody called requestClose, returns without a hang and leaves no thread of its own (the collector thread
// :735-741 is joined).
TEST_F(AppContextTest, DestructionClosesJoinsThreadsAndReturnsInTime) {
    ASSERT_TRUE(QDir().mkpath(defaultMapsDir()));
    writeZmp(defaultMapsDir(), QStringLiteral("a.zmp"), kGuidA, "Map A", 1);
    // ThreadSanitizer starts its own background thread when the first thread of the process is created and
    // keeps it: create and join one thread first, so that the baseline already holds it.
    std::thread([] {}).join();
    const int threadsBefore = threadCount();

    std::stop_token token;
    std::chrono::steady_clock::time_point destroyStart;
    {
        auto ctx = AppContext::create(options());
        ASSERT_TRUE(ctx.has_value());
        ASSERT_TRUE(ctx.value()->loadMaps().has_value());
        EXPECT_GT(threadCount(), threadsBefore); // the collector thread is running
        token = ctx.value()->appClosing();
        EXPECT_FALSE(token.stop_requested());
        destroyStart = std::chrono::steady_clock::now();
    }
    const auto elapsed = std::chrono::steady_clock::now() - destroyStart;
    EXPECT_TRUE(token.stop_requested()); // kills: requestClose missing from the destructor
    EXPECT_LT(elapsed, std::chrono::seconds(5));
    EXPECT_EQ(threadCount(), threadsBefore); // kills: collector thread not joined
}

// T-0198 (UAT D-1, app-cli.md 1.6). Proof 1: a CacheType=9 map stores a tile on disk below
// <cache>/cache_sqlite/<NameInCache>/, and a second context over the same directories finds it.
TEST_F(AppContextTest, RamCacheMapStoresTilesOnDiskAndASecondContextFindsThem) {
    ASSERT_TRUE(QDir().mkpath(defaultMapsDir()));
    writeZmp(defaultMapsDir(), QStringLiteral("a.zmp"), kGuidA, "Map A", 1);
    QByteArray png;
    {
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(qRgb(30, 60, 90));
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
    }
    const cg::geo::PointI xy(5, 6);
    {
        auto ctx = AppContext::create(options());
        ASSERT_TRUE(ctx.has_value());
        ASSERT_TRUE(ctx.value()->loadMaps().has_value());
        const auto map = ctx.value()->fullMapsSet()->mapTypeByGuid(uuid(kGuidA));
        ASSERT_NE(map, nullptr);
        const auto storage = map->tileStorage();
        ASSERT_NE(storage, nullptr);
        const auto saved = storage->saveTile(xy, 4, map->versionRequest()->getStatic()->baseVersion,
                                             cg::storage::fromPosixSeconds(1700000000),
                                             QByteArrayView("image/png"), png, true);
        ASSERT_TRUE(saved.has_value() && *saved);
    }
    QStringList files;
    QDirIterator it(dataDir(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files.push_back(it.next());
    }
    EXPECT_TRUE(std::ranges::any_of(files, [](const QString& f) {
        return f.contains(QStringLiteral("/cache_sqlite/cache1/"));
    })) << qPrintable(files.join(u'\n'));

    auto ctx = AppContext::create(options());
    ASSERT_TRUE(ctx.has_value());
    ASSERT_TRUE(ctx.value()->loadMaps().has_value());
    const auto map = ctx.value()->fullMapsSet()->mapTypeByGuid(uuid(kGuidA));
    ASSERT_NE(map, nullptr);
    const auto info = map->tileStorage()->tileInfo(xy, 4, map->versionRequest()->getStatic()->baseVersion,
                                                   cg::storage::LoadMode::WithData);
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->isExists());
    EXPECT_EQ(info->data, png);
}

// Proof 3. The cache type of every map after loadMaps: RAM becomes Default (or SQLite when DefCache=9), every
// other code stays. Codes: Default 0, Ram 9, Sqlite 71, Bdb 6. A RAM map without NameInCache keeps RAM.
TEST_F(AppContextTest, OnlyRamCacheMapsAreSwitchedToAFileCache) {
    using cg::storage::CacheTypeId;
    struct Row {
        QByteArray cacheLines;
        QByteArray iniExtra;
        CacheTypeId expected;
        bool withName = true;
    };
    const Row rows[] = {
        {"CacheType=9\r\n", "", CacheTypeId::Default},
        {"CacheType=71\r\n", "", CacheTypeId::Sqlite},
        {"CacheType=6\r\n", "", CacheTypeId::Bdb},
        {"", "", CacheTypeId::Default},
        {"CacheType=0\r\n", "[VIEW]\r\nDefCache=9\r\n", CacheTypeId::Sqlite},
        {"CacheType=9\r\n", "[VIEW]\r\nDefCache=9\r\n", CacheTypeId::Sqlite},
        {"CacheType=71\r\n", "[VIEW]\r\nDefCache=9\r\n", CacheTypeId::Sqlite},
        {"CacheType=0\r\n", "[VIEW]\r\nDefCache=6\r\n", CacheTypeId::Default},
        {"CacheType=9\r\n", "", CacheTypeId::Ram, false},
    };
    for (const Row& row : rows) {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString data = dir.filePath(QStringLiteral("data"));
        const QString maps = data + QStringLiteral("/Maps");
        ASSERT_TRUE(QDir().mkpath(maps));
        writeZmp(maps, QStringLiteral("a.zmp"), kGuidA, "Map A", row.withName ? 1 : -1, row.cacheLines);
        writeFile(data + QStringLiteral("/chart-grabber.conf"), row.iniExtra);
        AppOptions o;
        o.arguments = {QStringLiteral("--datadir=") + data};
        o.mapsDirs = {maps};
        o.warn = [](const QString&) {};
        auto ctx = AppContext::create(o);
        ASSERT_TRUE(ctx.has_value());
        ASSERT_TRUE(ctx.value()->loadMaps().has_value());
        const auto map = ctx.value()->fullMapsSet()->mapTypeByGuid(uuid(kGuidA));
        ASSERT_NE(map, nullptr);
        EXPECT_EQ(static_cast<int>(map->storageConfig()->cacheType()), static_cast<int>(row.expected))
            << row.cacheLines.constData() << row.iniExtra.constData();
    }
}

// T-0198 proof 2, storage level: MemCacheCapacity=2 and three saved tiles. A RAM storage keeps the last two
// (u_TileInfoBasicMemCache.pas:271-282); the file cache keeps all three.
TEST_F(AppContextTest, RamCacheMapKeepsMoreTilesThanMemCacheCapacity) {
    ASSERT_TRUE(QDir().mkpath(defaultMapsDir()));
    writeZmp(defaultMapsDir(), QStringLiteral("a.zmp"), kGuidA, "Map A", 1,
             "CacheType=9\r\nMemCacheCapacity=2\r\n");
    auto ctx = AppContext::create(options());
    ASSERT_TRUE(ctx.has_value());
    ASSERT_TRUE(ctx.value()->loadMaps().has_value());
    const auto map = ctx.value()->fullMapsSet()->mapTypeByGuid(uuid(kGuidA));
    ASSERT_NE(map, nullptr);
    const auto version = map->versionRequest()->getStatic()->baseVersion;
    const cg::geo::PointI tiles[] = {{7, 6}, {5, 6}, {6, 6}};
    for (const cg::geo::PointI& xy : tiles) {
        const QByteArray data = QByteArray("tile") + QByteArray::number(xy.x());
        const auto saved =
            map->tileStorage()->saveTile(xy, 4, version, cg::storage::fromPosixSeconds(1700000000),
                                         QByteArrayView("image/png"), data, true);
        ASSERT_TRUE(saved.has_value() && *saved);
    }
    for (const cg::geo::PointI& xy : tiles) {
        const auto info = map->tileStorage()->tileInfo(xy, 4, version, cg::storage::LoadMode::WithData);
        ASSERT_TRUE(info.has_value());
        EXPECT_TRUE(info->isExists()) << xy.x();
        EXPECT_EQ(info->data, QByteArray("tile") + QByteArray::number(xy.x()));
    }
}

// T-0291 (app-cli.md 1.6.1): the --maps folders. No FEAT; the D row of the ticket records it.
namespace {

class MapsFoldersTest : public AppContextTest {
protected:
    [[nodiscard]] QString folderA() const { return root_.filePath(QStringLiteral("folderA")); }
    [[nodiscard]] QString folderB() const { return root_.filePath(QStringLiteral("folderB")); }
    [[nodiscard]] AppOptions foldersOptions(const QStringList& folders) {
        AppOptions o = options();
        o.mapsDirs = folders;
        return o;
    }
    static QString nameOf(const AppContext& ctx, const QByteArray& guid) {
        const auto map = ctx.fullMapsSet()->mapTypeByGuid(uuid(guid));
        return map ? map->guiConfig()->name()->value() : QStringLiteral("<absent>");
    }
    // a ZMP with a GUID that fails to load: content type and extension are both unknown
    static void writeUnloadableZmp(const QString& mapsDir, const QString& folder, const QByteArray& guid) {
        const QString dir = QDir(mapsDir).filePath(folder);
        ASSERT_TRUE(QDir().mkpath(dir));
        writeFile(dir + QStringLiteral("/params.txt"),
                  "[PARAMS]\r\nGUID=" + guid +
                      "\r\nname=Unloadable\r\nNameInCache=cacheU\r\nContentType=image/zzz\r\nExt=.zzz\r\n"
                      "projection=1\r\nsradiusa=6378137\r\nsradiusb=6356752\r\n");
    }
    static QString noMapsText(const QString& folders) {
        return QStringLiteral("no map source loaded from %1: Unable to load map(s)! Aborting...")
            .arg(folders);
    }
};

} // namespace

// (1) no folder: refused first, nothing created (not even the --datadir directory).
TEST_F(MapsFoldersTest, NoFolderIsRefusedBeforeAnythingIsCreated) {
    AppOptions o = options();
    o.mapsDirs.clear();
    auto ctx = AppContext::create(o);
    ASSERT_FALSE(ctx.has_value());
    EXPECT_EQ(ctx.error().code(), ErrorCode::NotFound);
    EXPECT_EQ(ctx.error().what(),
              QStringLiteral("no map sources given: pass --maps DIR, a folder of map-source definitions "
                             "(repeat --maps for more folders)"));
    EXPECT_FALSE(QFileInfo::exists(dataDir()));
}

// (2) one GUID in both folders: the later folder's definition is in use; one uncounted warning line.
TEST_F(MapsFoldersTest, LaterFolderDefinitionIsUsedWithOneUncountedWarning) {
    writeZmp(folderA(), QStringLiteral("a.zmp"), kGuidA, "From A", 1);
    writeZmp(folderB(), QStringLiteral("b.zmp"), kGuidA, "From B", 2);
    writeZmp(folderA(), QStringLiteral("only.zmp"), kGuidC, "Only A", 3);
    auto ctx = AppContext::create(foldersOptions({folderA(), folderB()}));
    ASSERT_TRUE(ctx.has_value()) << ctx.error().what().toStdString();
    ASSERT_TRUE(ctx.value()->loadMaps().has_value());
    EXPECT_EQ(ctx.value()->fullMapsSet()->count(), 2);
    EXPECT_EQ(nameOf(*ctx.value(), kGuidA), QStringLiteral("From B"));
    EXPECT_EQ(nameOf(*ctx.value(), kGuidC), QStringLiteral("Only A"));
    EXPECT_EQ(warnings(), QStringList{QStringLiteral("map source %1: %2/b.zmp replaces %3/a.zmp")
                                          .arg(QString::fromLatin1(kGuidA), folderB(), folderA())});
    EXPECT_EQ(ctx.value()->zmpErrorCount(), 0);
}

// (3) two files of one GUID inside folder A: the legacy rule (the first in sort order stays, one error,
// u_ZmpInfoSet.pas:174-177); with B also defining the GUID the warning names only A's survivor.
TEST_F(MapsFoldersTest, DuplicateInsideOneFolderKeepsTheLegacyRuleAndTheWarningNamesTheSurvivor) {
    writeZmp(folderA(), QStringLiteral("u2.zmp"), kGuidA, "A second", 3);
    writeZmp(folderA(), QStringLiteral("u1.zmp"), kGuidA, "A first", 4);
    {
        auto ctx = AppContext::create(foldersOptions({folderA()}));
        ASSERT_TRUE(ctx.has_value()) << ctx.error().what().toStdString();
        ASSERT_TRUE(ctx.value()->loadMaps().has_value());
        EXPECT_EQ(nameOf(*ctx.value(), kGuidA), QStringLiteral("A first"));
        EXPECT_EQ(ctx.value()->zmpErrorCount(), 1);
        EXPECT_EQ(warnings().size(), 1) << warnings().join(u'|').toStdString();
    }
    warnings_.clear();
    writeZmp(folderB(), QStringLiteral("b.zmp"), kGuidA, "From B", 5);
    auto ctx = AppContext::create(foldersOptions({folderA(), folderB()}));
    ASSERT_TRUE(ctx.has_value()) << ctx.error().what().toStdString();
    ASSERT_TRUE(ctx.value()->loadMaps().has_value());
    EXPECT_EQ(nameOf(*ctx.value(), kGuidA), QStringLiteral("From B"));
    EXPECT_EQ(ctx.value()->zmpErrorCount(), 1);
    const QStringList all = warnings(); // the load error of the second file and the replace line
    ASSERT_EQ(all.size(), 2) << all.join(u'|').toStdString();
    EXPECT_EQ(all.filter(QStringLiteral("replaces")),
              QStringList{QStringLiteral("map source %1: %2/b.zmp replaces %3/u1.zmp")
                              .arg(QString::fromLatin1(kGuidA), folderB(), folderA())});
}

// (4) a missing folder contributes nothing and is not an error; with every folder missing S-1 names all.
TEST_F(MapsFoldersTest, MissingFolderContributesNothingAndAllMissingIsTheNoMapsError) {
    writeZmp(folderB(), QStringLiteral("b.zmp"), kGuidB, "From B", 2);
    {
        auto ctx = AppContext::create(foldersOptions({folderA(), folderB()}));
        ASSERT_TRUE(ctx.has_value()) << ctx.error().what().toStdString();
        ASSERT_TRUE(ctx.value()->loadMaps().has_value());
        EXPECT_EQ(ctx.value()->fullMapsSet()->count(), 1);
        EXPECT_EQ(nameOf(*ctx.value(), kGuidB), QStringLiteral("From B"));
        EXPECT_EQ(ctx.value()->zmpErrorCount(), 0);
        EXPECT_TRUE(warnings().isEmpty());
        EXPECT_FALSE(QFileInfo::exists(folderA()));
    }
    const QString none1 = root_.filePath(QStringLiteral("none1"));
    const QString none2 = root_.filePath(QStringLiteral("none2"));
    auto ctx = AppContext::create(foldersOptions({none1, none2}));
    ASSERT_TRUE(ctx.has_value());
    const cg::core::Result<void> loaded = ctx.value()->loadMaps();
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().code(), ErrorCode::NotFound);
    EXPECT_EQ(loaded.error().what(), noMapsText(none1 + QStringLiteral(", ") + none2));
}

// (5) a path that exists but is not a directory.
TEST_F(MapsFoldersTest, AFileAsFolderFailsTheCreate) {
    writeZmp(folderA(), QStringLiteral("a.zmp"), kGuidA, "From A", 1);
    const QString file = root_.filePath(QStringLiteral("plain.txt"));
    writeFile(file, "x");
    auto ctx = AppContext::create(foldersOptions({folderA(), file}));
    ASSERT_FALSE(ctx.has_value());
    EXPECT_EQ(ctx.error().code(), ErrorCode::NotFound);
    EXPECT_EQ(ctx.error().what(), QStringLiteral("--maps: %1 is not a directory").arg(file));
}

// (6) the configured maps directory ([PATHtoMAPS] PrimaryPath, u_GlobalConfig.pas:230-231) and the default
// <datadir>/Maps are neither read nor created.
TEST_F(MapsFoldersTest, ConfiguredMapsDirectoryIsNeitherReadNorCreated) {
    const QString fromIni = root_.filePath(QStringLiteral("ini_maps"));
    writeZmp(fromIni, QStringLiteral("i.zmp"), kGuidB, "From ini", 2);
    writeZmp(defaultMapsDir(), QStringLiteral("d.zmp"), kGuidC, "From default", 3);
    writeZmp(folderA(), QStringLiteral("a.zmp"), kGuidA, "From A", 1);
    writeFile(dataDir() + QStringLiteral("/chart-grabber.conf"),
              "[PATHtoMAPS]\r\nPrimaryPath=" + fromIni.toUtf8() + "\r\n");
    {
        auto ctx = AppContext::create(foldersOptions({folderA()}));
        ASSERT_TRUE(ctx.has_value()) << ctx.error().what().toStdString();
        ASSERT_TRUE(ctx.value()->loadMaps().has_value());
        EXPECT_EQ(ctx.value()->fullMapsSet()->count(), 1);
        EXPECT_EQ(nameOf(*ctx.value(), kGuidA), QStringLiteral("From A"));
        EXPECT_EQ(nameOf(*ctx.value(), kGuidB), QStringLiteral("<absent>"));
        EXPECT_EQ(nameOf(*ctx.value(), kGuidC), QStringLiteral("<absent>"));
    }
    QDir(defaultMapsDir()).removeRecursively();
    QFile::remove(dataDir() + QStringLiteral("/chart-grabber.conf"));
    auto ctx = AppContext::create(foldersOptions({folderA()}));
    ASSERT_TRUE(ctx.has_value());
    EXPECT_FALSE(QFileInfo::exists(defaultMapsDir()));
}

// (7) Maps.ini is read from the LAST folder only (DIR/Maps.ini). Key Enabled=0 (proof key of app-cli 1.6.3,
// map_type_gui_config.cpp:108-111).
TEST_F(MapsFoldersTest, MapsIniIsReadFromTheLastFolderOnly) {
    writeZmp(folderA(), QStringLiteral("a.zmp"), kGuidA, "From A", 1);
    writeZmp(folderB(), QStringLiteral("b.zmp"), kGuidB, "From B", 2);
    const QByteArray disableA = "[" + kGuidA + "]\r\nEnabled=0\r\n";
    const QByteArray disableB = "[" + kGuidB + "]\r\nEnabled=0\r\n";
    {
        writeFile(folderB() + QStringLiteral("/Maps.ini"), disableA); // the last folder
        auto ctx = AppContext::create(foldersOptions({folderA(), folderB()}));
        ASSERT_TRUE(ctx.has_value());
        ASSERT_TRUE(ctx.value()->loadMaps().has_value());
        EXPECT_FALSE(ctx.value()->fullMapsSet()->mapTypeByGuid(uuid(kGuidA))->guiConfig()->enabled());
        EXPECT_TRUE(ctx.value()->fullMapsSet()->mapTypeByGuid(uuid(kGuidB))->guiConfig()->enabled());
    }
    // the same file in the first folder only: not applied
    QFile::remove(folderB() + QStringLiteral("/Maps.ini"));
    writeFile(folderA() + QStringLiteral("/Maps.ini"), disableA);
    auto ctx = AppContext::create(foldersOptions({folderA(), folderB()}));
    ASSERT_TRUE(ctx.has_value());
    ASSERT_TRUE(ctx.value()->loadMaps().has_value());
    EXPECT_TRUE(ctx.value()->fullMapsSet()->mapTypeByGuid(uuid(kGuidA))->guiConfig()->enabled());
    EXPECT_TRUE(ctx.value()->fullMapsSet()->mapTypeByGuid(uuid(kGuidB))->guiConfig()->enabled());
    // and the file of the last folder names a map of the last folder
    writeFile(folderB() + QStringLiteral("/Maps.ini"), disableB);
    auto last = AppContext::create(foldersOptions({folderA(), folderB()}));
    ASSERT_TRUE(last.has_value());
    ASSERT_TRUE(last.value()->loadMaps().has_value());
    EXPECT_TRUE(last.value()->fullMapsSet()->mapTypeByGuid(uuid(kGuidA))->guiConfig()->enabled());
    EXPECT_FALSE(last.value()->fullMapsSet()->mapTypeByGuid(uuid(kGuidB))->guiConfig()->enabled());
}

// (7a) T-0357: DIR/Maps.conf is read when it exists, else DIR/Maps.ini; neither file is renamed or written.
TEST_F(MapsFoldersTest, MapsConfIsPreferredAndMapsIniStillRead) {
    writeZmp(folderA(), QStringLiteral("a.zmp"), kGuidA, "From A", 1);
    writeZmp(folderB(), QStringLiteral("b.zmp"), kGuidB, "From B", 2);
    const QByteArray disableA = "[" + kGuidA + "]\r\nEnabled=0\r\n";
    const QByteArray disableB = "[" + kGuidB + "]\r\nEnabled=0\r\n";
    const auto enabled = [&](const QByteArray& guid) {
        auto ctx = AppContext::create(foldersOptions({folderB()}));
        EXPECT_TRUE(ctx.has_value());
        EXPECT_TRUE(ctx.value()->loadMaps().has_value());
        return ctx.value()->fullMapsSet()->mapTypeByGuid(uuid(guid))->guiConfig()->enabled();
    };
    writeFile(folderB() + QStringLiteral("/Maps.ini"), disableB); // only the old name: still read
    EXPECT_FALSE(enabled(kGuidB));
    writeFile(folderB() + QStringLiteral("/Maps.conf"), disableA); // both: Maps.conf alone counts
    EXPECT_TRUE(enabled(kGuidB));
    EXPECT_EQ(QFileInfo(folderB() + QStringLiteral("/Maps.ini")).size(), disableB.size()); // not touched
    EXPECT_TRUE(QFileInfo::exists(folderB() + QStringLiteral("/Maps.conf")));
    QFile::remove(folderB() + QStringLiteral("/Maps.ini")); // only the new name
    EXPECT_TRUE(enabled(kGuidB));
    EXPECT_FALSE(QFileInfo::exists(folderB() + QStringLiteral("/Maps.ini")));
}

// (8) a ZMP that fails to load in B does not hide A's definition of the same GUID.
TEST_F(MapsFoldersTest, AnUnloadableZmpInTheLaterFolderDoesNotHideTheEarlierOne) {
    writeZmp(folderA(), QStringLiteral("a.zmp"), kGuidA, "From A", 1);
    writeUnloadableZmp(folderB(), QStringLiteral("b.zmp"), kGuidA);
    auto ctx = AppContext::create(foldersOptions({folderA(), folderB()}));
    ASSERT_TRUE(ctx.has_value()) << ctx.error().what().toStdString();
    ASSERT_TRUE(ctx.value()->loadMaps().has_value());
    EXPECT_EQ(nameOf(*ctx.value(), kGuidA), QStringLiteral("From A"));
    EXPECT_EQ(ctx.value()->zmpErrorCount(), 1);
    ASSERT_EQ(warnings().size(), 1) << warnings().join(u'|').toStdString(); // the load error, no replace line
    EXPECT_FALSE(warnings().front().contains(QStringLiteral("replaces")));
}

// (9) + (10) the same folder twice (once spelled with `..`) is loaded once, at its last position.
TEST_F(MapsFoldersTest, TheSameFolderTwiceCountsOnceAtItsLastPosition) {
    writeZmp(folderA(), QStringLiteral("a.zmp"), kGuidA, "From A", 1);
    writeZmp(folderB(), QStringLiteral("b.zmp"), kGuidB, "From B", 2);
    const QString respelled = folderA() + QStringLiteral("/../folderA");
    auto ctx = AppContext::create(foldersOptions({folderA(), folderB(), respelled}));
    ASSERT_TRUE(ctx.has_value()) << ctx.error().what().toStdString();
    EXPECT_EQ(ctx.value()->mapsDirs(), (QStringList{folderB(), respelled}));
    ASSERT_TRUE(ctx.value()->loadMaps().has_value());
    EXPECT_EQ(ctx.value()->fullMapsSet()->count(), 2);
    EXPECT_TRUE(warnings().isEmpty());
    EXPECT_EQ(ctx.value()->zmpErrorCount(), 0);

    // a missing folder is the same folder by its cleaned absolute path; no message either
    const QString missing = root_.filePath(QStringLiteral("nowhere"));
    auto twice = AppContext::create(foldersOptions({missing + QStringLiteral("/x/.."), folderB(), missing}));
    ASSERT_TRUE(twice.has_value());
    EXPECT_EQ(twice.value()->mapsDirs(), (QStringList{folderB(), missing}));
    EXPECT_FALSE(QFileInfo::exists(missing));
}
