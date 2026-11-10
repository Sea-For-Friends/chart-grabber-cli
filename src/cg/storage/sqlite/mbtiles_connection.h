// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLiteFile/u_TileStorageSQLiteFileConnectionMBTiles.pas (705 lines):
//        :131-158 (InvertY, StrToBounds), :159-443 (Create, CreateTables, FetchMetadata,
//        UpdateMetadata), :446-705 (the seven statement classes + the metadata select/upsert).
//
// The MBTiles format. Depends on the format-independent base (sqlite_file_connection.h) -- read
// that header's own top comment first.
//
// The MBTiles flavour of the single-file SQLite cache: schema, the seven statement hooks, and the
// `metadata` table (name/description/format/scheme/crs on create; minzoom/maxzoom/bounds/center
// maintained on every write). An MBTiles file written by this class must be readable by legacy and
// by standard MBTiles readers, and this class must read files legacy wrote -- table/index
// definitions, metadata rows, and TMS row flipping are all observable file format, not an
// implementation detail (legacy wins).
//
// Two-phase construction, the same shape as sqlite_file_connection.h (see its top comment): a
// private constructor builds the object, then MbtilesConnection::open() calls the base's
// initialize() and finishes construction. One extra wrinkle is specific to MBTiles: legacy's derived
// constructor (u_TileStorageSQLiteFileConnectionMBTiles.pas:159-229) reads `FFileInfo` (format
// check, bounds/minzoom/maxzoom, and -- critically -- the `scheme` key that decides
// FTileDataStmt/FTileInfoStmt's Y-inversion) AFTER `inherited Create` has already run FetchMetadata
// through Object Pascal's virtual dispatch into a not-yet-fully-constructed object -- a trick C++
// cannot play. So this port's constructor builds tileDataStmt_/tileInfoStmt_ with a placeholder
// inversion flag (their SQL text does not depend on it, only their bind/result logic does --
// initialize()'s eager prepare of these two is therefore unaffected either way), calls initialize()
// (which populates fileInfo() the same way legacy's FetchMetadata did), and only then -- in
// finishOpen(), after the real `scheme` is known -- rebuilds all seven statement objects
// (re-preparing tileData/tileInfo if the connection is enabled, since
// fetchOne()/tileExistsForWrite() assume they are already prepared and skip ensurePrepared()
// themselves) with the correct Y-inversion. No caller ever observes the placeholder: open() only
// returns a connection after finishOpen() has completed.

#include "cg/storage/sqlite/sqlite_file_connection.h"

#include <QString>

#include <array>
#include <memory>

namespace cg::storage::sqlite {

// Ports TTileStorageSQLiteFileConnectionMBTiles.
// NOLINTNEXTLINE(readability-identifier-naming)
class MbtilesConnection final : public SqliteFileConnection {
private:
    // Passkey: the constructor below cannot be reached from outside this class -- a caller cannot
    // name `ConstructKey` (private), so cannot construct one to pass in, so cannot call the
    // constructor -- open() (public, below) is the only way to obtain an instance, and it always
    // follows up with initialize() and finishOpen(). The constructor itself stays technically
    // public (rather than a plain `private:` constructor) because std::make_unique needs public
    // access to call it, and tools/validate.sh's RAW-NEW rule forbids a bare `new` outside
    // make_shared/make_unique.
    struct ConstructKey {
        explicit ConstructKey() = default;
    };

public:
    // Ports the constructor (:159-229) plus what the base's initialize() does not cover (see the
    // class doc comment above): builds the object, calls initialize(), then finishOpen(). Mirrors
    // the shape the ConnectionMaker calls. The only entry point -- a caller cannot build an
    // object without finishOpen() running.
    [[nodiscard]] static core::Result<std::unique_ptr<MbtilesConnection>>
    open(bool isReadOnly, QString fileName, core::ContentTypeInfo mainContentType,
         const core::ContentTypeRegistry& contentTypeManager, geo::ProjectionSetPtr projectionSet,
         SqliteFileInfoPtr existingInfo);

    // Builds the object with a placeholder Y-inversion flag (see the class doc comment above) --
    // matches the base's own two-phase-construction contract ("construct all statements in your
    // own constructor before the maker calls initialize()"). Only callable by open() (above), via
    // the ConstructKey passkey, so the constructor cannot be reached without open()'s finishOpen()
    // call.
    MbtilesConnection(ConstructKey, bool isReadOnly, QString fileName, core::ContentTypeInfo mainContentType,
                      const core::ContentTypeRegistry& contentTypeManager,
                      geo::ProjectionSetPtr projectionSet);

private:
    [[nodiscard]] core::Result<void> createTables() override;
    [[nodiscard]] core::Result<void> fetchMetadata() override;
    [[nodiscard]] core::Result<void> updateMetadata(geo::PointI xy, quint8 zoom) override;

    // Ports the derived constructor's body that runs after `inherited Create` (:172-228) -- see
    // the class doc comment. Called by open() once initialize() has succeeded.
    [[nodiscard]] core::Result<void> finishOpen();

    // Ports DoUpdateMetadata (:354-358): upserts through metadataUpsertStmt_ AND mirrors the write
    // into fileInfo()'s shared metadata cache.
    [[nodiscard]] core::Result<void> doUpdateMetadata(const QString& name, const QString& value);

    // Ports TMetadataConnectionStatementMBTiles.ExecSelect (:651-684): the current bounds/center/
    // maxzoom/minzoom metadata rows, indices 0..3 in that order.
    [[nodiscard]] core::Result<std::array<QString, 4>> execSelect();

    bool isReadOnly_;
    core::ContentTypeInfo mainContentType_;

    // Ports FBounds/FMinZoom/FMaxZoom (:52-54): the connection's own running totals, seeded from
    // fileInfo() in finishOpen() and updated in-memory by updateMetadata() (only written back to
    // the `metadata` table -- and to fileInfo() -- when they actually change something).
    geo::LonLatRect bounds_;
    int minZoom_;
    int maxZoom_;

    // Read access: always built (finishOpen() rebuilds tileDataStmt_/tileInfoStmt_, inherited from
    // the base, plus rectInfoStmt_/enumTilesStmt_, also inherited). Write access
    // (insertStmt_/updateStmt_/deleteStmt_, also inherited) plus these two metadata statements:
    // only when !isReadOnly_ (:218-224).
    std::unique_ptr<ConnectionStatement> metadataUpsertStmt_;
    std::unique_ptr<ConnectionStatement> metadataSelectStmt_;
};

} // namespace cg::storage::sqlite
