// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLite/u_TileStorageSQLiteHandler.pas (TTileStorageSQLiteHandler +
// TTileStorageSQLiteHandlerComplex, collapsed -- TTileStorageSQLiteHandlerComplex is the only
// concrete subclass anywhere in the tree, verified by grep), t_TileStorageSQLiteHandler.pas
// (TVersionColMode already ported as sqlite_region_sql.h's VersionColMode; TTBColInfo below as
// ColumnInfo), t_TileStorageSQLite.pas (TSaveTileFlag/TSaveTileFlags below as SaveFlag/SaveFlags).
//
// docs/specs/storage.md §2.16 (the native cache_sqlite), §2.16.2 "Column discovery", §2.16.3 "The
// statements". One instance per OPEN region file (one .sqlitedb, one 256x256 tile region at one
// zoom) -- NOT the pooling/lazy-open/idle-close layer, which is T-0022B5's SqliteRegionFileMap.
// Non-virtual: this ticket (T-0022B2) defines only the constructor and column discovery below;
// T-0022B3 defines tileInfo/listTileVersions/listVersions (sqlite_region_handler_read.cpp) and
// T-0022B4 defines saveTile/deleteTile/tileRectInfo (sqlite_region_handler_write.cpp) as member
// functions of this SAME class, in their own translation units -- private members are visible to
// them without any accessor because C++ access control is per-class, not per-file.
//
// Design notes (verified against legacy, not guessed):
// - No `zoom` parameter anywhere, including tileRectInfo's (T-0022B2 review finding 1): table `t`
//   has no z column, and stamping the accumulated `TileRectInfoEntry` items with a zoom is B5's job
//   once it builds the final `TileRectInfo` from the shared accumulator (see tileRectInfo's comment
//   below). GetTileInfo/GetListOfTileVersions/GetSQL_DeleteTile/SaveTile/GetTileRectInfo's
//   AZoom/SZoom/DZoom/DestZoom parameters are declared in legacy's interfaces but never read in
//   u_TileStorageSQLiteHandler.pas (grep confirms it): the physical region file already IS one
//   zoom, so nothing here needs to know which one.
// - `FSingleVersionOnly` (legacy constructor parameter ASingleVersionOnly) is assigned to a field
//   and never read anywhere else in the unit -- dropped.
// - deleteTile DOES take a version (unlike the spec §2.16.3 table's "all versions" shorthand for
//   the delete row): GetSQL_DeleteTile (u_TileStorageSQLiteHandler.pas:854-881) adds
//   `AND <VersionFieldIsEqual predicate>` whenever the table has a version column, exactly like
//   every other operation. Flagged in the T-0022B2 handoff as a spec/legacy disagreement; legacy's
//   code wins per project rule.
// - The per-file mutex of spec §3.3/D-10 is NOT a member here (ticket Scope out): this class is as
//   thread-unsafe as the SqliteDb it wraps; T-0022B5's file map owns one core::Mutex per open
//   handler and holds it for the whole of every call into these methods.
// - A read-only-opened handler (see the constructor, T-0022B2.cpp) skips the session/established
//   SQL and column discovery entirely, exactly as legacy's Create does (the `if AIsReadOnly ...
//   else begin ... end` branch, u_TileStorageSQLiteHandler.pas:372-392) -- columnInfo() stays at
//   ColumnInfo{} (columnCount 0, hasVersion false) for such a handler. This looks surprising but
//   is legacy behaviour, reproduced; see docs/handoffs/T-0022B2.md "Legacy defects noticed".

#include "cg/core/result.h"
#include "cg/geo/geo_types.h"
#include "cg/storage/map_version.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/sqlite/sqlite_region_schema.h"
#include "cg/storage/sqlite/sqlite_region_sql.h"
#include "cg/storage/tile_date.h"
#include "cg/storage/tile_info.h"
#include "cg/storage/tile_rect_info.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QFlags>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <stop_token>
#include <vector>

namespace cg::storage::sqlite {

// Ports TSaveTileFlag/TSaveTileFlags (t_TileStorageSQLite.pas:47-51). spec §2.16.3.
enum class SaveFlag : quint8 { KeepExisting = 1, SkipIfSameAsPrev = 2 };
Q_DECLARE_FLAGS(SaveFlags, SaveFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(SaveFlags)

// FEAT-STO-018.
class SqliteRegionHandler final {
public:
    // Ports TTBColInfo (t_TileStorageSQLiteHandler.pas:29-38), discovered once by the constructor
    // (§2.16.2). `versionMode` is meaningful only when `hasVersion` is true -- legacy's third
    // TVersionColMode value, vcm_None, becomes `hasVersion == false` here (T-0022B1 handoff, "What
    // T-0022B2 must know").
    struct ColumnInfo {
        int columnCount = 0;
        bool hasVersion = false;
        VersionColMode versionMode = VersionColMode::Int;
        bool hasContentType = false;
        bool hasCrc = false;
    };

    // Ports TTileStorageSQLiteHandlerComplex.Create (u_TileStorageSQLiteHandler.pas:353-400).
    // `schema` is shared with every other handler opened under the same storage root (its s.sql/
    // t.sql/e.sql are read once, not per file). `useVersionFieldInDb` ports AUseVersionFieldInDB.
    // `readOnly` ports AIsReadOnly -- see the header comment above for what it skips. Never
    // throws: on any failure the object is left with isOpened() == false and the failure is
    // reported through schema->logError(Init, ...), exactly as legacy's Create swallows every
    // exception and leaves FSQLite3DbHandler closed.
    SqliteRegionHandler(std::shared_ptr<SqliteRegionSchema> schema, QString dbFilename,
                        bool useVersionFieldInDb, bool readOnly);
    ~SqliteRegionHandler();

    SqliteRegionHandler(const SqliteRegionHandler&) = delete;
    SqliteRegionHandler& operator=(const SqliteRegionHandler&) = delete;

    // Ports Opened.
    [[nodiscard]] bool isOpened() const noexcept { return db_ != nullptr; }

    [[nodiscard]] const ColumnInfo& columnInfo() const noexcept { return columnInfo_; }
    [[nodiscard]] const QString& dbFilename() const noexcept { return dbFilename_; }

    // T-0276: the text of the failure that left isOpened() false (empty when it opened). Legacy only logs it.
    [[nodiscard]] const QString& openError() const noexcept { return openError_; }

    // Exposed for T-0022B3/B4's member functions and for tests; nullptr when !isOpened().
    [[nodiscard]] SqliteDb* db() noexcept { return db_.get(); }

    // --- T-0022B3 (sqlite_region_handler_read.cpp): declared here, defined there. ---

    // Ports GetTileInfo (u_TileStorageSQLiteHandlerComplex, :949-1039). `useOtherVersions` ports
    // AUseOtherVersions (VersionFieldIsEqualOrMax's "this version, or the newest").
    [[nodiscard]] core::Result<TileInfo> tileInfo(geo::PointI xy, const MapVersionInfoPtr& version,
                                                  bool useOtherVersions, LoadMode mode);

    // Ports GetListOfTileVersions (:773-816): the versions this ONE tile (x,y) has, newest first.
    // `stop` ports AOper's per-row poll (CallbackSelectVersions, :730-735): legacy checks
    // IsOperationCancelled before adding EACH row and, once cancelled, adds no more. T-0022B3
    // correction (this comment previously said the partial list was kept -- it is not):
    // TSelectTileVersions.GetMapVersionInfoStatic (:243-250) returns nil whenever `Cancelled` is
    // set, UNCONDITIONALLY, discarding every row collected before the cancellation was observed --
    // `if not Cancelled and (ListOfVersions.Count > 0) then ... else Result := nil`. The port
    // reproduces exactly that: a cancellation (at any point, including after rows were collected)
    // yields an empty `MapVersionList` (nullptr), the same value an empty-from-the-start result
    // would. This is success with an all-or-nothing result, never a core::Result error -- matching
    // legacy's own try/except, which logs (c_Log_GetVer) and swallows a genuine query failure the
    // same way, also to nil.
    // T-0022B3 fix: `stop` is `const std::stop_token&`, not by value as first declared -- this
    // method (unlike ITileStorage::tileRectInfo's identically-shaped `stop` parameter) is not a
    // virtual override, so clang-tidy's performance-unnecessary-value-param fires on it (the body
    // only ever reads `stop`, never moves/copies it). Same convention already used for a similar
    // spec-vs-clang-tidy conflict in proxy_config.h's normalizeAddress -- a parameter-passing
    // detail, no behavior change. B4's `tileRectInfo` has the identical `std::stop_token stop` by
    // value and, being non-virtual too, will likely need the same fix -- flagged in the handoff.
    [[nodiscard]] core::Result<MapVersionList> listTileVersions(geo::PointI xy, const std::stop_token& stop);

    // Ports GetListOfVersions (:817-853): every distinct version anywhere in this region file.
    [[nodiscard]] core::Result<MapVersionList> listVersions();

    // --- T-0022B4 (sqlite_region_handler_write.cpp): declared here, defined there. ---

    // Ports SaveTile (:1075-1229, TNE at :1196-1204). Legacy decides "tile does not exist" (TNE)
    // from the DATA alone -- `VIsTne := (Size = 0) or (Body = nil)` (:1117) -- NOT from
    // `contentType`: a save with an empty/null content type but a non-empty `data` stores a real
    // tile with `c = NULL` (main content type), not a TNE.
    // Returns false (not an error) when `flags` has KeepExisting and a row already exists, or
    // SkipIfSameAsPrev and the existing row's h/s already matches.
    [[nodiscard]] core::Result<bool> saveTile(geo::PointI xy, const MapVersionInfoPtr& version,
                                              TileDate loadDate, QByteArrayView contentType,
                                              const QByteArray& data, SaveFlags flags);

    // Ports DeleteTile (:401-425) / GetSQL_DeleteTile (:854-881). Takes `version` because legacy's
    // SQL DOES filter by it when the table has a version column -- see the header comment above.
    // The T-0022B2 review walked the exact SQL legacy emits (GetSQL_DeleteTile + ParseSQLiteDBVersion/
    // VersionFieldIsEqual, u_TileStorageSQLiteFunc.pas); B4 must reproduce all three forms:
    //   1. No `v` column: `DELETE FROM t WHERE x=X AND y=Y` -- deletes every version of the tile
    //      (the ONLY "all versions" case; spec §2.16.3's old "delete = all versions" summary was
    //      wrong for the other two forms and has been corrected).
    //   2. `v` column, a real version given (useVersionFieldInDb and a non-empty StoreString): adds
    //      `AND <predicate>` where the predicate depends on the column's declared type and whether
    //      the version string parses as an integer (TryStrToInt64, first char not in `x X $`):
    //      INT column + int version -> `v=<n>`; INT column + text version -> `cast(v as TEXT)='<s>'`;
    //      TEXT column + int version -> `cast(v as TEXT)='<n>'`; TEXT column + text version ->
    //      `v='<s>'`.
    //   3. `v` column, no version (nil/empty StoreString, or useVersionFieldInDb = false): the
    //      default version only. INT column: `AND v=0`. TEXT column: `AND cast(v as TEXT)=''`
    //      (RequestedVersionIsInt is True for the default, which picks the cast branch).
    [[nodiscard]] core::Result<bool> deleteTile(geo::PointI xy, const MapVersionInfoPtr& version);

    // Ports GetTileRectInfo (:444-469) / GetSQL_TileRectInfo (:882-943), REWORKED per the T-0022B2
    // review (finding 1): legacy fills ONE caller-owned `TTileInfoShortEnumData.RectItems` array,
    // sized for the WHOLE destination rect and shared across every region file the caller (B5)
    // covers (`u_TileStorageSQLiteHelper.pas:405-406, :444-484`), ORing each file's "not cancelled"
    // result -- a fresh result per file would force B5 to invent a merge that legacy does not have.
    // `destRect`/`items` port that: `items` is indexed exactly like TileRectInfo::indexOf (row-major,
    // `(x - destRect.left) + (y - destRect.top) * destRect.width()`; tile_rect_info.h), already sized
    // by the caller to `destRect`'s area, and every entry this file has data for is written in place
    // -- entries this file has nothing for are left untouched, so B5 can call this once per covered
    // region file over the SAME `items` and then hand it to `TileRectInfo`'s constructor. `version`
    // ports `AEnumData.RectVersionInfo`: legacy adds the version predicate (or, with
    // `useOtherVersions`, an `ORDER BY ... DESC` for "newest") whenever the table has a version
    // column and `!useOtherVersions` (`GetSQL_TileRectInfo :914-934`) -- needed to build that
    // predicate at all, which the pre-review header (no version parameter) could not do. `zoom` is
    // dropped: it only stamped the returned `TileRectInfo`, which is now B5's job once every covered
    // file has been folded into `items`. Returns `!cancelled` (`stop`, polled per tile, spec §3.1
    // rule 4); like legacy's `GetTileRectInfo`, any SQL exception is logged under `GetMap` and
    // swallowed (`:462-465`) -- never a core::Result error.
    [[nodiscard]] bool tileRectInfo(geo::RectI destRect, const MapVersionInfoPtr& version,
                                    bool useOtherVersions, std::vector<TileRectInfoEntry>& items,
                                    const std::stop_token& stop);

private:
    // Ports InternalCheckStructure (:1040-1074) / CallbackSelectCols (:486-525). Resets
    // columnInfo_ and re-derives it from `SELECT * FROM t WHERE 0=1`'s column metadata; leaves
    // columnCount == 0 if `t` does not exist (no error -- the caller creates it and calls again;
    // ports DeclareSql's ARaiseOnOpenError = False for the PREPARE call specifically,
    // u_SQLite3Handler.pas:594-596). Returns fail() when the query PREPAREd but its `step()` did
    // not (BUSY past the timeout, IOERR, CORRUPT, ...): DeclareSql ends with an UNCONDITIONAL
    // `CheckResult(sqlite3_finalize(...))` (:605), which raises into Create's `except` regardless of
    // ARaiseOnOpenError -- the caller must fail the open on this, unlike a PREPARE failure.
    [[nodiscard]] core::Result<void> discoverColumns();

    std::shared_ptr<SqliteRegionSchema> schema_;
    QString dbFilename_;
    QString openError_;
    bool useVersionFieldInDb_ = false;
    std::unique_ptr<SqliteDb> db_; // null when not opened (construction failure, or see isOpened())
    ColumnInfo columnInfo_;
};

} // namespace cg::storage::sqlite
