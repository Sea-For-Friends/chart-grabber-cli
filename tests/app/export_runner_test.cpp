// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/export_runner (new code; inherits RegionProcess/u_ExportProviderAbstract.pas:182-202
// and the cancel/close behaviour of Export/MBTiles/u_ExportTaskToMBTiles.pas:220-278). T-0192.
// No FEAT tag: FEAT-REG-069 is T-0172 / T-0183's.
//
// Rig: one real AppContext (tile-iterator factory), maps over a RamTileStorage holding the fixture PNG at the
// z2 tiles (1,1) (2,1) (1,2) of the area lon -80..80, lat 50..-50 (z2 tiles x 1..2, y 1..2 = 4 tiles, one
// of them empty), wrapped in a decorator that counts tileInfoEx calls and runs a hook / injects an error at
// a chosen call. The cancel test is deterministic without a sleep: the hook (on the export thread) fires the
// termination and blocks until the console's clock callback runs, which the calling thread reaches only
// after stop.request_stop(). Tests that start the export thread are TSan-relevant.
#include "cg/app/app_context.h"
#include "cg/app/console.h"
#include "cg/app/export_runner.h"
#include "cg/core/content_type.h"
#include "cg/core/language_manager.h"
#include "cg/core/platform/qt_compat.h"
#include "cg/core/platform/test_support.h"
#include "cg/core/version.h"
#include "cg/geo/datum.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/map_version_factory_changeable.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/map_version_request_config.h"
#include "cg/storage/ram_tile_storage.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_storage.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <unistd.h>

#include "../corpus_skip.h"
#include "../maptype/maptype_test_util.h"
#include "../maptype/zmp_info_gui_stub.h"
#include "../region/region_fake_map_type.h"

using namespace cg;
using cg::app::Console;
using cg::app::ExportOutcome;
using cg::app::ExportResult;
using cg::app::Job;
using cg::app::ResolvedSource;
using storage::sqlite::SqliteDb;
using storage::sqlite::SqliteStmt;

namespace {

QString fixturesDir() {
#ifdef CG_FIXTURES_DIR
    return QString::fromUtf8(CG_FIXTURES_DIR);
#else
    QDir dir = QDir::current();
    for (int level = 0; level < 8; ++level) {
        if (dir.exists(QStringLiteral("tests/fixtures"))) {
            return dir.filePath(QStringLiteral("tests/fixtures"));
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QStringLiteral("tests/fixtures");
#endif
}

const QByteArray& png() {
    static const QByteArray d = [] {
        QFile f(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
        EXPECT_TRUE(f.open(QIODevice::ReadOnly));
        return f.readAll();
    }();
    return d;
}

geo::ProjectionSetPtr googleSet() {
    static const auto datums = std::make_shared<const geo::DatumFactory>(geo::HashSeed{});
    static const geo::ProjectionSetFactory factory{geo::HashSeed{}, datums};
    return factory.byCode(geo::kProjEpsgGoogle, geo::kTileSplitQuadrate256x256);
}

// Forwards to the inner storage; counts tileInfoEx and runs `hook` / fails at the chosen ordinal.
class HookStorage final : public storage::ITileStorage {
public:
    explicit HookStorage(std::shared_ptr<storage::ITileStorage> inner) : inner_(std::move(inner)) {}

    int hookAt = 0;             // 1-based ordinal; 0 = never
    std::function<void()> hook; // runs on the calling (export) thread
    int failAt = 0;             // 1-based ordinal; 0 = never
    int calls() const { return calls_.load(); }

    core::Result<storage::TileInfo> tileInfoEx(geo::PointI xy, quint8 zoom,
                                               const storage::MapVersionRequest& request,
                                               storage::LoadMode mode) const override {
        const int n = ++calls_;
        if (n == hookAt && hook) {
            hook();
        }
        if (n == failAt) {
            return core::fail(core::ErrorCode::IoError, QStringLiteral("scripted failure at call %1").arg(n));
        }
        return inner_->tileInfoEx(xy, zoom, request, mode);
    }
    storage::TileStorageTypeAbilities typeAbilities() const override { return inner_->typeAbilities(); }
    storage::StorageStatePtr state() const override { return inner_->state(); }
    core::Changeable& stateChangeable() override { return inner_->stateChangeable(); }
    const geo::IProjectionSet& projectionSet() const override { return inner_->projectionSet(); }
    storage::TilePyramidNotifier& tileNotifier() override { return inner_->tileNotifier(); }
    QString tileFileName(geo::PointI xy, quint8 zoom, const storage::MapVersionInfoPtr& v) const override {
        return inner_->tileFileName(xy, zoom, v);
    }
    core::Result<storage::TileInfo> tileInfo(geo::PointI xy, quint8 zoom, const storage::MapVersionInfoPtr& v,
                                             storage::LoadMode mode) const override {
        return inner_->tileInfo(xy, zoom, v, mode);
    }
    core::Result<bool> deleteTile(geo::PointI xy, quint8 zoom, const storage::MapVersionInfoPtr& v) override {
        return inner_->deleteTile(xy, zoom, v);
    }
    core::Result<bool> saveTile(geo::PointI xy, quint8 zoom, const storage::MapVersionInfoPtr& v,
                                storage::TileDate d, QByteArrayView type, const QByteArray& data,
                                bool overwrite) override {
        return inner_->saveTile(xy, zoom, v, d, type, data, overwrite);
    }
    core::Result<storage::MapVersionList> listVersions(geo::PointI xy, quint8 zoom,
                                                       const storage::MapVersionRequest& r) const override {
        return inner_->listVersions(xy, zoom, r);
    }
    core::Result<storage::TileRectInfoPtr> tileRectInfo(geo::RectI rect, quint8 zoom,
                                                        const storage::MapVersionRequest& r,
                                                        std::stop_token stop) const override {
        return inner_->tileRectInfo(rect, zoom, r, std::move(stop));
    }
    core::Result<storage::EnumTileInfoPtr> scanTiles(bool a, bool b) override {
        return inner_->scanTiles(a, b);
    }

private:
    std::shared_ptr<storage::ITileStorage> inner_;
    mutable std::atomic<int> calls_{0};
};

// One-shot latch with a mutex: released by the console clock, awaited by the storage hook.
class Gate {
public:
    void release() {
        {
            const std::lock_guard lock(mutex_);
            released_ = true;
        }
        cv_.notify_all();
    }
    bool wait() { // bounded: a lost stop fails the test instead of hanging it
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, std::chrono::seconds(20), [&] { return released_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool released_ = false;
};

class Stream {
public:
    Stream() : file_(std::tmpfile()) {}
    ~Stream() {
        if (file_ != nullptr) {
            std::fclose(file_);
        }
    }
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    std::FILE* get() const { return file_; }
    QByteArray text() const {
        std::fflush(file_);
        std::rewind(file_);
        QByteArray all;
        char buffer[256];
        size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof buffer, file_)) > 0) {
            all.append(buffer, static_cast<qsizetype>(n));
        }
        return all;
    }

private:
    std::FILE* file_;
};

class ExportRunnerTest : public ::testing::Test {
public:
    static void SetUpTestSuite() {
        maptype::test::ensureGuiApp();
        dataDir_ = std::make_unique<QTemporaryDir>();
        app::AppOptions options;
        options.arguments = {QStringLiteral("--datadir=") + dataDir_->path()};
        options.warn = [](const QString&) {};
        options.mapsDirs = {dataDir_->path() + QStringLiteral("/Maps")};
        auto created = app::AppContext::create(options);
        if (!created) {
            throw std::runtime_error("AppContext::create failed");
        }
        app_ = std::move(created.value());
    }
    static void TearDownTestSuite() {
        app_.reset();
        dataDir_.reset();
    }

protected:
    void SetUp() override {
        CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
        ASSERT_TRUE(dir_.isValid());
        versionFactory_ = std::make_shared<maptype::MapVersionFactoryChangeable>(
            std::make_shared<storage::MapVersionFactory>());
        versionRequest_ = std::make_shared<maptype::MapVersionRequestChangeable>(
            std::make_shared<maptype::MapVersionRequestConfig>(QStringLiteral("base-ver")), versionFactory_);
        out_ = dir_.path() + QStringLiteral("/o.mbtiles");
    }

    // A map whose z2 storage holds `png()` at the given tiles; also one tile of z3 (not in the job zooms).
    std::shared_ptr<region::test::FakeMapType> makeMap(std::initializer_list<std::pair<int, int>> tiles) {
        const core::ContentTypeInfo* info = core::ContentTypeRegistry::standard().infoByType("image/png");
        auto cache = std::make_shared<storage::TileInfoMemCache>(100, std::chrono::milliseconds(60'000),
                                                                 storage::ClearStrategy::Never);
        auto ram = std::make_shared<storage::RamTileStorage>(
            storage::tileStorageTypeAbilities(storage::TileStorageKind::Ram),
            storage::tileStorageTypeAbilities(storage::TileStorageKind::Ram).base,
            std::make_shared<storage::MapVersionFactory>(), googleSet(),
            std::make_shared<storage::TilePyramidNotifier>(), *info, cache);
        const auto base = versionRequest_->getStatic()->baseVersion;
        const auto save = [&](int x, int y, quint8 zoom) {
            auto saved = ram->saveTile(geo::PointI(x, y), zoom, base, storage::fromPosixSeconds(1700000000),
                                       QByteArrayView("image/png"), png(), true);
            EXPECT_TRUE(saved.has_value() && *saved);
        };
        for (const auto& t : tiles) {
            save(t.first, t.second, 2);
        }
        save(3, 3, 3);
        storage_ = std::make_shared<HookStorage>(ram);
        auto m = std::make_shared<region::test::FakeMapType>();
        m->contentTypeValue = info;
        m->isBitmapTilesValue = true;
        m->projectionSetValue = googleSet();
        m->versionRequestValue = versionRequest_;
        m->tileStorageValue = storage_;
        return m;
    }

    ResolvedSource source(std::shared_ptr<region::test::FakeMapType> map, bool overlay = false) const {
        ResolvedSource s;
        s.map = std::move(map);
        s.asOverlay = overlay;
        s.outFile = out_;
        s.name = QStringLiteral("Карта");
        s.attribution = QStringLiteral("Автор"); // T-0222: resolveJob derives it, the runner writes it
        s.zooms = {2};                           // T-0245: the effective list, as job().zooms
        return s;
    }

    // T-0249: a map that answers a name (sourceName), for the snapshot description default.
    static std::shared_ptr<region::test::FakeMapType> named(std::shared_ptr<region::test::FakeMapType> map,
                                                            const QString& name) {
        static QTemporaryDir languageDir;
        static auto languages = std::make_shared<core::LanguageManager>(languageDir.path());
        auto gui = std::make_shared<maptype::test::ZmpGuiStub>();
        gui->nameEn = name;
        map->guiConfigValue = std::make_shared<maptype::MapTypeGuiConfig>(languages, gui);
        return map;
    }

    static app::Snapshot snapshotAt(const QDateTime& utc) {
        app::Snapshot s;
        s.version = QStringLiteral("v");
        s.time = utc;
        return s;
    }

    static Job job() {
        Job j;
        j.zooms = {2};
        j.description = QStringLiteral("Описание");
        return j;
    }

    static std::shared_ptr<const geo::IGeometryLonLatPolygon> area() {
        return app_->lonLatFactory()->createPolygonByRect(geo::LonLatRect{-80, 50, 80, -50});
    }

    ExportResult run(const ResolvedSource& s, const Job& j, std::stop_token term = {}) {
        Stream out;
        Stream err;
        Console console(0, true, out.get(), err.get());
        auto r = app::exportSource(s, j, area(), *app_, console, term);
        EXPECT_EQ(out.text(), QByteArray()); // the runner prints nothing on stdout
        return r;
    }

    QStringList files() const { return QDir(dir_.path()).entryList(QDir::Files | QDir::NoDotAndDotDot); }

    static QString scalar(const QString& file, const QByteArray& sql) {
        auto db = SqliteDb::open(file, SqliteDb::OpenMode::ReadOnly);
        EXPECT_TRUE(db.has_value());
        if (!db) {
            return {};
        }
        auto stmt = db->prepare(sql);
        EXPECT_TRUE(stmt.has_value());
        if (!stmt) {
            return {};
        }
        auto step = stmt->step();
        EXPECT_TRUE(step.has_value() && *step == SqliteStmt::StepResult::Row);
        return stmt->columnText(0);
    }

    static void writeFile(const QString& path, const QByteArray& data) {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(data);
    }
    static QByteArray readFile(const QString& path) {
        QFile f(path);
        EXPECT_TRUE(f.open(QIODevice::ReadOnly));
        return f.readAll();
    }

    QTemporaryDir dir_;
    QString out_;
    std::shared_ptr<maptype::MapVersionFactoryChangeable> versionFactory_;
    std::shared_ptr<maptype::MapVersionRequestChangeable> versionRequest_;
    std::shared_ptr<HookStorage> storage_;

    inline static std::unique_ptr<app::AppContext> app_;

private:
    inline static std::unique_ptr<QTemporaryDir> dataDir_;
};

} // namespace

// Proof 1: out exists, only out is left, the row count is the stored tiles of the job zooms (3 of the 4 tiles
// of the area; the z3 tile is outside job.zooms), a base layer with the job texts.
TEST_F(ExportRunnerTest, SuccessRenamesThePartFileAndCountsTheStoredTiles) {
    const ExportResult r = run(source(makeMap({{1, 1}, {2, 1}, {1, 2}})), job());
    ASSERT_EQ(r.outcome, ExportOutcome::Written) << qPrintable(r.error);
    EXPECT_EQ(r.tiles, 3);
    EXPECT_EQ(files(), QStringList{QStringLiteral("o.mbtiles")});
    EXPECT_EQ(r.fileSize, QFileInfo(out_).size());
    EXPECT_GT(r.fileSize, 0);
    EXPECT_EQ(scalar(out_, "SELECT count(*) FROM tiles"), QStringLiteral("3"));
    EXPECT_EQ(scalar(out_, "SELECT value FROM metadata WHERE name='type'"), QStringLiteral("baselayer"));
    EXPECT_EQ(scalar(out_, "SELECT value FROM metadata WHERE name='name'"), QStringLiteral("Карта"));
    EXPECT_EQ(scalar(out_, "SELECT value FROM metadata WHERE name='description'"),
              QStringLiteral("Описание"));
    EXPECT_EQ(scalar(out_, "SELECT value FROM metadata WHERE name='attribution'"), QStringLiteral("Автор"));
    EXPECT_EQ(scalar(out_, "SELECT value FROM metadata WHERE name='scheme'"), QStringLiteral("tms")); // C-4
}

// T-0245: the zooms exported are the ones of the resolved source, not job.zooms (job: z2; source: z3 only).
TEST_F(ExportRunnerTest, ExportsTheZoomsOfTheResolvedSource) {
    ResolvedSource s = source(makeMap({{1, 1}, {2, 1}, {1, 2}}));
    s.zooms = {3};
    const ExportResult r = run(s, job());
    ASSERT_EQ(r.outcome, ExportOutcome::Written) << qPrintable(r.error);
    EXPECT_EQ(r.tiles, 1);
    EXPECT_EQ(scalar(out_, "SELECT max(zoom_level) FROM tiles"), QStringLiteral("3"));
}

// T-0222: the runner writes the attribution of the source, not of the job; an empty one stays empty.
TEST_F(ExportRunnerTest, AttributionIsTheOneOfTheResolvedSource) {
    Job j = job();
    j.attribution = QStringLiteral("Job text"); // not read by the runner
    ResolvedSource base = source(makeMap({{1, 1}}));
    base.attribution = QStringLiteral("Лицензия base");
    ASSERT_EQ(run(base, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, "SELECT value FROM metadata WHERE name='attribution'"),
              QStringLiteral("Лицензия base"));

    ResolvedSource overlay = source(makeMap({{1, 1}}), true);
    overlay.outFile = dir_.filePath(QStringLiteral("ov.mbtiles"));
    overlay.attribution = QStringLiteral("Credit overlay");
    ASSERT_EQ(run(overlay, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(overlay.outFile, "SELECT value FROM metadata WHERE name='attribution'"),
              QStringLiteral("Credit overlay"));
    EXPECT_EQ(scalar(out_, "SELECT value FROM metadata WHERE name='attribution'"),
              QStringLiteral("Лицензия base")); // the base file is untouched
}

TEST_F(ExportRunnerTest, EmptyAttributionIsWrittenEmpty) {
    ResolvedSource s = source(makeMap({{1, 1}}));
    s.attribution.clear();
    ASSERT_EQ(run(s, job()).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, "SELECT value FROM metadata WHERE name='attribution'"), QString());
}

// T-0203: with --progress=lines the export writes `phase export` first and ends with permille 1000 after the
// written file; every record is a whole `progress export` line with a permille in 0..1000.
TEST_F(ExportRunnerTest, LinesModeWritesThePhaseThenProgressEndingWith1000) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get());
    console.setProgressRecords(true);
    Job j = job();
    j.progressMode = app::ProgressMode::Lines;
    auto map = makeMap({{1, 1}, {2, 1}});
    map->guidValue = QUuid::fromString(QStringLiteral("{0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9}"));
    const ExportResult r = app::exportSource(source(map), j, area(), *app_, console, {});
    ASSERT_EQ(r.outcome, ExportOutcome::Written) << qPrintable(r.error);
    const QStringList lines = QString::fromUtf8(err.text()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QString guid = QStringLiteral("{0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9}");
    ASSERT_GE(lines.size(), 2);
    EXPECT_EQ(lines.first(), QStringLiteral("phase\texport\t") + guid);
    EXPECT_EQ(lines.last(), QStringLiteral("progress\texport\t") + guid + QStringLiteral("\t1000"));
    for (const QString& line : lines.mid(1)) {
        const QStringList f = line.split(QLatin1Char('\t'));
        ASSERT_EQ(f.size(), 4) << qPrintable(line); // no human "export N%" line either
        EXPECT_EQ(f[0] + f[1], QStringLiteral("progressexport"));
        EXPECT_GE(f[3].toInt(), 0);
        EXPECT_LE(f[3].toInt(), 1000);
    }
}

TEST_F(ExportRunnerTest, AutoModeWritesNoRecords) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get());
    const ExportResult r = app::exportSource(source(makeMap({{1, 1}})), job(), area(), *app_, console, {});
    ASSERT_EQ(r.outcome, ExportOutcome::Written) << qPrintable(r.error);
    EXPECT_FALSE(err.text().contains("phase\t"));
    EXPECT_FALSE(err.text().contains("progress\t"));
}

TEST_F(ExportRunnerTest, AnOverlaySourceIsWrittenAsAnOverlayLayer) {
    const ExportResult r = run(source(makeMap({{1, 1}}), true), job());
    ASSERT_EQ(r.outcome, ExportOutcome::Written) << qPrintable(r.error);
    EXPECT_EQ(r.tiles, 1);
    EXPECT_EQ(scalar(out_, "SELECT value FROM metadata WHERE name='type'"), QStringLiteral("overlay"));
}

// Proof 2 (C-10)
TEST_F(ExportRunnerTest, AnEmptyResultIsReportedAndLeavesNoFile) {
    const ExportResult r = run(source(makeMap({})), job());
    EXPECT_EQ(r.outcome, ExportOutcome::Empty);
    EXPECT_EQ(r.error, QStringLiteral("no tiles of Карта in the cache for this area"));
    EXPECT_EQ(r.tiles, 0);
    EXPECT_TRUE(files().isEmpty()) << qPrintable(files().join(','));
}

// Proof 3 and 5 (old out untouched): the termination fires at the 2nd lookup. The hook runs on the export
// thread and waits for the console clock, which only the calling thread's progress line reaches, after
// request_stop().
TEST_F(ExportRunnerTest, ATerminationDuringTheExportStopsTheTaskAndDeletesEveryFile) {
    writeFile(out_, "OLD");
    auto map = makeMap({{1, 1}, {2, 1}, {1, 2}});
    std::stop_source termination;
    Gate gate;
    std::atomic<bool> gateOpened{false};
    storage_->hookAt = 2;
    storage_->hook = [&] {
        termination.request_stop();
        gateOpened = gate.wait();
    };
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get(), [&] {
        gate.release();
        return qint64{0};
    });
    Job j = job();
    j.overwrite = true;
    const auto start = std::chrono::steady_clock::now();
    const ExportResult r = app::exportSource(source(map), j, area(), *app_, console, termination.get_token());
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(gateOpened.load()); // the calling thread reached the progress line (so it had requested stop)
    EXPECT_EQ(r.outcome, ExportOutcome::Interrupted);
    EXPECT_TRUE(r.error.isEmpty());
    EXPECT_LT(elapsed, std::chrono::seconds(2));
    EXPECT_EQ(storage_->calls(), 2); // the task saw the stop before the 3rd lookup
    EXPECT_EQ(files(), QStringList{QStringLiteral("o.mbtiles")});
    EXPECT_EQ(readFile(out_), QByteArray("OLD"));
    // The progress line only (T-0267): the interrupted run keeps its reached ratio (no 100 block finished),
    // and the final line equal to the printed one is not repeated.
    EXPECT_EQ(err.text(), QByteArray("Карта  export  0%\n"));
}

// T-0267: the task refreshes the ratio every 100 tiles only; a completed export ends with 100%, once.
TEST_F(ExportRunnerTest, NonTerminalFinalLineOfACompletedExportIs100) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get(), [] { return qint64{0}; });
    const ExportResult r =
        app::exportSource(source(makeMap({{1, 1}, {2, 1}, {1, 2}})), job(), area(), *app_, console, {});
    ASSERT_EQ(r.outcome, ExportOutcome::Written) << qPrintable(r.error);
    EXPECT_EQ(err.text(), QByteArray("Карта  export  100%\n"));
}

// Proof 4
TEST_F(ExportRunnerTest, AStalePartFileAndItsSideFilesAreReplacedNotAppendedTo) {
    writeFile(out_ + QStringLiteral(".part"), "junk, not a database");
    writeFile(out_ + QStringLiteral(".part-journal"), "junk journal");
    writeFile(out_ + QStringLiteral(".part-wal"), "junk wal");
    writeFile(out_ + QStringLiteral(".part-shm"), "junk shm");
    const ExportResult r = run(source(makeMap({{1, 1}, {2, 2}})), job());
    ASSERT_EQ(r.outcome, ExportOutcome::Written) << qPrintable(r.error);
    EXPECT_EQ(r.tiles, 2);
    EXPECT_EQ(files(), QStringList{QStringLiteral("o.mbtiles")});
    EXPECT_EQ(scalar(out_, "SELECT count(*) FROM tiles"), QStringLiteral("2"));
}

// Proof 5, success half: the old file is replaced.
TEST_F(ExportRunnerTest, WithOverwriteAnExistingOutIsReplacedAfterASuccessfulExport) {
    writeFile(out_, "OLD");
    Job j = job();
    j.overwrite = true;
    const ExportResult r = run(source(makeMap({{1, 1}, {2, 1}})), j);
    ASSERT_EQ(r.outcome, ExportOutcome::Written) << qPrintable(r.error);
    EXPECT_EQ(files(), QStringList{QStringLiteral("o.mbtiles")});
    EXPECT_EQ(scalar(out_, "SELECT count(*) FROM tiles"), QStringLiteral("2"));
}

// Spec app-cli.md 1.5: the replacing rename leaves the new content and no .part.
TEST_F(ExportRunnerTest, WithOverwriteTheRenameReplacesTheOldFileAndLeavesNoPart) {
    writeFile(out_, "OLD");
    Job j = job();
    j.overwrite = true;
    const ExportResult r = run(source(makeMap({{1, 1}})), j);
    ASSERT_EQ(r.outcome, ExportOutcome::Written) << qPrintable(r.error);
    EXPECT_NE(readFile(out_), QByteArray("OLD"));
    EXPECT_EQ(files(), QStringList{QStringLiteral("o.mbtiles")});
}

// T-0172 caller contract: a bare relative out name (`--out o.mbtiles`) is made absolute.
TEST_F(ExportRunnerTest, ABareRelativeOutNameIsWrittenInTheCurrentDirectory) {
    const auto map = makeMap({{1, 1}}); // before the chdir: the fixture is read by a relative path
    const QString oldCwd = QDir::currentPath();
    ASSERT_TRUE(QDir::setCurrent(dir_.path()));
    out_ = QStringLiteral("o.mbtiles");
    const ExportResult r = run(source(map), job());
    QDir::setCurrent(oldCwd);
    ASSERT_EQ(r.outcome, ExportOutcome::Written) << qPrintable(r.error);
    EXPECT_EQ(files(), QStringList{QStringLiteral("o.mbtiles")});
}

// Proof 5, failure half: the directory cannot be written after the checks.
TEST_F(ExportRunnerTest, AnUnwritableDirectoryFailsAndLeavesTheOldOutUntouched) {
    if (cg::core::platform::directoryPermissionsIgnored()) {
        GTEST_SKIP() << "root ignores directory permissions";
    }
    const QString sub = dir_.path() + QStringLiteral("/ro");
    ASSERT_TRUE(QDir().mkpath(sub));
    out_ = sub + QStringLiteral("/o.mbtiles");
    writeFile(out_, "OLD");
    ASSERT_TRUE(QFile::setPermissions(sub, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    Job j = job();
    j.overwrite = true;
    const ExportResult r = run(source(makeMap({{1, 1}})), j);
    QFile::setPermissions(sub, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    EXPECT_EQ(r.outcome, ExportOutcome::Failed);
    EXPECT_FALSE(r.error.isEmpty());
    EXPECT_EQ(QDir(sub).entryList(QDir::Files | QDir::NoDotAndDotDot),
              QStringList{QStringLiteral("o.mbtiles")});
    EXPECT_EQ(readFile(out_), QByteArray("OLD"));
}

// The file appeared since the checks and overwrite is off: not replaced (ticket step 5).
TEST_F(ExportRunnerTest, AnExistingOutWithoutOverwriteFailsAndIsKept) {
    writeFile(out_, "OLD");
    const ExportResult r = run(source(makeMap({{1, 1}})), job());
    EXPECT_EQ(r.outcome, ExportOutcome::Failed);
    EXPECT_EQ(r.error, QStringLiteral("the file %1 exists").arg(out_));
    EXPECT_EQ(files(), QStringList{QStringLiteral("o.mbtiles")});
    EXPECT_EQ(readFile(out_), QByteArray("OLD"));
}

// Proof 6: a storage error ends the export (the writer is closed, T-0172); the error text is returned.
TEST_F(ExportRunnerTest, AStorageErrorFailsWithItsTextAndDeletesEveryFile) {
    writeFile(out_, "OLD");
    auto map = makeMap({{1, 1}, {2, 1}, {1, 2}});
    storage_->failAt = 2;
    Job j = job();
    j.overwrite = true;
    const ExportResult r = run(source(map), j);
    EXPECT_EQ(r.outcome, ExportOutcome::Failed);
    EXPECT_TRUE(r.error.contains(QStringLiteral("scripted failure at call 2"))) << qPrintable(r.error);
    EXPECT_EQ(files(), QStringList{QStringLiteral("o.mbtiles")});
    EXPECT_EQ(readFile(out_), QByteArray("OLD"));
}

// T-0249 (Q-W3): the default attribution's {year} is the snapshot's UTC year; a static source or an unknown
// time takes the current UTC year; --attribution is verbatim.
TEST_F(ExportRunnerTest, YearOfTheDefaultAttributionIsTheSnapshotsOrTheCurrent) {
    const QString current = QString::number(QDateTime::currentDateTimeUtc().date().year());
    ResolvedSource s = source(named(makeMap({{1, 1}}), QStringLiteral("Wx")));
    s.attribution = QStringLiteral("Data {year} and {year}");
    Job j = job();
    j.attribution.clear();
    const QByteArray sql = "SELECT value FROM metadata WHERE name='attribution'";

    s.snapshot = snapshotAt(cg::core::platform::utcDateTime(QDate(2026, 10, 15), QTime(20, 10)));
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Data 2026 and 2026"));

    s.snapshot =
        snapshotAt(cg::core::platform::utcDateTime(QDate(2024, 12, 31), QTime(23, 59))); // not the local year
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Data 2024 and 2024"));

    s.snapshot = snapshotAt(QDateTime()); // unknown time
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Data ") + current + QStringLiteral(" and ") + current);

    s.snapshot.reset(); // static
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Data ") + current + QStringLiteral(" and ") + current);

    s.snapshot = snapshotAt(cg::core::platform::utcDateTime(QDate(2026, 10, 15), QTime(20, 10)));
    s.attribution = QStringLiteral("X {year}"); // what resolveJob derives from --attribution X {year}
    j.attribution = QStringLiteral("X {year}");
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("X {year}"));
}

// T-0249 (Q-W4): the default description of a snapshot export; --description wins; static stays empty;
// the apostrophe of a derived text is removed with a warning (S-11).
TEST_F(ExportRunnerTest, DefaultDescriptionOfASnapshotExportNamesTheSnapshot) {
    const QByteArray sql = "SELECT value FROM metadata WHERE name='description'";
    ResolvedSource s = source(named(makeMap({{1, 1}}), QStringLiteral("Wx Radar")));
    s.snapshot = snapshotAt(cg::core::platform::utcDateTime(QDate(2026, 10, 15), QTime(20, 10)));
    Job j = job();
    j.description.clear();
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Wx Radar, snapshot 2026-10-15 20:10 UTC"));

    j.description = QStringLiteral("D");
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("D"));

    j.description.clear();
    s.snapshot.reset();
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql),
              QStringLiteral("Created by chart-grabber-cli ") +
                  QString::fromLatin1(cg::core::versionString())); // the writer's own default

    ResolvedSource q = source(named(makeMap({{1, 1}}), QStringLiteral("O'Brien")));
    q.snapshot = snapshotAt(cg::core::platform::utcDateTime(QDate(2026, 10, 15), QTime(20, 10)));
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get());
    QFile::remove(out_);
    ASSERT_EQ(app::exportSource(q, j, area(), *app_, console, {}).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("OBrien, snapshot 2026-10-15 20:10 UTC"));
    EXPECT_NE(err.text().indexOf("the apostrophe is removed from the description of \"O'Brien\""), -1)
        << err.text().toStdString();
}

// T-0327: the default MBTiles name of a snapshot export carries the frame time; --name wins; static
// unchanged.
TEST_F(ExportRunnerTest, DefaultNameOfASnapshotExportCarriesTheFrameTime) {
    const QByteArray sql = "SELECT value FROM metadata WHERE name='name'";
    ResolvedSource s = source(makeMap({{1, 1}}));
    Job j = job();
    j.name.clear();
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Карта")); // static: unchanged

    s.snapshot = snapshotAt(cg::core::platform::utcDateTime(QDate(2026, 10, 15), QTime(20, 10)));
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    const QString first = scalar(out_, sql);
    EXPECT_EQ(first, QStringLiteral("Карта 2026-10-15 20:10 UTC"));

    s.snapshot = snapshotAt(cg::core::platform::utcDateTime(QDate(2026, 10, 15), QTime(21, 10)));
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Карта 2026-10-15 21:10 UTC"));
    EXPECT_NE(scalar(out_, sql), first);

    j.name = QStringLiteral("Mine");
    s.name = j.name; // resolveJob: --name becomes the source's name
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Mine")); // explicit name wins

    j.name.clear();
    s.name = QStringLiteral("Карта");
    s.snapshot->time = QDateTime(); // unknown frame time: nothing to append
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Карта"));
}

// T-0330: an overlay from a snapshot source carries the frame time in its name too; --name names the base
// only (resolveJob), so it does not stop the overlay; a static overlay is unchanged.
TEST_F(ExportRunnerTest, OverlayOfASnapshotSourceCarriesTheFrameTimeInItsName) {
    const QByteArray sql = "SELECT value FROM metadata WHERE name='name'";
    ResolvedSource s = source(makeMap({{1, 1}}), true);
    s.name = QStringLiteral("Слой");
    Job j = job();
    j.name.clear();
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Слой")); // static overlay: unchanged

    s.snapshot = snapshotAt(cg::core::platform::utcDateTime(QDate(2026, 10, 15), QTime(20, 10)));
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Слой 2026-10-15 20:10 UTC"));

    j.name = QStringLiteral("Mine"); // the base's name; the overlay's name stays the source's
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Слой 2026-10-15 20:10 UTC"));

    s.snapshot->time = QDateTime(); // unknown frame time: nothing to append
    QFile::remove(out_);
    ASSERT_EQ(run(s, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Слой"));
}

// T-0330: a static base with a snapshot overlay: the base is unchanged, the overlay has the overlay's frame
// time.
TEST_F(ExportRunnerTest, StaticBaseKeepsItsNameBesideASnapshotOverlay) {
    const QByteArray sql = "SELECT value FROM metadata WHERE name='name'";
    ResolvedSource base = source(makeMap({{1, 1}}));
    ResolvedSource layer = source(makeMap({{1, 1}}), true);
    layer.name = QStringLiteral("Слой");
    layer.snapshot = snapshotAt(cg::core::platform::utcDateTime(QDate(2026, 10, 15), QTime(20, 10)));
    Job j = job();
    j.name.clear();
    QFile::remove(out_);
    ASSERT_EQ(run(base, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Карта"));
    QFile::remove(out_);
    ASSERT_EQ(run(layer, j).outcome, ExportOutcome::Written);
    EXPECT_EQ(scalar(out_, sql), QStringLiteral("Слой 2026-10-15 20:10 UTC"));
}
