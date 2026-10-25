// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLiteFile/u_TileStorageSQLiteFileConnection.pas:179-645,
//        Src/TileStorage/SQLiteFile/u_TileStorageSQLiteFileConnectionBuilder.pas:179-240
//        (PrepareFileName + the per-format default extensions only -- the format dispatch that
//        fills the rest of that unit's line range is T-0022C4's scope, not this ticket's),
//        Src/TileStorage/SQLiteFile/t_TileStorageSQLiteFile.pas,
//        Src/TileStorage/SQLiteFile/{i_,u_}TileStorageSQLiteFileInfo.pas.
//
// docs/specs/storage.md §2.17 (:1324-1445, the intro + "Filename resolution"), §3.3's D-10 last
// sentence ("SqliteDb stays NOMUTEX for every other user (MBTiles/Orux/SQLiteFile), which matches
// their legacy flags").
//
// The format-independent half of a single-file SQLite cache: one connection to one .mbtiles /
// .sqlitedb / .db file. The seven abstract statement classes below are the "flavour hooks";
// T-0022C2 (MBTiles), T-0022C3 (RMaps/OsmAnd/Locus) and T-0022D1 (OruxMaps, read-only) each supply
// concrete SQL text, bind/result logic, schema creation and metadata handling by subclassing them
// and SqliteFileConnection itself. None of those three formats, nor the storage face or the
// builder's format dispatch, are in this ticket's scope (T-0022C1).
//
// Construction is two-phase: a flavour's own constructor builds the concrete object (assigning
// whichever of the seven statement slots it needs), then the flavour's factory function calls
// initialize(). This is NOT how legacy's constructor works (u_TileStorageSQLiteFileConnection.pas
// :236-240 calls Self.CreateTables / Self.FetchMetadata from inside TTileStorageSQLiteFileConnec-
// tion.Create) -- but it is the only correct C++ translation: those are pure virtuals, and a
// not-yet-finished derived object's override cannot be dispatched to from a base-class
// constructor the way Object Pascal's fully-allocated-before-Create instance allows. The sequence
// of operations initialize() performs is otherwise identical to legacy's constructor body.
//
// No connection here is ever shared across threads: it belongs to SqliteFileConnectionPool, which
// hands each acquired connection to exactly one caller (NOMUTEX; contrast with the region cache's
// single-shared-connection-plus-per-file-mutex model, T-0022B, D-10) -- so, unlike that class,
// nothing in SqliteFileConnection itself takes a lock.

#include "cg/core/content_type.h"
#include "cg/core/result.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/projection_set.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/tile_date.h"
#include "cg/storage/tile_info.h"
#include "cg/storage/tile_rect_info.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>

namespace cg::storage::sqlite {

// FEAT-STO-019. Ports TTileStorageSQLiteFileFormatId (t_TileStorageSQLiteFile.pas:27-33), same
// declaration order (also the order PrepareFileName's default-extension table uses).
enum class SqliteFileFormat : std::uint8_t { MbTiles, OsmAnd, Locus, RMaps, OruxMaps };

// FEAT-STO-019. Ports TTileStorageSQLiteFileConnectionBuilder.PrepareFileName
// (u_TileStorageSQLiteFileConnectionBuilder.pas:179-225) and its per-format default extension
// table (:184-195). `path` may name a file or a directory (a trailing '/' is the only hint the
// legacy function looks at): strip one trailing '/', and if what remains has no extension (no '.'
// after the last '/'), treat it as a directory and append "/<last path component><default ext>"
// -- ".../cache_mbtiles/osm/" -> ".../cache_mbtiles/osm/osm.mbtiles". A path that already ends in
// an extension is returned with only the trailing '/' stripped, unchanged otherwise. Default
// extensions by format: MbTiles ".mbtiles", OsmAnd/Locus/RMaps ".sqlitedb", OruxMaps ".db".
[[nodiscard]] QString prepareFileName(const QString& path, SqliteFileFormat format);

// Ports ITileStorageSQLiteFileInfo / TTileStorageSQLiteFileInfo. Shared by every connection of the
// same open file: the file name, its (creation-time-as-mtime, see initialize()) date, and a
// metadata cache flavours fill for their own key/value schema (MBTiles' `metadata` table; the
// RMaps family's parsed `info.sasgis` JSON) so it is read from disk once by the first connection
// and reused by every later one T-0022C4's builder/pool hands the same SqliteFileInfoPtr to.
class SqliteFileInfo final {
public:
    SqliteFileInfo(QString fileName, TileDate fileDate);

    [[nodiscard]] const QString& fileName() const noexcept { return fileName_; }
    [[nodiscard]] const TileDate& fileDate() const noexcept { return fileDate_; }

    // Ports AddOrSetMetadataValue / TryGetMetadataValue (TDictionary<string,string>: case-
    // sensitive key match, last write wins).
    void addOrSetMetadataValue(const QString& key, const QString& value);
    [[nodiscard]] std::optional<QString> metadataValue(const QString& key) const;

private:
    QString fileName_;
    TileDate fileDate_;
    QHash<QString, QString> metadata_;
};
using SqliteFileInfoPtr = std::shared_ptr<SqliteFileInfo>;

// Ports TConnectionStatement (u_TileStorageSQLiteFileConnection.pas:40-55, 595-643). A lazily
// prepared statement plus the Y/Z tile-coordinate inversion flags a flavour's bind/result logic
// needs (e.g. MBTiles' Y-flip depends on the `scheme` metadata key; RMaps/Locus always invert Z,
// OsmAnd only when `info.tilenumbering` = "BigPlanet" -- storage.md §2.17.2). This class is never
// instantiated directly; a flavour subclasses one of the seven leaf types declared below it.
// Abstract base *class* with real logic, not a GUID-style pure interface -- the coding standard
// reserves the I-prefix for the latter; .clang-tidy's AbstractClassPrefix heuristic does not
// distinguish the two (see config_element.h for the established precedent for this NOLINT).
// NOLINTNEXTLINE(readability-identifier-naming)
class ConnectionStatement {
public:
    ConnectionStatement(bool invertedY, bool invertedZ) : invertedY_(invertedY), invertedZ_(invertedZ) {}
    virtual ~ConnectionStatement() = default;

    ConnectionStatement(const ConnectionStatement&) = delete;
    ConnectionStatement& operator=(const ConnectionStatement&) = delete;

    // Ports CheckPrepared (:631-643): prepares sqlText() on the first call and remembers the
    // outcome; later calls neither re-prepare nor re-run sqlText(), they just repeat the first
    // outcome. Legacy's FetchOne asserts its two statements (TileData/TileInfo) already prepared
    // instead of calling CheckPrepared, implying legacy prepares those eagerly (in the flavour's
    // constructor, see initialize()) and the other five lazily (on first use); nothing observable
    // depends on which schedule a given statement uses, so this port always prepares lazily,
    // uniformly, for all seven kinds -- initialize() just happens to force TileData/TileInfo's
    // first use immediately (F3 below).
    //
    // The Result<bool> shape mirrors CheckPrepared's own two-faced contract exactly (F1/F3;
    // legacy-defects.md L-40): a FRESH failure (this statement's very first prepare attempt)
    // raises in legacy (`ASQLite3DB.RaiseSQLite3Error`, :639, uncaught by anything in
    // u_TileStorageSQLiteFileConnection.pas) and is reported here as an error Result -- the
    // caller must propagate it, not silently disable the connection, because legacy's raise skips
    // the `FEnabled := False` line entirely. A CACHED failure (this statement already failed on
    // an earlier call) does NOT raise in legacy -- CheckPrepared just returns False again -- so
    // it comes back here as a successful `Result<bool>(false)`; only THIS outcome is what lets a
    // caller legitimately disable the connection, one call later than the failure itself.
    // Prepared: `Result<bool>(true)`.
    [[nodiscard]] core::Result<bool> ensurePrepared(sqlite::SqliteDb& db);

    [[nodiscard]] bool isPrepared() const noexcept { return state_ == State::Prepared; }
    [[nodiscard]] sqlite::SqliteStmt& stmt() { return *stmt_; }

    [[nodiscard]] bool invertedY() const noexcept { return invertedY_; }
    [[nodiscard]] bool invertedZ() const noexcept { return invertedZ_; }

protected:
    // Ports FText -- supplied by the flavour's override instead of being assigned once after
    // `inherited Create` (matches this class always preparing lazily; see ensurePrepared()).
    [[nodiscard]] virtual QByteArray sqlText() const = 0;

    // Ports BlobToBinaryData (:615-629). Returns an empty QByteArray, not an assertion failure,
    // for a NULL/zero-length blob column: this project's Assert-equivalents -- like Delphi's --
    // compile out in a release build (legacy-defects.md L-10 makes the same observation for a
    // different unit), so a flavour's GetResult() cannot rely on an assert firing here either.
    [[nodiscard]] QByteArray blobColumn(int index) const;

private:
    enum class State : std::uint8_t { None, Prepared, Error };

    State state_ = State::None;
    std::unique_ptr<sqlite::SqliteStmt> stmt_;
    bool invertedY_ = false;
    bool invertedZ_ = false;
};

// Ports TTileDataConnectionStatement (:57-61).
// NOLINTNEXTLINE(readability-identifier-naming)
class TileDataStatement : public ConnectionStatement {
public:
    using ConnectionStatement::ConnectionStatement;
    [[nodiscard]] virtual core::Result<void> bindParams(geo::PointI xy, quint8 zoom) = 0;
    [[nodiscard]] virtual QByteArray result() = 0; // the tile blob
};

// Ports TTileInfoConnectionStatement (:63-67).
// NOLINTNEXTLINE(readability-identifier-naming)
class TileInfoStatement : public ConnectionStatement {
public:
    using ConnectionStatement::ConnectionStatement;
    [[nodiscard]] virtual core::Result<void> bindParams(geo::PointI xy, quint8 zoom) = 0;
    [[nodiscard]] virtual quint32 result() = 0; // the tile's blob size, e.g. `length(tile_data)`
};

// Ports TRectInfoConnectionStatement (:69-75).
// NOLINTNEXTLINE(readability-identifier-naming)
class RectInfoStatement : public ConnectionStatement {
public:
    using ConnectionStatement::ConnectionStatement;
    [[nodiscard]] virtual core::Result<void> bindParams(geo::RectI rect, quint8 zoom) = 0;
    virtual void result(int& x, int& y, quint32& size) = 0;
};

// Ports TEnumTilesConnectionStatement (:77-80).
// NOLINTNEXTLINE(readability-identifier-naming)
class EnumTilesStatement : public ConnectionStatement {
public:
    using ConnectionStatement::ConnectionStatement;
    virtual void result(int& x, int& y, quint8& z, QByteArray& blob) = 0;
};

// Ports TInsertTileConnectionStatement (:82-85) / TUpdateTileConnectionStatement (:87-90): same
// shape, two call sites in insertOrUpdate() below (a fresh row vs. an existing one).
// NOLINTNEXTLINE(readability-identifier-naming)
class InsertTileStatement : public ConnectionStatement {
public:
    using ConnectionStatement::ConnectionStatement;
    [[nodiscard]] virtual core::Result<void> bindParams(geo::PointI xy, quint8 zoom,
                                                        const QByteArray& data) = 0;
};
// NOLINTNEXTLINE(readability-identifier-naming)
class UpdateTileStatement : public ConnectionStatement {
public:
    using ConnectionStatement::ConnectionStatement;
    [[nodiscard]] virtual core::Result<void> bindParams(geo::PointI xy, quint8 zoom,
                                                        const QByteArray& data) = 0;
};

// Ports TDeleteTileConnectionStatement (:92-95).
// NOLINTNEXTLINE(readability-identifier-naming)
class DeleteTileStatement : public ConnectionStatement {
public:
    using ConnectionStatement::ConnectionStatement;
    [[nodiscard]] virtual core::Result<void> bindParams(geo::PointI xy, quint8 zoom) = 0;
};

// FEAT-STO-019, FEAT-STO-020, FEAT-STO-021. Ports TTileStorageSQLiteFileConnection (:97-164,
// 179-594). Every operation method below returns a core::Result: legacy returns nil/False
// uniformly for "disabled" and "not found/nothing to do", but it is NOT uniformly exception-free
// -- a bind failure (FetchOne :310, FetchRectInfo :358, Delete :583) and a statement's first
// failed prepare (CheckPrepared :639, reached from FetchRectInfo :353, FetchNext :413, Delete
// :578, and -- through Acquire, at construction -- from initialize() itself) all raise, uncaught
// anywhere in u_TileStorageSQLiteFileConnection.pas (T-0022C1-review.md F1). A Result's error
// channel carries exactly that: "legacy raises" maps to an error Result, "legacy returns
// False/nil" maps to a successful `false`/null/nullptr value -- never the reverse. See
// ConnectionStatement::ensurePrepared's own comment for the one piece of this that is genuinely
// two-faced (a fresh prepare failure raises; a cached one does not).
// Abstract base *class* with real logic, not a GUID-style pure interface (see ConnectionStatement's
// own NOLINT comment above for why .clang-tidy's AbstractClassPrefix still fires here).
// NOLINTNEXTLINE(readability-identifier-naming)
class SqliteFileConnection {
public:
    virtual ~SqliteFileConnection() = default;

    SqliteFileConnection(const SqliteFileConnection&) = delete;
    SqliteFileConnection& operator=(const SqliteFileConnection&) = delete;

    // Ports the body of Create (u_TileStorageSQLiteFileConnection.pas:179-244): opens the sqlite3
    // file (ReadOnly with the `file:///...?immutable=1` URI of :218-222, else ReadWriteCreate plus
    // the three PRAGMAs of :225-227), and, only for the FIRST connection to this file
    // (`existingInfo == nullptr`), checks the file exists / creates its parent directory, then
    // calls createTables() (only when the file is new) and fetchMetadata() -- both flavour hooks.
    // Finally (every connection, not just the first -- MBTiles :226-228, RMaps :283-285, OruxMaps
    // :191-193) prepares tileDataStmt_ then tileInfoStmt_ and sets isEnabled() from the outcome
    // (F3 below). A flavour's factory function must call this exactly once, right after finishing
    // construction (its own statement fields assigned), before the connection is used for
    // anything else; every other method assumes it already succeeded.
    [[nodiscard]] core::Result<void> initialize(SqliteFileInfoPtr existingInfo);

    [[nodiscard]] const SqliteFileInfoPtr& fileInfo() const noexcept { return fileInfo_; }

    // Ports FEnabled. Legacy zero-initialises it (False) and only the flavour constructor's
    // `FEnabled := FTileDataStmt.CheckPrepared and FTileInfoStmt.CheckPrepared` (short-circuit
    // `and`; MBTiles :226-228) can raise it to True -- so it defaults to `false` here too, and
    // only initialize() ever sets it (T-0022C1-review.md F3). Every operation below checks this
    // FIRST and returns the disabled-connection value without touching db() -- there is no path
    // to a null-db_ dereference on a connection whose initialize() never enabled it, including one
    // that was never initialize()'d at all.
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    // Ports FetchOne (:282-331). `mode == LoadMode::WithData` selects the data statement, any
    // other mode the (data-less) info statement -- matches legacy's `AMode = gtimWithData` check
    // exactly (both gtimWithoutData and gtimAsIs take the info-statement branch). A disabled
    // connection and a missing row both come back as a successful TileInfoType::NotExists --
    // legacy returns nil for both and never distinguishes them (spec §4's convention;
    // file_system_tile_storage.cpp maps legacy's own nil the same way). A bind failure (:310-312,
    // uncaught in legacy) is the one case that comes back as an error Result (F1).
    [[nodiscard]] core::Result<TileInfo> fetchOne(geo::PointI xy, quint8 zoom, LoadMode mode);

    // Ports FetchRectInfo (:333-395). `stop` replaces legacy's (AOperationID, ACancelNotifier)
    // pair (idioms §8; ITileStorage::tileRectInfo already uses this shape). Checked before the
    // scan, every 1024 rows during it (:371, same constant) and once more after -- exactly like
    // legacy's three IsOperationCanceled checks, all three of which just return nil, same as
    // "disabled": a successful null TileRectInfoPtr. A bind failure (:358-360) and a FRESH
    // rectInfoStmt_ prepare failure (:353, F1) are errors; a CACHED prepare failure disables the
    // connection and returns null, same as legacy's `if not CheckPrepared then FEnabled := False`
    // (:353-356, legacy-defects L-40). A step failure mid-scan is NOT an error here either -- it
    // ends the scan exactly like a normal DONE does, and the rows collected so far are returned as
    // a (possibly partial) result, not null (:368, legacy-defects L-41, F4).
    [[nodiscard]] core::Result<TileRectInfoPtr> fetchRectInfo(geo::RectI rect, quint8 zoom,
                                                              const std::stop_token& stop);

    // Ports FetchNext (:397-460): a pull enumerator over enumTilesStmt_'s SELECT (a flavour hook
    // supplies the SQL, e.g. MBTiles' unfiltered `SELECT * FROM tiles`). A successful `false`
    // means exhausted, disabled, or a step failure (legacy's plain Boolean there, no raise) --
    // matching this port's own IEnumTileInfo::next() contract (tile_info.h) once the caller
    // unwraps the Result; SqliteFileEnumerator keeps a Result error for status() (D-45). A FRESH
    // enumTilesStmt_ prepare failure (:413) is an error (F1); a CACHED one disables the connection and
    // returns a successful `false` (L-40).
    [[nodiscard]] core::Result<bool> fetchNext(TileInfo& out, geo::PointI& xy, quint8& zoom);

    // Ports Insert (:462-563): an upsert with an explicit exists-check (tileInfoStmt_) inside a
    // BEGIN/COMMIT. `overwrite == false` and an existing row is legacy's "Ignore" branch
    // (:547-550), which returns WITHOUT a COMMIT or ROLLBACK, leaving the transaction open on this
    // connection until its next statement -- legacy-defects.md L-38, reproduced verbatim (not a
    // crash/UB/unsafe defect, so Rule 3 applies, not the Rule 2 exception -- see
    // docs/handoffs/T-0022C1-review.md's "open transaction on Insert-ignore" recommendation).
    // `FSQLite3DB.BeginTransaction` (:541) sits OUTSIDE legacy's try/except, so a failure there is
    // an error Result, not a caught `false` (F1); every other in-try failure (bind, a fresh
    // insert/update prepare, a step, updateMetadata (F2), COMMIT) is caught, rolls back and
    // returns a successful `false`, same as legacy's `except Result := False; RollbackTransaction`.
    [[nodiscard]] core::Result<bool> insertOrUpdate(geo::PointI xy, quint8 zoom, const QByteArray& data,
                                                    bool overwrite);

    // Ports Delete (:565-593). A bind failure (:583-585) and a FRESH deleteStmt_ prepare failure
    // (:578) are errors (F1); a CACHED prepare failure disables the connection and returns a
    // successful `false` (L-40); a step failure is a plain successful `false`, same as legacy.
    [[nodiscard]] core::Result<bool> deleteTile(geo::PointI xy, quint8 zoom);

protected:
    // `mainContentType` mirrors FileSystemTileStorage's own constructor parameter: no separate
    // ContentTypeManager type exists in this port, core::ContentTypeRegistry::standard() plays
    // that role (see tileContentType()). `contentTypeManager` restores legacy's dropped
    // `AContentTypeManager` parameter (T-0022C1-review.md F5): `GetTileContentTypeInfo` (:274)
    // falls back through it when a sniffed content type is unknown to the registry -- wired in by
    // tileContentType() below (T-0022C2).
    SqliteFileConnection(bool isReadOnly, QString fileName, core::ContentTypeInfo mainContentType,
                         const core::ContentTypeRegistry& contentTypeManager,
                         geo::ProjectionSetPtr projectionSet);

    // Flavour hooks (pure virtual; "flavour" = T-0022C2/C3/D1). Ports CreateTables/FetchMetadata/
    // UpdateMetadata (:119-121), called from initialize() (CreateTables, FetchMetadata) and from
    // insertOrUpdate() (UpdateMetadata). UpdateMetadata returns core::Result<void> (F2): MBTiles
    // raises on an invalid zoom value, RMaps raises through RaiseSQLite3Error
    // (u_TileStorageSQLiteFileConnectionMBTiles.pas:373-375,
    // u_TileStorageSQLiteFileConnectionRMaps.pas:624-627) -- both inside Insert's try, so the
    // exception is caught there and Insert rolls back and returns False, discarding the tile that
    // was otherwise about to be committed. An error Result here makes insertOrUpdate() do the same.
    [[nodiscard]] virtual core::Result<void> createTables() = 0;
    [[nodiscard]] virtual core::Result<void> fetchMetadata() = 0;
    [[nodiscard]] virtual core::Result<void> updateMetadata(geo::PointI xy, quint8 zoom) = 0;

    // Ports GetTileContentTypeInfo (:264-280): sniffs magic bytes via cg::core::detectContentType
    // (moved from cg::net to cg::core by T-0033), maps a match through contentTypeManager()
    // below, and falls back to mainContentType_ when the data is empty, nothing is sniffed, or the
    // sniffed type is unknown to the registry (T-0022C2).
    [[nodiscard]] QByteArray tileContentType(const QByteArray& data) const;

    [[nodiscard]] sqlite::SqliteDb& db() { return *db_; }
    [[nodiscard]] const TileDate& fileDate() const noexcept { return fileDate_; }
    [[nodiscard]] const geo::ProjectionSetPtr& projectionSet() const noexcept { return projectionSet_; }

    // F5: exposed for a flavour to consult directly too (its own content-type-bearing metadata,
    // e.g. MBTiles' `format` key, resolves through the same registry -- storage.md §2.17.4).
    [[nodiscard]] const core::ContentTypeRegistry& contentTypeManager() const noexcept {
        return *contentTypeManager_;
    }

    // Statement slots a flavour's constructor assigns as it needs them; nullptr is legal --
    // "this flavour has no such operation", the way OruxMaps leaves insert/update/delete null
    // (builder :149-158, read-only construction).
    std::unique_ptr<TileDataStatement> tileDataStmt_;
    std::unique_ptr<TileInfoStatement> tileInfoStmt_;
    std::unique_ptr<RectInfoStatement> rectInfoStmt_;
    std::unique_ptr<EnumTilesStatement> enumTilesStmt_;
    std::unique_ptr<InsertTileStatement> insertStmt_;
    std::unique_ptr<UpdateTileStatement> updateStmt_;
    std::unique_ptr<DeleteTileStatement> deleteStmt_;

private:
    enum class FetchNextState : std::uint8_t { None, Next, Done, Error };

    [[nodiscard]] core::Result<bool> tileExistsForWrite(geo::PointI xy,
                                                        quint8 zoom); // ports IsTileExists (:470-486)

    bool isReadOnly_ = false;
    QString fileName_;
    core::ContentTypeInfo mainContentType_;
    const core::ContentTypeRegistry* contentTypeManager_ = nullptr; // F5; see contentTypeManager()
    geo::ProjectionSetPtr projectionSet_;

    std::unique_ptr<sqlite::SqliteDb> db_;
    bool enabled_ = false; // F3: legacy's FEnabled is zero-initialised; only initialize() sets it
    FetchNextState fetchNextState_ = FetchNextState::None;

    SqliteFileInfoPtr fileInfo_;
    TileDate fileDate_;
};

} // namespace cg::storage::sqlite
