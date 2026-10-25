// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/region/export_task_to_mbtiles (Src/RegionProcess/Export/MBTiles/u_ExportTaskToMBTiles.pas
// :101-290, direct-copy branch). T-0172 (E4 of docs/specs/region-export.md).
// FEAT-REG-069 (Export to MBTiles) is CONTRIBUTED to, not closed: the recompose branch is absent.
//
// Rig: a scripted ITileStorage (real PNG / JPEG fixture bytes of different lengths, missing tiles, TNE
// markers, hooks run at the start of the N-th tileInfoEx) in the Google 256x256 grid; the task writes
// into a QTemporaryDir and the file is read back with storage::sqlite::MbtilesConnection (every tile of
// the grid) and with SqliteDb queries.
//
// Hand derivation of the grid (Google, 256x256; tile row y = floor((1 - asinh(tan(lat))/pi) / 2 * 2^z)):
//  - rectangle lon -80..80, lat -50..50: z2 tiles x 1..2, y 1..2 (4); z3 tiles x 2..5, y 2..5 (16);
//    the lowest zoom (2) covers lon -90..90, lat +-66.51326044 (y boundary 1 = atan(sinh(pi/2)));
//  - quadrilateral (-80,50) (80,50) (80,20) (-80,-50): z2 all 4 tiles, z3 12 tiles: rows y2, y3 full
//    (x 2..5), y4 x 2..4, y5 x 2 only; (5,4) (3,5) (4,5) (5,5) lie in the bounding rectangle but outside;
//  - zoom 7 (128 tiles, 2.8125 degrees): lon -179..179, lat -84..84 -> x 0..127, y 3..124 = 15616 tiles;
//    lon -151..-110, lat 55..37.5 -> x 10..24, y 40..49 = 150 tiles.
// Cancel: the stop flag is read at the head of the loop (:244-246), BEFORE GetTileInfoEx (:249); a stop
// requested inside the N-th read therefore still processes that tile: N tiles are written.
#include "cg/core/content_type.h"
#include "cg/core/result.h"
#include "cg/geo/datum.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_projected.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/geo/tile_count.h"
#include "cg/region/export_task_to_mbtiles.h"
#include "cg/region/region_process_progress_info.h"
#include "cg/storage/map_version.h"
#include "cg/storage/sqlite/mbtiles_connection.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/storage_state.h"
#include "cg/storage/tile_info.h"
#include "cg/storage/tile_pyramid_notifier.h"
#include "cg/storage/tile_storage.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <memory>
#include <stop_token>
#include <tuple>
#include <utility>
#include <vector>

#include "../corpus_skip.h"

using namespace cg;
using core::ErrorCode;
using geo::LonLat;
using geo::LonLatRect;
using geo::PointI;
using region::ExportTaskToMbtiles;
using region::RegionProcessProgressInfo;
using storage::sqlite::SqliteDb;
using storage::sqlite::SqliteStmt;

namespace {

using Key = std::tuple<int, int, int>; // zoom, x, y
using TileMap = std::map<Key, QByteArray>;

struct Entry {
    storage::TileInfoType type = storage::TileInfoType::Exists;
    QByteArray data;
};

geo::ProjectionSetPtr googleSet() {
    static const auto datums = std::make_shared<const geo::DatumFactory>(geo::HashSeed{});
    static const geo::ProjectionSetFactory factory{geo::HashSeed{}, datums};
    return factory.byCode(geo::kProjEpsgGoogle, geo::kTileSplitQuadrate256x256);
}

// CG_FIXTURES_DIR comes from CMake; the nocmake fallback finds tests/fixtures from the working directory.
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

QByteArray readFixture(const char* name) {
    QFile f(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/") + QString::fromLatin1(name));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << name;
    return f.readAll();
}

// Three real files of different lengths (28.6K, 18.3K, 8.5K), with interior zero bytes.
const QByteArray& png() {
    static const QByteArray d = readFixture("test.png");
    return d;
}
const QByteArray& jpg() {
    static const QByteArray d = readFixture("test.jpg");
    return d;
}
const QByteArray& png2() {
    static const QByteArray d = readFixture("test2.png");
    return d;
}

Entry exists(const QByteArray& data) {
    return {storage::TileInfoType::Exists, data};
}
Entry tne() {
    return {storage::TileInfoType::TneExists, {}};
}

class ScriptedStorage final : public storage::ITileStorage {
public:
    explicit ScriptedStorage(std::map<Key, Entry> entries = {}) : entries_(std::move(entries)) {}

    // when non-empty: every tile of the grid exists with this payload
    QByteArray constantPayload;
    std::function<void(int)> onCall; // called at the start of the N-th tileInfoEx (1-based)
    int missingEvery = 0;            // with constantPayload: every N-th read answers "unknown" (legacy nil)
    int errorAt = 0;                 // the N-th tileInfoEx returns a Result error

    mutable int calls = 0;
    mutable std::vector<Key> requested;
    mutable storage::LoadMode lastMode = storage::LoadMode::AsIs;
    mutable bool lastShowOthers = false;

    storage::TileStorageTypeAbilities typeAbilities() const override {
        return {storage::TileStorageAbilities::readWrite(), storage::VersionSupport::MultiVersions, false,
                storage::StorageClass::Other};
    }
    storage::StorageStatePtr state() const override {
        return std::make_shared<const storage::StorageState>(
            storage::StorageState{true, true, true, true, true});
    }
    core::Changeable& stateChangeable() override { return stateHolder_; }
    const geo::IProjectionSet& projectionSet() const override { return *projectionSet_; }
    storage::TilePyramidNotifier& tileNotifier() override { return notifier_; }
    QString tileFileName(PointI, quint8, const storage::MapVersionInfoPtr&) const override { return {}; }
    core::Result<storage::TileInfo> tileInfo(PointI, quint8, const storage::MapVersionInfoPtr&,
                                             storage::LoadMode) const override {
        return core::fail(ErrorCode::Unsupported, QStringLiteral("not used by the task"));
    }
    core::Result<storage::TileInfo> tileInfoEx(PointI xy, quint8 zoom,
                                               const storage::MapVersionRequest& request,
                                               storage::LoadMode mode) const override {
        ++calls;
        requested.emplace_back(zoom, xy.x(), xy.y());
        lastMode = mode;
        lastShowOthers = request.showOtherVersions;
        if (onCall)
            onCall(calls);
        if (errorAt != 0 && calls == errorAt)
            return core::fail(ErrorCode::IoError, QStringLiteral("scripted storage failure"));
        storage::TileInfo info; // type Unknown: legacy's nil
        if (!constantPayload.isEmpty() && !(missingEvery != 0 && calls % missingEvery == 0)) {
            info.type = storage::TileInfoType::Exists;
            info.data = constantPayload;
        } else if (const auto it = entries_.find({zoom, xy.x(), xy.y()}); it != entries_.end()) {
            info.type = it->second.type;
            info.data = it->second.data;
        } else if (!missingIsNotExists_) {
            info.type = storage::TileInfoType::Unknown;
        } else {
            info.type = storage::TileInfoType::NotExists;
        }
        info.size = static_cast<quint32>(info.data.size());
        return info;
    }
    core::Result<bool> deleteTile(PointI, quint8, const storage::MapVersionInfoPtr&) override {
        return false;
    }
    core::Result<bool> saveTile(PointI, quint8, const storage::MapVersionInfoPtr&, storage::TileDate,
                                QByteArrayView, const QByteArray&, bool) override {
        return false;
    }
    core::Result<storage::MapVersionList> listVersions(PointI, quint8,
                                                       const storage::MapVersionRequest&) const override {
        return storage::MapVersionList{};
    }
    core::Result<storage::TileRectInfoPtr> tileRectInfo(geo::RectI, quint8, const storage::MapVersionRequest&,
                                                        std::stop_token) const override {
        return core::fail(ErrorCode::Unsupported, QStringLiteral("not used by the task"));
    }
    core::Result<storage::EnumTileInfoPtr> scanTiles(bool, bool) override {
        return core::fail(ErrorCode::Unsupported, QStringLiteral("not used by the task"));
    }

    void answerNotExistsForMissing() { missingIsNotExists_ = true; }

private:
    std::map<Key, Entry> entries_;
    bool missingIsNotExists_ = false;
    geo::ProjectionSetPtr projectionSet_ = googleSet();
    storage::StorageStateHolder stateHolder_{storage::TileStorageAbilities::readWrite()};
    storage::TilePyramidNotifier notifier_;
};

using PolygonPtr = std::shared_ptr<const geo::IGeometryLonLatPolygon>;

PolygonPtr makePolygon(const LonLatRect& bounds, std::vector<LonLat> ring) {
    geo::GeometryLonLatPolygonBuilder builder;
    builder.addOuter(bounds, std::make_shared<const std::vector<LonLat>>(std::move(ring)));
    return std::static_pointer_cast<const geo::IGeometryLonLatPolygon>(builder.makeStaticAndClear());
}

PolygonPtr rectPolygon(double left, double top, double right, double bottom) {
    return makePolygon({left, top, right, bottom},
                       {{left, top}, {right, top}, {right, bottom}, {left, bottom}});
}

PolygonPtr rectangle() { // z2 4 tiles, z3 16 tiles
    return rectPolygon(-80, 50, 80, -50);
}
PolygonPtr quadrilateral() { // z2 4 tiles, z3 12 tiles
    return makePolygon({-80, 50, 80, -50}, {{-80, 50}, {80, 50}, {80, 20}, {-80, -50}});
}

struct Opt {
    QString path;
    PolygonPtr polygon = rectangle();
    std::vector<quint8> zooms = {2, 3};
    std::shared_ptr<ScriptedStorage> store = std::make_shared<ScriptedStorage>();
    bool direct = true;
    bool xyz = false;
    bool tileMill = false;
    QString name = QStringLiteral("map");
    QString description = QStringLiteral("desc");
    QString attribution = QStringLiteral("attr");
    bool isLayer = false;
    QString format = QStringLiteral("png");
    bool force = true;
    bool replace = true;
    std::shared_ptr<RegionProcessProgressInfo> progress;
    std::shared_ptr<const storage::MapVersionRequest> version = makeVersion();

    static std::shared_ptr<const storage::MapVersionRequest> makeVersion() {
        auto v = std::make_shared<storage::MapVersionRequest>();
        v->showOtherVersions = true;
        return v;
    }
};

std::unique_ptr<ExportTaskToMbtiles> makeTask(Opt& o, bool nullStorage = false) {
    if (!o.progress)
        o.progress = std::make_shared<RegionProcessProgressInfo>(std::stop_token{});
    std::shared_ptr<storage::ITileStorage> store = o.store;
    if (nullStorage)
        store = nullptr;
    return std::make_unique<ExportTaskToMbtiles>(o.progress, o.path,
                                                 std::make_shared<const geo::TileIteratorFactory>(
                                                     std::make_shared<const geo::GeometryProjectedFactory>()),
                                                 o.polygon, o.zooms, store, o.version, o.direct, o.xyz,
                                                 o.tileMill, o.name, o.description, o.attribution, o.isLayer,
                                                 o.format, o.force, o.replace);
}

QString fileIn(const QTemporaryDir& d) {
    return d.path() + QStringLiteral("/out/t.mbtiles"); // the directory does not exist yet
}

// ---- read-back --------------------------------------------------------------------------------------

// Every tile of the grid of the given zooms, read through the landed cache connector.
TileMap readBack(const QString& file, const std::vector<int>& zooms) {
    TileMap result;
    const core::ContentTypeInfo pngType{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                        core::ContentKind::Bitmap};
    auto opened = storage::sqlite::MbtilesConnection::open(
        true, file, pngType, core::ContentTypeRegistry::standard(), googleSet(), nullptr);
    EXPECT_TRUE(opened.has_value());
    if (!opened)
        return result;
    auto conn = std::move(*opened);
    for (const int z : zooms) {
        for (int x = 0; x < (1 << z); ++x) {
            for (int y = 0; y < (1 << z); ++y) {
                auto info = conn->fetchOne(PointI(x, y), static_cast<quint8>(z), storage::LoadMode::WithData);
                EXPECT_TRUE(info.has_value());
                if (info && info->type == storage::TileInfoType::Exists)
                    result[{z, x, y}] = info->data;
            }
        }
    }
    return result;
}

std::vector<QStringList> query(const QString& path, const QByteArray& sql, int cols) {
    std::vector<QStringList> rows;
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
    EXPECT_TRUE(db.has_value());
    if (!db)
        return rows;
    auto stmt = db->prepare(sql);
    EXPECT_TRUE(stmt.has_value());
    if (!stmt)
        return rows;
    for (;;) {
        auto step = stmt->step();
        if (!step || *step != SqliteStmt::StepResult::Row)
            break;
        QStringList row;
        for (int i = 0; i < cols; ++i)
            row.push_back(stmt->columnText(i));
        rows.push_back(row);
    }
    return rows;
}

int count(const QString& path, const QByteArray& sql) {
    const auto rows = query(path, sql, 1);
    return rows.size() == 1 ? rows[0][0].toInt() : -1;
}

QString meta(const QString& path, const QString& name) {
    const auto rows = query(path, ("SELECT value FROM metadata WHERE name='" + name + "'").toUtf8(), 1);
    return rows.size() == 1 ? rows[0][0] : QStringLiteral("<%1 rows>").arg(rows.size());
}

// The index of the Classic flavour is created by close() (:500-517) and nowhere else.
int classicIndexCount(const QString& path) {
    return count(path, "SELECT count(*) FROM sqlite_master WHERE name='tiles_idx'");
}

// "The writer is released": nothing holds a lock on the file, so an exclusive transaction can start
// (the writer runs with locking_mode EXCLUSIVE until its handle is closed).
::testing::AssertionResult fileIsFree(const QString& path) {
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
    if (!db)
        return ::testing::AssertionFailure() << "cannot open: " << db.error().what().toStdString();
    if (auto r = db->exec("BEGIN EXCLUSIVE"); !r)
        return ::testing::AssertionFailure() << "file is locked: " << r.error().what().toStdString();
    (void)db->exec("ROLLBACK");
    return ::testing::AssertionSuccess();
}

// ---- the stored tiles of the end-to-end rig ---------------------------------------------------------

std::map<Key, Entry> storedTiles() {
    return {
        // z2: (1,2) is missing; (2,2) is a TNE marker; (0,0) lies outside every polygon
        {{2, 1, 1}, exists(png())},
        {{2, 2, 1}, exists(jpg())},
        {{2, 2, 2}, tne()},
        {{2, 0, 0}, exists(jpg())},
        // z3: (3,3) is missing; (5,2) and (4,4) are TNE markers; (0,0) outside
        {{3, 2, 2}, exists(png())},
        {{3, 3, 2}, exists(jpg())},
        {{3, 4, 2}, exists(png2())},
        {{3, 5, 2}, tne()},
        {{3, 2, 3}, exists(jpg())},
        {{3, 4, 3}, exists(png2())},
        {{3, 5, 3}, exists(png())},
        {{3, 2, 4}, exists(png())},
        {{3, 3, 4}, exists(jpg())},
        {{3, 4, 4}, tne()},
        {{3, 5, 4}, exists(jpg())},
        {{3, 2, 5}, exists(png2())},
        {{3, 3, 5}, exists(png())},
        {{3, 4, 5}, exists(jpg())},
        {{3, 5, 5}, exists(png2())},
        {{3, 0, 0}, exists(png())},
    };
}

TileMap expectedForRectangle() {
    return {
        {{2, 1, 1}, png()}, {{2, 2, 1}, jpg()},  {{3, 2, 2}, png()}, {{3, 3, 2}, jpg()}, {{3, 4, 2}, png2()},
        {{3, 2, 3}, jpg()}, {{3, 4, 3}, png2()}, {{3, 5, 3}, png()}, {{3, 2, 4}, png()}, {{3, 3, 4}, jpg()},
        {{3, 5, 4}, jpg()}, {{3, 2, 5}, png2()}, {{3, 3, 5}, png()}, {{3, 4, 5}, jpg()}, {{3, 5, 5}, png2()},
    };
}

TileMap expectedForQuadrilateral() { // (5,4) (3,5) (4,5) (5,5) of z3 are stored but outside the polygon
    return {
        {{2, 1, 1}, png()},  {{2, 2, 1}, jpg()}, {{3, 2, 2}, png()},  {{3, 3, 2}, jpg()},
        {{3, 4, 2}, png2()}, {{3, 2, 3}, jpg()}, {{3, 4, 3}, png2()}, {{3, 5, 3}, png()},
        {{3, 2, 4}, png()},  {{3, 3, 4}, jpg()}, {{3, 2, 5}, png2()},
    };
}

// the tile-aligned rectangle of zoom 2 (x 1..2, y 1..2), not the polygon bounds -80,-50,80,50
const char* const kAlignedBounds = "-90.00000000,-66.51326044,90.00000000,66.51326044";

} // namespace

// ---- 2. end to end ---------------------------------------------------------------------------------

TEST(ExportTaskToMbtiles, ExportsExactlyTheStoredTilesOfTheIteratorInBothFlavoursAndSchemes) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    struct Shape {
        const char* label;
        PolygonPtr polygon;
        TileMap expected;
        qint64 total;
    };
    const std::vector<Shape> shapes = {{"rectangle", rectangle(), expectedForRectangle(), 20},
                                       {"quadrilateral", quadrilateral(), expectedForQuadrilateral(), 16}};
    for (const Shape& shape : shapes) {
        for (const bool tileMill : {false, true}) {
            for (const bool xyz : {false, true}) {
                const std::string label = std::string(shape.label) + (tileMill ? " tilemill" : " classic") +
                                          (xyz ? " xyz" : " tms");
                QTemporaryDir d;
                ASSERT_TRUE(d.isValid());
                Opt o;
                o.path = fileIn(d);
                o.polygon = shape.polygon;
                o.store = std::make_shared<ScriptedStorage>(storedTiles());
                o.tileMill = tileMill;
                o.xyz = xyz;
                auto task = makeTask(o);
                const auto r = task->processRegion();
                ASSERT_TRUE(r.has_value()) << label << " " << r.error().what().toStdString();

                EXPECT_EQ(readBack(o.path, {2, 3}), shape.expected) << label;
                EXPECT_EQ(count(o.path, "SELECT count(*) FROM tiles"),
                          static_cast<int>(shape.expected.size()))
                    << label;
                EXPECT_EQ(o.store->calls, shape.total)
                    << label; // every tile of the iterators, missing ones too
                EXPECT_EQ(o.store->lastMode, storage::LoadMode::WithData) << label;
                EXPECT_TRUE(o.store->lastShowOthers) << label; // the map version request reaches the storage

                EXPECT_EQ(meta(o.path, "bounds"), kAlignedBounds) << label;
                EXPECT_EQ(meta(o.path, "minzoom"), "2") << label;
                EXPECT_EQ(meta(o.path, "maxzoom"), "3") << label;
                EXPECT_EQ(meta(o.path, "scheme"), xyz ? "xyz" : "tms") << label;

                EXPECT_EQ(o.progress->caption(), QStringLiteral("Export")) << label;
                EXPECT_EQ(o.progress->firstLine(), QStringLiteral("Total to save: %1 files").arg(shape.total))
                    << label;
                // not forced to 100 % at the end (:266): 20 / 16 tiles never reach a multiple of 100
                EXPECT_EQ(o.progress->processedRatio(), 0.0) << label;
                EXPECT_EQ(o.progress->secondLine(), QStringLiteral("Processed 0")) << label;

                EXPECT_TRUE(fileIsFree(o.path)) << label; // close ran, the task is still alive
                EXPECT_FALSE(o.progress->finished()) << label;
                task.reset();
                EXPECT_TRUE(o.progress->finished()) << label;
            }
        }
    }
}

TEST(ExportTaskToMbtiles, ConstructorArgumentsReachTheWriterInTheLegacyOrder) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    for (const bool tileMill : {false, true}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Opt o;
        o.path = fileIn(d); // the directory part is created by the task (:195-199), the file part is used
        o.store = std::make_shared<ScriptedStorage>(storedTiles());
        o.zooms = {2};
        o.tileMill = tileMill;
        o.xyz = true;
        o.name = QStringLiteral("Карта");
        o.description = QStringLiteral("Описание карты");
        o.attribution = QStringLiteral("Автор");
        o.isLayer = true;
        o.format = QStringLiteral("jpeg");
        auto task = makeTask(o);
        ASSERT_TRUE(task->processRegion().has_value()) << tileMill;
        EXPECT_TRUE(QFileInfo(o.path).isFile()) << tileMill;
        EXPECT_EQ(meta(o.path, "name"), "Карта") << tileMill;
        EXPECT_EQ(meta(o.path, "description"), "Описание карты") << tileMill;
        EXPECT_EQ(meta(o.path, "attribution"), "Автор") << tileMill;
        EXPECT_EQ(meta(o.path, "type"), "overlay") << tileMill;
        EXPECT_EQ(meta(o.path, "format"), "jpeg") << tileMill;
        EXPECT_EQ(meta(o.path, "scheme"), "xyz") << tileMill;
    }
}

TEST(ExportTaskToMbtiles, AppendKeepsTheOldTilesAndTheReplaceFlagDecidesDuplicates) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    // first export: (3,2,2)=png and (3,3,3)=jpg. Second export (forceDropTarget false, replaceTiles true)
    // stores only (3,2,2)=png2.
    for (const bool tileMill : {false, true}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Opt first;
        first.path = fileIn(d);
        first.zooms = {3};
        first.tileMill = tileMill;
        first.store = std::make_shared<ScriptedStorage>(
            std::map<Key, Entry>{{{3, 2, 2}, exists(png())}, {{3, 3, 3}, exists(jpg())}});
        ASSERT_TRUE(makeTask(first)->processRegion().has_value()) << tileMill;

        Opt second;
        second.path = first.path;
        second.zooms = {3};
        second.tileMill = tileMill;
        second.force = false;
        second.replace = true;
        second.store = std::make_shared<ScriptedStorage>(std::map<Key, Entry>{{{3, 2, 2}, exists(png2())}});
        ASSERT_TRUE(makeTask(second)->processRegion().has_value()) << tileMill;

        if (tileMill) { // map_index is unique: REPLACE keeps the new id for (3,2,2); the old (3,3,3) stays
            const TileMap expected = {{{3, 2, 2}, png2()}, {{3, 3, 3}, jpg()}};
            EXPECT_EQ(readBack(first.path, {3}), expected);
        } else { // the Classic tiles table has no unique key: the duplicate is kept (LC-1)
            EXPECT_EQ(count(first.path, "SELECT count(*) FROM tiles"), 3);
            EXPECT_EQ(count(first.path, "SELECT count(*) FROM tiles WHERE zoom_level=3"), 3);
        }
    }
}

// The rectangle comes from the iterator of the LOWEST kept zoom (:214-221, GetLonLatRect :280-290), not from
// the polygon and not from the last zoom. lon -80..40: z3 tiles x 2..4 end at lon 45 (z2 tiles x 1..2 end at
// 90).
TEST(ExportTaskToMbtiles, BoundsAreTheTileAlignedRectangleOfTheLowestZoomOnly) {
    struct Case {
        std::vector<quint8> zooms;
        QString bounds, minzoom, maxzoom;
    };
    const std::vector<Case> cases = {
        {{2, 3}, "-90.00000000,-66.51326044,90.00000000,66.51326044", "2", "3"},
        {{3, 4}, "-90.00000000,-66.51326044,45.00000000,66.51326044", "3", "4"},
    };
    for (const Case& c : cases) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Opt o;
        o.path = fileIn(d);
        o.polygon = rectPolygon(-80, 50, 40, -50);
        o.zooms = c.zooms;
        ASSERT_TRUE(makeTask(o)->processRegion().has_value());
        EXPECT_EQ(meta(o.path, "bounds"), c.bounds);
        EXPECT_EQ(meta(o.path, "minzoom"), c.minzoom);
        EXPECT_EQ(meta(o.path, "maxzoom"), c.maxzoom);
    }
}

// ---- 3. empty area ----------------------------------------------------------------------------------

TEST(ExportTaskToMbtiles, EmptyAreaGivesAFileWithMetadataAndNoTiles) {
    for (const bool tileMill : {false, true}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Opt o;
        o.path = fileIn(d);
        o.tileMill = tileMill;
        // nothing stored: every read answers "unknown" (legacy nil) or "not exists"
        o.store = std::make_shared<ScriptedStorage>();
        auto task = makeTask(o);
        ASSERT_TRUE(task->processRegion().has_value()) << tileMill;
        EXPECT_EQ(o.store->calls, 20) << tileMill;
        EXPECT_EQ(count(o.path, "SELECT count(*) FROM metadata"), tileMill ? 12 : 11) << tileMill;
        EXPECT_EQ(count(o.path, "SELECT count(*) FROM tiles"), 0) << tileMill;
        EXPECT_EQ(meta(o.path, "bounds"), kAlignedBounds) << tileMill; // written before any tile is read
        if (!tileMill) {
            EXPECT_EQ(classicIndexCount(o.path), 1);
        }
    }
    // the same with a storage that answers "not exists" for what it does not hold
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    Opt o;
    o.path = fileIn(d);
    o.store = std::make_shared<ScriptedStorage>();
    o.store->answerNotExistsForMissing();
    ASSERT_TRUE(makeTask(o)->processRegion().has_value());
    EXPECT_EQ(count(o.path, "SELECT count(*) FROM tiles"), 0);
}

// ---- 4. cancel --------------------------------------------------------------------------------------

TEST(ExportTaskToMbtiles, CancelBeforeTheFirstTileLeavesAClosedFileWithMetadataOnly) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    for (const bool tileMill : {false, true}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        std::stop_source source;
        source.request_stop();
        Opt o;
        o.path = fileIn(d);
        o.tileMill = tileMill;
        o.store = std::make_shared<ScriptedStorage>(storedTiles());
        o.progress = std::make_shared<RegionProcessProgressInfo>(source.get_token());
        auto task = makeTask(o);
        const auto r = task->processRegion();
        ASSERT_FALSE(r.has_value()) << tileMill;
        EXPECT_EQ(r.error().code(), ErrorCode::Cancelled) << tileMill;
        EXPECT_EQ(o.store->calls, 0) << tileMill; // the flag is read before the first read
        EXPECT_TRUE(QFileInfo(o.path).isFile()) << tileMill;
        EXPECT_TRUE(fileIsFree(o.path)) << tileMill;
        EXPECT_EQ(count(o.path, "SELECT count(*) FROM metadata"), tileMill ? 12 : 11) << tileMill;
        EXPECT_EQ(count(o.path, "SELECT count(*) FROM tiles"), 0) << tileMill;
        if (!tileMill) {
            EXPECT_EQ(classicIndexCount(o.path), 1);
        }
        EXPECT_EQ(o.progress->firstLine(), QStringLiteral("Total to save: 20 files")) << tileMill;
        EXPECT_FALSE(o.progress->finished()) << tileMill;
        task.reset();
        EXPECT_TRUE(o.progress->finished()) << tileMill;
    }
}

TEST(ExportTaskToMbtiles, CancelInTheMiddleKeepsTheTilesWrittenSoFarAndClosesTheFile) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    for (const bool tileMill : {false, true}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        std::stop_source source;
        Opt o;
        o.path = fileIn(d);
        o.zooms = {2};
        o.tileMill = tileMill;
        o.progress = std::make_shared<RegionProcessProgressInfo>(source.get_token());
        // all four z2 tiles of the rectangle exist
        o.store = std::make_shared<ScriptedStorage>(std::map<Key, Entry>{{{2, 1, 1}, exists(png())},
                                                                         {{2, 2, 1}, exists(jpg())},
                                                                         {{2, 1, 2}, exists(png2())},
                                                                         {{2, 2, 2}, exists(png())}});
        o.store->onCall = [&source](int call) {
            if (call == 3)
                source.request_stop();
        };
        auto task = makeTask(o);
        const auto r = task->processRegion();
        ASSERT_FALSE(r.has_value()) << tileMill;
        EXPECT_EQ(r.error().code(), ErrorCode::Cancelled) << tileMill;
        EXPECT_EQ(o.store->calls, 3) << tileMill; // the fourth read never happens
        // the tile being read when the flag rose is still written (:244-246 is checked before :249)
        TileMap expected;
        for (const Key& k : o.store->requested) {
            expected[k] = k == Key{2, 1, 1}   ? png()
                          : k == Key{2, 2, 1} ? jpg()
                          : k == Key{2, 1, 2} ? png2()
                                              : png();
        }
        ASSERT_EQ(expected.size(), 3U) << tileMill;
        EXPECT_EQ(readBack(o.path, {2}), expected) << tileMill; // committed by close
        EXPECT_TRUE(fileIsFree(o.path)) << tileMill;
        if (!tileMill) {
            EXPECT_EQ(classicIndexCount(o.path), 1);
        }
        EXPECT_FALSE(o.progress->finished()) << tileMill;
        task.reset();
        EXPECT_TRUE(o.progress->finished()) << tileMill;
    }
}

// ---- 5. more than 10 000 tiles ----------------------------------------------------------------------

TEST(ExportTaskToMbtiles, MoreThan10000TilesAreAllPresentAndCommittedAtTheBoundary) {
    for (const bool tileMill : {false, true}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Opt o;
        o.path = fileIn(d);
        o.polygon = rectPolygon(-179, 84, 179, -84); // z7: 128 columns x 122 rows = 15616 tiles
        o.zooms = {7};
        o.tileMill = tileMill;
        o.store = std::make_shared<ScriptedStorage>();
        o.store->constantPayload = QByteArrayLiteral("twenty-byte-payload!");
        qint64 sizeBeforeTile10000 = -1;
        qint64 sizeBeforeTile10001 = -1;
        const QString file = o.path;
        o.store->onCall = [&](int call) {
            // Pages of an open transaction stay in the page cache (journal_mode OFF); the file grows only
            // when COMMIT runs, so its size shows whether the commit at the 10000th tile happened.
            if (call == 10000)
                sizeBeforeTile10000 = QFileInfo(file).size();
            if (call == 10001)
                sizeBeforeTile10001 = QFileInfo(file).size();
        };
        auto task = makeTask(o);
        ASSERT_TRUE(task->processRegion().has_value()) << tileMill;
        EXPECT_EQ(o.store->calls, 15616) << tileMill;
        EXPECT_EQ(o.progress->firstLine(), QStringLiteral("Total to save: 15616 files")) << tileMill;
        EXPECT_EQ(count(o.path, "SELECT count(*) FROM tiles"), 15616) << tileMill;
        EXPECT_EQ(count(o.path, "SELECT count(*) FROM tiles WHERE zoom_level=7"), 15616) << tileMill;
        // the last multiple of 100 not above 15616
        EXPECT_EQ(o.progress->secondLine(), QStringLiteral("Processed 15600")) << tileMill;
        // 10000 rows of ~30 bytes are on disk after the commit and not before it
        EXPECT_GT(sizeBeforeTile10001 - sizeBeforeTile10000, 150000) << tileMill;
        EXPECT_GT(sizeBeforeTile10000, 0) << tileMill;
        const auto rows = query(o.path, "SELECT tile_data FROM tiles WHERE zoom_level=7 LIMIT 1", 1);
        ASSERT_EQ(rows.size(), 1U) << tileMill;
        EXPECT_EQ(rows[0][0], QStringLiteral("twenty-byte-payload!")) << tileMill;
    }
}

// ---- 6. progress ------------------------------------------------------------------------------------

TEST(ExportTaskToMbtiles, ProgressShowsTheLastMultipleOf100AndTheRatioIsNotForcedToOne) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    Opt o;
    o.path = fileIn(d);
    o.polygon = rectPolygon(-151, 55, -110, 37.5); // z7: 15 x 10 = 150 tiles
    o.zooms = {7};
    o.progress = std::make_shared<RegionProcessProgressInfo>(std::stop_token{});
    o.store = std::make_shared<ScriptedStorage>();
    o.store->constantPayload = QByteArrayLiteral("p");
    o.store->missingEvery = 7; // 21 of the 150 reads find nothing; they count as processed all the same
    QString secondAt11, secondAt100, secondAt101;
    double ratioAt101 = -1;
    o.store->onCall = [&](int call) {
        if (call == 11)
            secondAt11 = o.progress->secondLine();
        if (call == 100)
            secondAt100 = o.progress->secondLine();
        if (call == 101) {
            secondAt101 = o.progress->secondLine();
            ratioAt101 = o.progress->processedRatio();
        }
    };
    auto task = makeTask(o);
    ASSERT_TRUE(task->processRegion().has_value());
    EXPECT_EQ(o.store->calls, 150);
    EXPECT_EQ(o.progress->caption(), QStringLiteral("Export"));
    EXPECT_EQ(o.progress->firstLine(), QStringLiteral("Total to save: 150 files"));
    EXPECT_EQ(secondAt11, QStringLiteral("Processed 0"));    // the initial 0 / total update, nothing at 10
    EXPECT_EQ(secondAt100, QStringLiteral("Processed 0"));   // 99 tiles done: not yet
    EXPECT_EQ(secondAt101, QStringLiteral("Processed 100")); // 100 tiles done
    EXPECT_DOUBLE_EQ(ratioAt101, 100.0 / 150.0);
    // after the run: still the value of the last multiple of 100, NOT 1 and not "Processed 150"
    EXPECT_DOUBLE_EQ(o.progress->processedRatio(), 100.0 / 150.0);
    EXPECT_EQ(o.progress->secondLine(), QStringLiteral("Processed 100"));
    EXPECT_EQ(count(o.path, "SELECT count(*) FROM tiles"), 129); // 150 - floor(150 / 7)
}

// ---- 7. refusals before anything is created ---------------------------------------------------------

TEST(ExportTaskToMbtiles, NoDirectCopyOrNoStorageIsUnsupportedAndCreatesNothing) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    struct Case {
        const char* label;
        bool direct;
        bool nullStorage;
    };
    const std::vector<Case> cases = {{"no direct copy, storage present", false, false},
                                     {"direct copy, no storage", true, true},
                                     {"neither", false, true}};
    for (const Case& c : cases) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Opt o;
        o.path = fileIn(d); // <tmp>/out does not exist
        o.store = std::make_shared<ScriptedStorage>(storedTiles());
        o.direct = c.direct;
        auto task = makeTask(o, c.nullStorage);
        const auto r = task->processRegion();
        ASSERT_FALSE(r.has_value()) << c.label;
        EXPECT_EQ(r.error().code(), ErrorCode::Unsupported) << c.label;
        EXPECT_FALSE(QDir(d.path() + QStringLiteral("/out")).exists()) << c.label; // decided before mkpath
        EXPECT_FALSE(QFileInfo::exists(o.path)) << c.label;
        EXPECT_EQ(o.store->calls, 0) << c.label;
        EXPECT_TRUE(o.progress->caption().isEmpty()) << c.label;
    }
}

TEST(ExportTaskToMbtiles, AnEmptyZoomListIsAParseErrorBeforeAnyOtherCheckAndCreatesNothing) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    for (const bool direct : {true, false}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Opt o;
        o.path = fileIn(d);
        o.zooms = {30, 24}; // nothing valid is left
        o.direct = direct;
        o.store = std::make_shared<ScriptedStorage>(storedTiles());
        const auto r = makeTask(o)->processRegion();
        ASSERT_FALSE(r.has_value()) << direct;
        EXPECT_EQ(r.error().code(), ErrorCode::ParseError) << direct; // the base check is first (:186)
        EXPECT_EQ(r.error().what(), QStringLiteral("Please select at least one zoom")) << direct;
        EXPECT_FALSE(QDir(d.path() + QStringLiteral("/out")).exists()) << direct;
    }
}

TEST(ExportTaskToMbtiles, ADirectoryThatCannotBeCreatedIsAnIoErrorNamingThePath) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    QFile blocker(d.path() + QStringLiteral("/blocker"));
    ASSERT_TRUE(blocker.open(QIODevice::WriteOnly)); // a regular file where the directory should be
    blocker.close();
    Opt o;
    o.path = d.path() + QStringLiteral("/blocker/sub/t.mbtiles");
    o.store = std::make_shared<ScriptedStorage>(storedTiles());
    const auto r = makeTask(o)->processRegion();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::IoError);
    EXPECT_TRUE(r.error().what().contains(d.path() + QStringLiteral("/blocker/sub")))
        << r.error().what().toStdString();
    EXPECT_EQ(o.store->calls, 0);
}

TEST(ExportTaskToMbtiles, ABareFileNameFailsLikeLegacyForceDirectoriesOfAnEmptyPathAndCreatesNoFile) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    Opt o;
    o.path = QStringLiteral("bare.mbtiles");
    o.store = std::make_shared<ScriptedStorage>(storedTiles()); // fixtures are cwd-relative: read first
    const QString oldCwd = QDir::currentPath();
    ASSERT_TRUE(QDir::setCurrent(d.path()));
    const auto r = makeTask(o)->processRegion();
    const bool created = QFileInfo::exists(QStringLiteral("bare.mbtiles"));
    QDir::setCurrent(oldCwd);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::IoError);
    EXPECT_EQ(r.error().what(), QStringLiteral("Unable to create directory"));
    EXPECT_FALSE(created);
    EXPECT_EQ(o.store->calls, 0);
}

TEST(ExportTaskToMbtiles, AWriterOpenErrorIsReturnedAtOnceAndTheExistingFileIsUntouched) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    for (const bool tileMill : {false, true}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Opt first;
        first.path = fileIn(d);
        first.zooms = {2};
        first.tileMill = tileMill;
        first.store = std::make_shared<ScriptedStorage>(storedTiles());
        ASSERT_TRUE(makeTask(first)->processRegion().has_value()) << tileMill;
        const int tilesBefore = count(first.path, "SELECT count(*) FROM tiles");
        ASSERT_EQ(tilesBefore, 2) << tileMill;

        Opt second;
        second.path = first.path;
        second.zooms = {2};
        second.tileMill = tileMill;
        second.format = QStringLiteral("jpeg"); // the file says "png"
        second.force = false;
        second.store = std::make_shared<ScriptedStorage>(storedTiles());
        {
            auto task = makeTask(second);
            const auto r = task->processRegion();
            ASSERT_FALSE(r.has_value()) << tileMill;
            EXPECT_EQ(r.error().code(), ErrorCode::Unsupported) << tileMill;
            EXPECT_EQ(r.error().what(),
                      QStringLiteral("Invalid \"format\" value! Expected \"jpeg\" but found \"png\""))
                << tileMill;
            EXPECT_EQ(second.store->calls, 0) << tileMill;                 // no tile is read
            EXPECT_TRUE(second.progress->caption().isEmpty()) << tileMill; // texts are set after Open (:231)
            EXPECT_TRUE(second.progress->firstLine().isEmpty()) << tileMill;
        } // legacy releases the handle in the destructor (:163-167)
        EXPECT_TRUE(second.progress->finished()) << tileMill;
        EXPECT_TRUE(fileIsFree(first.path)) << tileMill;
        EXPECT_EQ(meta(first.path, "format"), "png") << tileMill;
        EXPECT_EQ(count(first.path, "SELECT count(*) FROM tiles"), tilesBefore) << tileMill;
    }
}

// ---- 8. storage error (finding b: the Result error is the port's legacy exception) ------------------

TEST(ExportTaskToMbtiles, AStorageErrorEndsTheLoopCloseRunsAndTheErrorIsReturned) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    for (const bool tileMill : {false, true}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Opt o;
        o.path = fileIn(d);
        o.zooms = {2};
        o.tileMill = tileMill;
        o.store = std::make_shared<ScriptedStorage>(std::map<Key, Entry>{{{2, 1, 1}, exists(png())},
                                                                         {{2, 2, 1}, exists(jpg())},
                                                                         {{2, 1, 2}, exists(png2())},
                                                                         {{2, 2, 2}, exists(png())}});
        o.store->errorAt = 3;
        auto task = makeTask(o);
        const auto r = task->processRegion();
        ASSERT_FALSE(r.has_value()) << tileMill;
        EXPECT_EQ(r.error().code(), ErrorCode::IoError) << tileMill;
        EXPECT_EQ(r.error().what(), QStringLiteral("scripted storage failure")) << tileMill;
        EXPECT_EQ(o.store->calls, 3) << tileMill; // the loop does not go on to the fourth tile
        // the two tiles read before the failure are in the file (committed by close)
        EXPECT_EQ(count(o.path, "SELECT count(*) FROM tiles"), 2) << tileMill;
        EXPECT_TRUE(fileIsFree(o.path)) << tileMill;
        if (!tileMill) {
            EXPECT_EQ(classicIndexCount(o.path), 1);
        }
        task.reset();
        EXPECT_TRUE(o.progress->finished()) << tileMill;
    }
}
