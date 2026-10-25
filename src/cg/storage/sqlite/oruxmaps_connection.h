// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLiteFile/u_TileStorageSQLiteFileConnectionOruxMaps.pas (370 lines):
//        :110-157 (SetBit/IsBitSet/BasePointsStrToArray), :158-226 (the constructor, CreateTables,
//        FetchMetadata, UpdateMetadata -- the refusal and the write hooks), :227-370 (the four
//        statement classes). Base: Src/TileStorage/SQLiteFile/u_TileStorageSQLiteFileConnection.pas.
//        The base64 blob format BasePointsStrToArray decodes: Includes/OruxMapsXmlFile.pas:180-285
//        (TOruxMapsCalibrationInfo.SaveToStream/LoadFromStream/*Base64String).
//
// docs/specs/storage.md §2.17.3 "OruxMaps (read-only)". Depends on the format-independent base
// landed by T-0022C1 (sqlite_file_connection.h) and reuses the placeholder-then-finishOpen()
// two-phase-construction shape T-0022C2/T-0022C3 established (mbtiles_connection.h's own top
// comment; rmaps_connection.h's own top comment).
//
// The OruxMaps flavour: `tiles(x, y, z, image)` -- no `s` column, unlike the RMaps family --
// always opened read-only (legacy hardcodes `inherited Create(True, ...)`, :169; the isReadOnly a
// caller might otherwise pass for the other flavours is not a parameter here at all, matching that
// legacy never consults one). Refuses to open when no sibling `*.otrk2.xml` file sits next to the
// `.db`; on a first open (no existing file info) fetchMetadata() parses that file's calibration
// (TOruxMapsXmlFile.Parse, Includes/OruxMapsXmlFile.pas:62-178) and stores it as the `base_points`
// value (SaveToBase64String), as legacy does. CreateTables/UpdateMetadata raise (:196-199, :219-222);
// every write statement
// (insert/update/delete) is simply never built, so the base's own null-statement guards make
// insertOrUpdate()/deleteTile() report a harmless `false`, the same way legacy leaves
// FInsertTileStmt/FUpdateTileStmt/FDeleteTileStmt nil (:194-197) for the base's own null checks to
// catch.
//
// Per-zoom tile offset: unlike MBTiles' single invertedY flag or the RMaps family's single
// invertedZ flag, OruxMaps' four read statements each carry the SAME bits/basePoints pair (bit N
// set <=> the calibration had a layer for zoom N; basePoints[N] is that layer's tile origin) --
// decoded once by basePointsStrToArray() from the `base_points` metadata key (stored by fetchMetadata())
// and threaded into every statement object by finishOpen(), the same
// placeholder-then-rebuild shape MBTiles/RMaps use for their own per-connection flags.

#include "cg/storage/sqlite/sqlite_file_connection.h"

#include <QString>

#include <array>
#include <memory>

namespace cg::storage::sqlite {

// Ports TryGetOruxMapsXmlFileName (u_TileStorageSQLiteFileImporter.pas:303-314) and the lookup in
// FetchMetadata: the first '*.otrk2.xml' beside the .db file, in name order (D-43); "" when none.
[[nodiscard]] QString oruxmapsSiblingXmlFile(const QString& dbFilePath);

// FEAT-STO-019. Ports TTileStorageSQLiteFileConnectionOruxMaps.
// NOLINTNEXTLINE(readability-identifier-naming)
class OruxmapsConnection final : public SqliteFileConnection {
private:
    // Passkey (mbtiles_connection.h / rmaps_connection.h's own established idiom): the constructor
    // below cannot be reached from outside this class, so open() is the only way to obtain an
    // instance, and it always follows up with initialize() and finishOpen().
    struct ConstructKey {
        explicit ConstructKey() = default;
    };

public:
    // Ports the constructor (:158-193) plus what T-0022C1's initialize() does not cover: builds
    // the object, calls initialize(), then finishOpen(). Always read-only -- see the class doc
    // comment above; there is no isReadOnly parameter to be ignored, unlike a caller ignoring one.
    [[nodiscard]] static core::Result<std::unique_ptr<OruxmapsConnection>>
    open(QString fileName, core::ContentTypeInfo mainContentType,
         const core::ContentTypeRegistry& contentTypeManager, geo::ProjectionSetPtr projectionSet,
         SqliteFileInfoPtr existingInfo);

    // Builds the object with placeholder (empty) bits/basePoints -- their SQL text does not depend
    // on either (only bindParams()/result() do), so initialize()'s eager prepare of
    // tileDataStmt_/tileInfoStmt_ succeeds/fails identically regardless of this placeholder;
    // finishOpen() rebuilds both with the real, decoded values. Only callable by open(), via the
    // ConstructKey passkey.
    OruxmapsConnection(ConstructKey, QString fileName, core::ContentTypeInfo mainContentType,
                       const core::ContentTypeRegistry& contentTypeManager,
                       geo::ProjectionSetPtr projectionSet);

private:
    // Always fails (:196-199, :219-222): OruxMaps files are read-only, there is no schema to
    // create and no metadata to maintain on write.
    [[nodiscard]] core::Result<void> createTables() override;
    [[nodiscard]] core::Result<void> fetchMetadata() override;
    [[nodiscard]] core::Result<void> updateMetadata(geo::PointI xy, quint8 zoom) override;

    // Ports the constructor's body that runs after `inherited Create` (:169-193) -- see the class
    // doc comment. Called by open() once initialize() has succeeded.
    [[nodiscard]] core::Result<void> finishOpen();

    // Ports FBits/FBasePoints -- legacy gives every one of the four statement classes its own copy
    // (:227-370); this port keeps one shared copy here and threads it (by value) into each
    // statement object instead, since all four are always rebuilt together in finishOpen().
    quint32 bits_ = 0;
    std::array<geo::PointI, 24> basePoints_{};
};

} // namespace cg::storage::sqlite
