// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-019, FEAT-STO-020, FEAT-STO-021. Ports
// u_TileStorageSQLiteFileConnectionBuilder.pas:179-225 (PrepareFileName) and
// u_TileStorageSQLiteFileConnectionPool.pas:51-104; see docs/specs/storage.md §2.17. Per the
// ticket's own Tests section, the connection base's full statement plumbing (real flavour SQL:
// bind/result logic, schema, metadata) is exercised through T-0022C2/C3's tests, once a real
// flavour exists to drive it -- not here. The three FakeConnection-based tests below (added in
// the T-0022C1 rework, review findings F2-F4) are the minimum needed to pin insertOrUpdate's
// rollback-on-metadata-failure, a disabled connection's no-op contract, and fetchRectInfo's
// partial-result-on-step-error behaviour without a real format's schema.
#include "cg/core/content_type.h"
#include "cg/core/result.h"
#include "cg/geo/geo_types.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/sqlite/sqlite_file_connection.h"
#include "cg/storage/sqlite/sqlite_file_connection_pool.h"
#include "cg/storage/tile_info.h"
#include "cg/storage/tile_rect_info.h"

#include <QByteArray>
#include <QDir>
#include <QString>
#include <QTemporaryDir>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>
#include <vector>

using cg::storage::LoadMode;
using cg::storage::TileInfo;
using cg::storage::TileInfoType;
using cg::storage::sqlite::prepareFileName;
using cg::storage::sqlite::SqliteFileConnection;
using cg::storage::sqlite::SqliteFileConnectionPool;
using cg::storage::sqlite::SqliteFileFormat;

namespace {

// A minimal concrete SqliteFileConnection: enough to satisfy the pure virtual flavour hooks so
// the pool can be tested without a real MBTiles/RMaps/OruxMaps flavour (T-0022C2/C3/D1). Never
// initialize()'d -- the pool only needs a distinct, identifiable object per acquire. Also used by
// F3's disabled-connection test below: an un-initialize()'d connection has isEnabled() == false
// (default; T-0022C1-review.md F3) and a null db_, so it doubles as the "operation on a
// not-enabled connection must not touch db_" fixture.
class StubConnection final : public SqliteFileConnection {
public:
    explicit StubConnection(int id)
        : SqliteFileConnection(/*isReadOnly=*/true, QStringLiteral("stub"), cg::core::ContentTypeInfo{},
                               cg::core::ContentTypeRegistry::standard(), nullptr),
          id_(id) {}

    [[nodiscard]] int id() const noexcept { return id_; }

protected:
    [[nodiscard]] cg::core::Result<void> createTables() override { return {}; }
    [[nodiscard]] cg::core::Result<void> fetchMetadata() override { return {}; }
    [[nodiscard]] cg::core::Result<void> updateMetadata(cg::geo::PointI, quint8) override { return {}; }

private:
    int id_;
};

// --- FakeConnection: a tiny real flavour, just enough SQL to drive F2/F4 -----------------------
//
// Schema: one table, `tiles(x, y, z, data)`, no uniqueness constraint (the exists-check is done in
// C++ via FakeTileInfoStmt, same as legacy's IsTileExists). Bind order for every statement is
// (x, y, z[, data]), matching TTileDataConnectionStatement's own (X, Y, Z[, ABlob]) signature.

class FakeTileDataStmt final : public cg::storage::sqlite::TileDataStatement {
public:
    using TileDataStatement::TileDataStatement;
    [[nodiscard]] cg::core::Result<void> bindParams(cg::geo::PointI xy, quint8 zoom) override {
        if (auto r = stmt().bindInt(1, xy.x()); !r)
            return r;
        if (auto r = stmt().bindInt(2, xy.y()); !r)
            return r;
        if (auto r = stmt().bindInt(3, zoom); !r)
            return r;
        return {};
    }
    [[nodiscard]] QByteArray result() override { return blobColumn(0); }

protected:
    [[nodiscard]] QByteArray sqlText() const override {
        return QByteArrayLiteral("SELECT data FROM tiles WHERE x=?1 AND y=?2 AND z=?3");
    }
};

class FakeTileInfoStmt final : public cg::storage::sqlite::TileInfoStatement {
public:
    using TileInfoStatement::TileInfoStatement;
    [[nodiscard]] cg::core::Result<void> bindParams(cg::geo::PointI xy, quint8 zoom) override {
        if (auto r = stmt().bindInt(1, xy.x()); !r)
            return r;
        if (auto r = stmt().bindInt(2, xy.y()); !r)
            return r;
        if (auto r = stmt().bindInt(3, zoom); !r)
            return r;
        return {};
    }
    [[nodiscard]] quint32 result() override { return static_cast<quint32>(stmt().columnInt64(0)); }

protected:
    [[nodiscard]] QByteArray sqlText() const override {
        return QByteArrayLiteral("SELECT length(data) FROM tiles WHERE x=?1 AND y=?2 AND z=?3");
    }
};

class FakeInsertStmt final : public cg::storage::sqlite::InsertTileStatement {
public:
    using InsertTileStatement::InsertTileStatement;
    [[nodiscard]] cg::core::Result<void> bindParams(cg::geo::PointI xy, quint8 zoom,
                                                    const QByteArray& data) override {
        if (auto r = stmt().bindInt(1, xy.x()); !r)
            return r;
        if (auto r = stmt().bindInt(2, xy.y()); !r)
            return r;
        if (auto r = stmt().bindInt(3, zoom); !r)
            return r;
        if (auto r = stmt().bindBlob(4, data); !r)
            return r;
        return {};
    }

protected:
    [[nodiscard]] QByteArray sqlText() const override {
        return QByteArrayLiteral("INSERT INTO tiles(x,y,z,data) VALUES(?1,?2,?3,?4)");
    }
};

class FakeUpdateStmt final : public cg::storage::sqlite::UpdateTileStatement {
public:
    using UpdateTileStatement::UpdateTileStatement;
    [[nodiscard]] cg::core::Result<void> bindParams(cg::geo::PointI xy, quint8 zoom,
                                                    const QByteArray& data) override {
        if (auto r = stmt().bindInt(1, xy.x()); !r)
            return r;
        if (auto r = stmt().bindInt(2, xy.y()); !r)
            return r;
        if (auto r = stmt().bindInt(3, zoom); !r)
            return r;
        if (auto r = stmt().bindBlob(4, data); !r)
            return r;
        return {};
    }

protected:
    [[nodiscard]] QByteArray sqlText() const override {
        return QByteArrayLiteral("UPDATE tiles SET data=?4 WHERE x=?1 AND y=?2 AND z=?3");
    }
};

// F4's fixture: forces a genuine sqlite runtime error (SQLITE_TOOBIG, well past the default
// 1e9-byte length limit) while stepping the row at (1, 0), so a rect scan that visits (0, 0) first
// collects one real row before the step failure -- proving fetchRectInfo returns what it has so
// far (legacy-defects L-41) rather than nullptr. Deliberately NO "ORDER BY": with one it, sqlite
// would materialise a sorter and could evaluate every row's CASE expression up front (on the
// first step() call) instead of incrementally, one row per step() -- the plain unindexed scan
// below returns rows in rowid (insertion) order instead, which the test relies on.
class PoisonRectInfoStmt final : public cg::storage::sqlite::RectInfoStatement {
public:
    using RectInfoStatement::RectInfoStatement;
    [[nodiscard]] cg::core::Result<void> bindParams(cg::geo::RectI rect, quint8 zoom) override {
        if (auto r = stmt().bindInt(1, zoom); !r)
            return r;
        if (auto r = stmt().bindInt(2, rect.left); !r)
            return r;
        if (auto r = stmt().bindInt(3, rect.right); !r)
            return r;
        if (auto r = stmt().bindInt(4, rect.top); !r)
            return r;
        if (auto r = stmt().bindInt(5, rect.bottom); !r)
            return r;
        return {};
    }
    void result(int& x, int& y, quint32& size) override {
        x = stmt().columnInt(0);
        y = stmt().columnInt(1);
        size = static_cast<quint32>(stmt().columnInt64(2));
    }

protected:
    [[nodiscard]] QByteArray sqlText() const override {
        return QByteArrayLiteral(
            "SELECT x, y, CASE WHEN x=1 AND y=0 THEN zeroblob(9223372036854775807) "
            "ELSE length(data) END FROM tiles WHERE z=?1 AND x>=?2 AND x<?3 AND y>=?4 AND y<?5");
    }
};

class FakeConnection final : public SqliteFileConnection {
public:
    explicit FakeConnection(QString fileName)
        : SqliteFileConnection(/*isReadOnly=*/false, std::move(fileName), cg::core::ContentTypeInfo{},
                               cg::core::ContentTypeRegistry::standard(), nullptr) {
        tileDataStmt_ = std::make_unique<FakeTileDataStmt>(false, false);
        tileInfoStmt_ = std::make_unique<FakeTileInfoStmt>(false, false);
        insertStmt_ = std::make_unique<FakeInsertStmt>(false, false);
        updateStmt_ = std::make_unique<FakeUpdateStmt>(false, false);
        rectInfoStmt_ = std::make_unique<PoisonRectInfoStmt>(false, false);
    }

    bool failMetadata = false; // F2: toggled mid-test to force updateMetadata() to fail

protected:
    [[nodiscard]] cg::core::Result<void> createTables() override {
        if (auto r =
                db().exec(QByteArrayView("CREATE TABLE tiles (x INTEGER, y INTEGER, z INTEGER, data BLOB)"));
            !r) {
            return cg::core::Unexpected(r.error());
        }
        return {};
    }
    [[nodiscard]] cg::core::Result<void> fetchMetadata() override { return {}; }
    [[nodiscard]] cg::core::Result<void> updateMetadata(cg::geo::PointI, quint8) override {
        if (failMetadata) {
            return cg::core::fail(cg::core::ErrorCode::Unsupported, QStringLiteral("boom"));
        }
        return {};
    }
};

QString tempDbPath(const QTemporaryDir& dir) {
    return QDir(dir.path()).filePath(QStringLiteral("fake.sqlite"));
}

} // namespace

// --- prepareFileName -------------------------------------------------------------

TEST(PrepareFileName, DirectoryWithTrailingSeparator) {
    // ".../cache_mbtiles/osm/" -> ".../cache_mbtiles/osm/osm.mbtiles" (storage.md §2.17's own
    // worked example).
    EXPECT_EQ(prepareFileName(QStringLiteral("/cache/cache_mbtiles/osm/"), SqliteFileFormat::MbTiles),
              QStringLiteral("/cache/cache_mbtiles/osm/osm.mbtiles"));
}

TEST(PrepareFileName, DirectoryWithoutTrailingSeparator) {
    struct Row {
        SqliteFileFormat format;
        QString expectedExt;
    };
    const std::vector<Row> rows = {
        {SqliteFileFormat::MbTiles, QStringLiteral(".mbtiles")},
        {SqliteFileFormat::OsmAnd, QStringLiteral(".sqlitedb")},
        {SqliteFileFormat::Locus, QStringLiteral(".sqlitedb")},
        {SqliteFileFormat::RMaps, QStringLiteral(".sqlitedb")},
        {SqliteFileFormat::OruxMaps, QStringLiteral(".db")},
    };
    for (const Row& row : rows) {
        const QString result = prepareFileName(QStringLiteral("/cache/cache_mbtiles/osm"), row.format);
        EXPECT_EQ(result, QStringLiteral("/cache/cache_mbtiles/osm/osm") + row.expectedExt)
            << "format=" << static_cast<int>(row.format);
    }
}

TEST(PrepareFileName, PathThatAlreadyHasAnExtensionIsUnchanged) {
    EXPECT_EQ(prepareFileName(QStringLiteral("/cache/osm.mbtiles"), SqliteFileFormat::MbTiles),
              QStringLiteral("/cache/osm.mbtiles"));
    // A trailing separator is still stripped even when an extension is already present -- the two
    // rules (strip trailing '/', then check for an extension) apply in sequence, not exclusively.
    EXPECT_EQ(prepareFileName(QStringLiteral("/cache/osm.mbtiles/"), SqliteFileFormat::MbTiles),
              QStringLiteral("/cache/osm.mbtiles"));
    // F6 / D-32 / legacy-defects L-42: legacy's PrepareFileName('') is an access violation
    // (`Result[High(Result)]` on an empty string, u_TileStorageSQLiteFileConnectionBuilder.pas
    // :210) -- a sanctioned critical-defect fix (Rule 2). The port returns the input unchanged.
    EXPECT_EQ(prepareFileName(QString(), SqliteFileFormat::MbTiles), QString());
}

// --- SqliteFileConnectionPool -----------------------------------------------------

TEST(SqliteFileConnectionPool, ReleasedConnectionIsHandedBackBeforeMakingANewOne) {
    int nextId = 0;
    SqliteFileConnectionPool pool([&nextId] { return std::make_unique<StubConnection>(++nextId); });

    auto first = pool.acquire();
    ASSERT_TRUE(first.has_value());
    ASSERT_NE(*first, nullptr);
    EXPECT_EQ(static_cast<StubConnection&>(**first).id(), 1);
    EXPECT_EQ(nextId, 1);
    EXPECT_EQ(pool.freeCountForTest(), 0U);

    pool.release(std::move(*first));
    EXPECT_EQ(pool.freeCountForTest(), 1U);

    // Free list is non-empty: acquire() must hand back the released connection, not call the
    // maker again (u_TileStorageSQLiteFileConnectionPool.pas:75-91).
    auto second = pool.acquire();
    ASSERT_TRUE(second.has_value());
    ASSERT_NE(*second, nullptr);
    EXPECT_EQ(static_cast<StubConnection&>(**second).id(), 1);
    EXPECT_EQ(nextId, 1);
    EXPECT_EQ(pool.freeCountForTest(), 0U);

    // Free list empty again: only now does the maker build a new (second) connection.
    auto third = pool.acquire();
    ASSERT_TRUE(third.has_value());
    ASSERT_NE(*third, nullptr);
    EXPECT_EQ(static_cast<StubConnection&>(**third).id(), 2);
    EXPECT_EQ(nextId, 2);

    pool.release(std::move(*second));
    pool.release(std::move(*third));
}

TEST(SqliteFileConnectionPool, FreeListIsLifo) {
    int nextId = 0;
    SqliteFileConnectionPool pool([&nextId] { return std::make_unique<StubConnection>(++nextId); });

    auto a = pool.acquire(); // id 1
    auto b = pool.acquire(); // id 2
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    pool.release(std::move(*a));
    pool.release(std::move(*b));
    EXPECT_EQ(pool.freeCountForTest(), 2U);

    // Ports FList[Count-1] (Acquire) / FList.Add (Release): last released is first acquired.
    auto first = pool.acquire();
    ASSERT_TRUE(first.has_value());
    ASSERT_NE(*first, nullptr);
    EXPECT_EQ(static_cast<StubConnection&>(**first).id(), 2);

    auto second = pool.acquire();
    ASSERT_TRUE(second.has_value());
    ASSERT_NE(*second, nullptr);
    EXPECT_EQ(static_cast<StubConnection&>(**second).id(), 1);

    pool.release(std::move(*first));
    pool.release(std::move(*second));
}

// --- SqliteFileConnection: F2/F3/F4 (T-0022C1 rework) ------------------------------------------

TEST(SqliteFileConnection, OperationsOnDisabledConnectionReturnLegacyValues) {
    // F3: isEnabled() defaults to false and this connection is never initialize()'d, so db_ stays
    // null. Every operation must check isEnabled() before touching db_ -- legacy's own FEnabled
    // check on each of these methods (:296-298, :349-351, :408-410, :537-539, :574-576) never
    // dereferences FSQLite3DB either. None of these five calls may crash; each must come back as a
    // successful Result carrying legacy's "disabled" value (nil/False), not an error.
    StubConnection conn(1);
    ASSERT_FALSE(conn.isEnabled());

    auto one = conn.fetchOne(cg::geo::PointI(0, 0), 1, LoadMode::WithData);
    ASSERT_TRUE(one.has_value());
    EXPECT_EQ(one->type, TileInfoType::NotExists);

    std::stop_source stopSource;
    auto rect = conn.fetchRectInfo(cg::geo::RectI{0, 0, 2, 2}, 1, stopSource.get_token());
    ASSERT_TRUE(rect.has_value());
    EXPECT_EQ(*rect, nullptr);

    TileInfo out;
    cg::geo::PointI xy;
    quint8 zoom = 0;
    auto next = conn.fetchNext(out, xy, zoom);
    ASSERT_TRUE(next.has_value());
    EXPECT_FALSE(*next);

    auto inserted = conn.insertOrUpdate(cg::geo::PointI(0, 0), 1, QByteArrayLiteral("x"), true);
    ASSERT_TRUE(inserted.has_value());
    EXPECT_FALSE(*inserted);

    auto deleted = conn.deleteTile(cg::geo::PointI(0, 0), 1);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_FALSE(*deleted);
}

TEST(SqliteFileConnection, InsertOrUpdateRollsBackWhenUpdateMetadataFails) {
    // F2: legacy calls UpdateMetadata inside Insert's own try (:555); both MBTiles ("Invalid zoom
    // value") and RMaps (RaiseSQLite3Error) can raise there, which is caught by the same
    // except -- ROLLBACK, Result := False. The tile that InsertTile just wrote must NOT survive.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    FakeConnection conn(tempDbPath(dir));
    ASSERT_TRUE(conn.initialize(nullptr).has_value());
    ASSERT_TRUE(conn.isEnabled());

    const cg::geo::PointI xy(5, 5);
    const quint8 zoom = 3;
    const QByteArray data = QByteArrayLiteral("hello");

    conn.failMetadata = true;
    auto failed = conn.insertOrUpdate(xy, zoom, data, /*overwrite=*/false);
    ASSERT_TRUE(failed.has_value()) << "updateMetadata failing is caught locally, not propagated";
    EXPECT_FALSE(*failed);

    // The insert must have been rolled back along with the metadata failure.
    auto lookup = conn.fetchOne(xy, zoom, LoadMode::WithData);
    ASSERT_TRUE(lookup.has_value());
    EXPECT_EQ(lookup->type, TileInfoType::NotExists);

    // Sanity check: the same insert succeeds, and IS visible, once metadata stops failing.
    conn.failMetadata = false;
    auto ok = conn.insertOrUpdate(xy, zoom, data, /*overwrite=*/false);
    ASSERT_TRUE(ok.has_value());
    EXPECT_TRUE(*ok);
    auto lookup2 = conn.fetchOne(xy, zoom, LoadMode::WithData);
    ASSERT_TRUE(lookup2.has_value());
    EXPECT_EQ(lookup2->type, TileInfoType::Exists);
    EXPECT_EQ(lookup2->data, data);
}

TEST(SqliteFileConnection, FetchRectInfoReturnsPartialResultOnStepError) {
    // F4: legacy's `while StepPrepared = SQLITE_ROW` (:368) ends the scan on a step error exactly
    // like it ends on DONE, so a failure partway through returns the rows collected so far, not
    // nullptr (legacy-defects L-41).
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    FakeConnection conn(tempDbPath(dir));
    ASSERT_TRUE(conn.initialize(nullptr).has_value());
    ASSERT_TRUE(conn.isEnabled());

    const quint8 zoom = 1;
    auto insA = conn.insertOrUpdate(cg::geo::PointI(0, 0), zoom, QByteArrayLiteral("a"), false);
    auto insB = conn.insertOrUpdate(cg::geo::PointI(1, 0), zoom, QByteArrayLiteral("bb"), false);
    auto insC = conn.insertOrUpdate(cg::geo::PointI(0, 1), zoom, QByteArrayLiteral("ccc"), false);
    ASSERT_TRUE(insA.has_value() && insA.value());
    ASSERT_TRUE(insB.has_value() && insB.value());
    ASSERT_TRUE(insC.has_value() && insC.value());

    // PoisonRectInfoStmt's unindexed scan visits rows in rowid (insertion) order: (0,0), (1,0),
    // (0,1); (1,0) always fails its step with SQLITE_TOOBIG, so the scan ends there.
    std::stop_source stopSource;
    auto rect = conn.fetchRectInfo(cg::geo::RectI{0, 0, 2, 2}, zoom, stopSource.get_token());
    ASSERT_TRUE(rect.has_value());
    ASSERT_NE(*rect, nullptr);

    const auto* first = (*rect)->at(cg::geo::PointI(0, 0));
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->type, TileInfoType::Exists);
    EXPECT_EQ(first->size, 1U);

    // Neither the failing row nor the row after it (never reached once the loop broke) was
    // populated -- proving the result is genuinely partial, not a lucky "happens to have
    // everything" coincidence.
    const auto* second = (*rect)->at(cg::geo::PointI(1, 0));
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->type, TileInfoType::Unknown);

    const auto* third = (*rect)->at(cg::geo::PointI(0, 1));
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(third->type, TileInfoType::Unknown);
}
