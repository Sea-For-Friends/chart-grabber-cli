// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileStorageArchive.pas (Create, GetTileFileName, GetTileInfo, GetTileInfoEx,
// DeleteTile, SaveTile, GetListOfTileVersions, GetTileRectInfo, ScanTiles, TEnumTileInfoArchive),
// u_TileStorageTypeArchive.pas (the two BuildStorageInternal call shapes -- write vs. scan --
// collapsed into the two static factories below). Behaviour reference only (replaced by
// libarchive, not ported line-by-line): Src/ArchiveReadWrite/u_ArchiveReadWriteSequentialTar.pas,
// Src/ArchiveReadWrite/u_ArchiveReadWriteSequentialZip.pas.
//
// docs/backlog/T-0024-storage-archive.md, docs/specs/storage.md §2.19/§3.5. A sequential,
// write-XOR-scan tile storage backed by a tar or zip archive (libarchive 3.8.9). Never registered
// as a cache type (TileStorageKind::Archive is never wired into cache_type.cpp's factory list,
// exactly as legacy comments out every TTileStorageTypeArchiveTar* registration) -- the only
// intended construction site is the (not-yet-built) Cache Manager "convert to/from archive" flow
// (frm_CacheManager.pas:412), out of scope here (ticket Scope out).
//
// Legacy passes an already-open IArchiveReaderBase/IArchiveWriterBase to
// TTileStorageArchive.Create (built by frm_CacheManager.CreateSimpleTileStorage:378-396, which
// picks reader-vs-writer by AStorageAbilities.AllowScan/AllowAdd and asserts they are never both
// set -- frm_CacheManager.pas:402). The port collapses "build the right
// reader/writer-then-construct" into two named static factories that do both steps at once,
// exactly mirroring which of the two ways u_TileStorageTypeArchive.pas:82-107's caller-supplied
// TTileStorageAbilities is used for.

#include "cg/core/content_type.h"
#include "cg/core/result.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/projection_set.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_info.h"
#include "cg/storage/tile_name_codec.h"
#include "cg/storage/tile_pyramid_notifier.h"
#include "cg/storage/tile_storage.h"
#include "cg/storage/tile_storage_abilities.h"
#include "cg/storage/tile_storage_base.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <memory>
#include <mutex>

// Opaque forward declaration only -- <archive.h>/<archive_entry.h> stay confined to
// archive_tile_storage.cpp (tools/validate.sh ARCHIVE-LEAK rule, mirrors SQLITE3-LEAK).
struct archive;

namespace cg::storage {

// FEAT-STO-027. Ports TTileStorageArchive + the write/scan halves of TTileStorageTypeArchive.
class ArchiveTileStorage final : public ITileStorage {
public:
    enum class Container : std::uint8_t { Tar, Zip };

    // Opens `file` for writing and returns a write-only instance (`saveTile` is the only working
    // method; every other ITileStorage method returns "nothing", exactly as legacy). `zipLevel` is
    // ignored for `Container::Tar`; for `Container::Zip`, 0 means store (no compression, legacy's
    // own default when the Cache Manager's zip panel is skipped -- MZ_COMPRESS_METHOD_STORE,
    // u_ArchiveReadWriteSequentialZip.pas:174-178) and 1-9 selects deflate at that level.
    // `mainContentType` is legacy's `FContentType` ctor argument (used only by the display-only
    // `tileFileName()` -- `saveTile` uses each call's own `contentType` argument for the entry
    // extension, since `supportsMixedContentTypes == true` for this backend, same as legacy).
    [[nodiscard]] static core::Result<std::shared_ptr<ArchiveTileStorage>>
    openForWrite(QString file, Container container, int zipLevel, ITileNameCodecPtr codec,
                 core::ContentTypeInfo mainContentType, geo::ProjectionSetPtr projectionSet,
                 MapVersionFactoryPtr mapVersionFactory, TilePyramidNotifierPtr tileNotifier);

    // Opens `file` for a single sequential scan (auto-detects tar vs. zip, exactly as libarchive's
    // archive_read_support_format_all does -- legacy's caller already knows which reader class to
    // build, storage.md §2.19's own signature carries no Container parameter). Returns a scan-only
    // instance (`scanTiles` is the only working method).
    [[nodiscard]] static core::Result<std::shared_ptr<ArchiveTileStorage>>
    openForScan(QString file, ITileNameCodecPtr codec, geo::ProjectionSetPtr projectionSet,
                MapVersionFactoryPtr mapVersionFactory, TilePyramidNotifierPtr tileNotifier);

    // Public (not private) for std::make_shared, matching this module's convention
    // (FileSystemTileStorage, RamTileStorage): call openForWrite()/openForScan() instead of this
    // directly, they are what wires up the actual libarchive reader/writer.
    ArchiveTileStorage(TileStorageTypeAbilities typeAbilities, TileStorageAbilities resolvedAbilities,
                       MapVersionFactoryPtr mapVersionFactory, geo::ProjectionSetPtr projectionSet,
                       TilePyramidNotifierPtr tileNotifier, QString archiveFileName,
                       core::ContentTypeInfo mainContentType, ITileNameCodecPtr codec);
    ~ArchiveTileStorage() override;
    ArchiveTileStorage(const ArchiveTileStorage&) = delete;
    ArchiveTileStorage& operator=(const ArchiveTileStorage&) = delete;

    [[nodiscard]] TileStorageTypeAbilities typeAbilities() const override;
    [[nodiscard]] StorageStatePtr state() const override;
    [[nodiscard]] core::Changeable& stateChangeable() override;
    [[nodiscard]] const geo::IProjectionSet& projectionSet() const override;
    [[nodiscard]] TilePyramidNotifier& tileNotifier() override;

    // Ports GetTileFileName (u_TileStorageArchive.pas:217-225): `archiveFile + '/' + codec->tilePath
    // + mainContentType.defaultExt` -- a plain concatenation, NOT run through addExt/AddExt. A UI
    // display string only; nothing is ever opened through it.
    [[nodiscard]] QString tileFileName(geo::PointI xy, quint8 zoom,
                                       const MapVersionInfoPtr& version) const override;

    // Ports GetTileInfo/GetTileInfoEx (u_TileStorageArchive.pas:227-245): unconditionally "nothing".
    [[nodiscard]] core::Result<TileInfo>
    tileInfo(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version, LoadMode mode) const override;
    [[nodiscard]] core::Result<TileInfo>
    tileInfoEx(geo::PointI xy, quint8 zoom, const MapVersionRequest& request, LoadMode mode) const override;

    // Ports DeleteTile (u_TileStorageArchive.pas:247-254): always `False`.
    [[nodiscard]] core::Result<bool> deleteTile(geo::PointI xy, quint8 zoom,
                                                const MapVersionInfoPtr& version) override;

    // Ports SaveTile (u_TileStorageArchive.pas:256-294): appends one entry, no overwrite check at
    // all (`overwrite` is accepted -- ITileStorage requires it -- and ignored, exactly as legacy
    // never reads `AIsOverwrite`); a duplicate tile therefore produces a second, duplicate-named
    // entry, every time, unconditionally. Gated only on being a write-mode instance (legacy:
    // `if (FWriter = nil) and (FWriterSeq = nil) then begin Assert(False); Exit; end` -- Result
    // stays False, not an error); `state()->addAccess` is never consulted, matching legacy (its
    // own `FState` is always nil). A `std::mutex` serialises every call (spec §3.5).
    [[nodiscard]] core::Result<bool> saveTile(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version,
                                              TileDate loadDate, QByteArrayView contentType,
                                              const QByteArray& data, bool overwrite) override;

    // Ports GetListOfTileVersions (u_TileStorageArchive.pas:314-321): always the empty list.
    [[nodiscard]] core::Result<MapVersionList> listVersions(geo::PointI xy, quint8 zoom,
                                                            const MapVersionRequest& request) const override;

    // Ports GetTileRectInfo (u_TileStorageArchive.pas:323-332): always "nothing" (null, not an
    // error).
    [[nodiscard]] core::Result<TileRectInfoPtr> tileRectInfo(geo::RectI rect, quint8 zoom,
                                                             const MapVersionRequest& request,
                                                             std::stop_token stop) const override;

    // Ports ScanTiles + TEnumTileInfoArchive (u_TileStorageArchive.pas:296-312,334-413): a pull
    // enumerator over the archive's entries in file order, never seeking. Gated on being a
    // scan-mode instance (spec §3.5 exclusivity; legacy raises when AllowScan is false -- the
    // port asserts instead, since the two static factories make the wrong-mode case
    // constructor-unreachable from any real caller). Each call wraps the SAME underlying reader
    // (legacy quirk L-45: TEnumTileInfoArchive.Create never Resets FReaderSeq, so a second
    // ScanTiles() call continues, exhausted, from wherever the first left off -- reproduced).
    [[nodiscard]] core::Result<EnumTileInfoPtr> scanTiles(bool ignoreTne,
                                                          bool ignoreMultiVersionTiles) override;

private:
    TileStorageBase base_;
    core::ContentTypeInfo mainContentType_; // display-only for tileFileName(); default when scanning
    ITileNameCodecPtr codec_;

    struct archive* writer_ = nullptr; // non-null <=> write mode (owned)
    Container writerContainer_ = Container::Tar;
    std::mutex writeMutex_; // spec §3.5: a single std::mutex serialises saveTile

    // non-null <=> scan mode. shared_ptr, not a borrowed/owned raw pointer (review F8): the scan
    // enumerator returned by scanTiles() holds its own copy, so archive_read_close/free run only
    // once nothing needs the reader any more, even if this storage is destroyed before the
    // enumerator it handed out finishes draining -- the same shared-ownership idiom
    // RamTileStorage::memCache_ uses for its own long-lived resource.
    std::shared_ptr<struct archive> reader_;
};

} // namespace cg::storage
