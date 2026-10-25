// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLiteFile/u_TileStorageSQLiteFileConnectionRMaps.pas (728 lines):
//        :137-207 (InvertZ, SwapMinMaxZoom, UpdateZooms, StrToZooms, ZoomsToStr), :208-397 (the
//        constructor, CreateTables, FetchMetadata, UpdateMetadata), :398-728 (the seven statement
//        classes + TMetadataConnectionStatementRMaps.ExecUpdate).
//
// docs/specs/storage.md §2.17.2 "RMaps / OsmAnd / Locus". Depends on the format-independent base
// landed by T-0022C1 (sqlite_file_connection.h) and reuses the placeholder-then-finishOpen()
// two-phase-construction shape T-0022C2 established for MBTiles (mbtiles_connection.h's own top
// comment; T-0022C2-review.md's "For C3" note).
//
// One class covers all three flavours (RMaps, OsmAnd, Locus) -- they share the `tiles` table, the
// seven statement SQL texts and the read/write logic; only the `info` table's shape, whether Z is
// inverted, and Locus's extra min/max-swap + `zooms` bookkeeping differ, all driven by `format_`.
// Y is NEVER inverted in this family (docs/specs/storage.md §2.17.2, RMaps.pas has no
// FIsInvertedY-consuming logic at all): every ConnectionStatement below is built with
// invertedY = false, unlike MBTiles.
//
// Z-inversion timing: RMaps/Locus always invert Z (known immediately, no metadata dependency);
// OsmAnd inverts Z only when `info.tilenumbering` equals "BigPlanet" (case-insensitive) -- not
// known until fetchMetadata() has run, the same "known only after fetchMetadata()" timing MBTiles'
// `scheme` key has. So this class uses the same constructor-placeholder + finishOpen() shape for
// ALL three formats uniformly (T-0022C2-review.md: "OsmAnd's tilenumbering has the same timing as
// scheme; reuse the constructor-placeholder + finishOpen() shape"): the constructor builds
// tileDataStmt_/tileInfoStmt_ with a placeholder invertedZ (their SQL text does not depend on it),
// and finishOpen() -- called by open() right after initialize() succeeds -- computes the real
// invertedZ from `format_`/metadata and rebuilds all seven statement objects (re-preparing
// tileData/tileInfo when the connection is enabled, mirroring mbtiles_connection.cpp's own gate).

#include "cg/storage/sqlite/sqlite_file_connection.h"

#include <QList>
#include <QString>

#include <memory>

namespace cg::storage::sqlite {

// FEAT-STO-020, FEAT-STO-021. Ports TTileStorageSQLiteFileConnectionRMaps.
// NOLINTNEXTLINE(readability-identifier-naming)
class RmapsConnection final : public SqliteFileConnection {
private:
    // Passkey (mbtiles_connection.h's own established idiom, review T-0022C2 finding 4): the
    // constructor below cannot be reached from outside this class, so open() is the only way to
    // obtain an instance, and it always follows up with initialize() and finishOpen().
    struct ConstructKey {
        explicit ConstructKey() = default;
    };

public:
    // Ports the constructor (:208-286) plus what T-0022C1's initialize() does not cover: builds
    // the object, calls initialize(), then finishOpen(). The only entry point -- format must be
    // one of SqliteFileFormat::{RMaps, OsmAnd, Locus}; any other value fails the same way legacy's
    // `else raise Exception.CreateFmt('RMaps: Unsupported FormatID = %d', ...)` does (:241,
    // reached from CreateTables (:335) when a fresh file is being created, and again from
    // finishOpen() below for every open, matching legacy's two raise sites).
    [[nodiscard]] static core::Result<std::unique_ptr<RmapsConnection>>
    open(bool isReadOnly, QString fileName, core::ContentTypeInfo mainContentType,
         const core::ContentTypeRegistry& contentTypeManager, geo::ProjectionSetPtr projectionSet,
         SqliteFileInfoPtr existingInfo, SqliteFileFormat format);

    // Builds the object with a placeholder invertedZ (see the class doc comment above). Only
    // callable by open(), via the ConstructKey passkey.
    RmapsConnection(ConstructKey, bool isReadOnly, QString fileName, core::ContentTypeInfo mainContentType,
                    const core::ContentTypeRegistry& contentTypeManager, geo::ProjectionSetPtr projectionSet,
                    SqliteFileFormat format);

private:
    [[nodiscard]] core::Result<void> createTables() override;
    [[nodiscard]] core::Result<void> fetchMetadata() override;
    [[nodiscard]] core::Result<void> updateMetadata(geo::PointI xy, quint8 zoom) override;

    // Ports the constructor's body (:222-285) -- see the class doc comment. Called by open() once
    // initialize() has succeeded.
    [[nodiscard]] core::Result<void> finishOpen();

    // Ports TMetadataConnectionStatementRMaps.ExecUpdate (:593-726): reconciles minZoom_/maxZoom_/
    // zooms_ against the `info` row currently on disk (a fresh SELECT, not the in-memory cache --
    // same cross-connection convention mbtiles_connection.cpp's execSelect()/updateMetadata() use)
    // and writes back only what actually changed.
    [[nodiscard]] core::Result<void> execMetadataUpdate();

    bool isReadOnly_;
    core::ContentTypeInfo mainContentType_;
    SqliteFileFormat format_; // one of {RMaps, OsmAnd, Locus}; validated by finishOpen()/createTables()

    // Ports FMinZoom/FMaxZoom/FZooms (:60-62): the connection's own running totals, in "business"
    // orientation (min <= max, already un-inverted and, for Locus, un-swapped) -- seeded from
    // fileInfo() in finishOpen() and updated in-memory by updateMetadata(). FZooms is populated
    // (and consulted) only for Locus; empty otherwise.
    int minZoom_;
    int maxZoom_;
    QList<int> zooms_;

    // Ports FIsInvertedZ, computed once in finishOpen() (see the class doc comment) and reused by
    // both the statement objects (their own copy, via ConnectionStatement::invertedZ()) and
    // execMetadataUpdate()'s own zoom arithmetic.
    bool invertedZ_ = false;

    // Write access only (:275-280): the metadata UPDATE and its paired SELECT (FMetadataStmt /
    // FMetadataStmt.FSelectStmt, :565-585). Built in finishOpen() only when !isReadOnly_.
    std::unique_ptr<ConnectionStatement> metadataUpdateStmt_;
    std::unique_ptr<ConnectionStatement> metadataSelectStmt_;
};

} // namespace cg::storage::sqlite
