// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/archive_tile_storage.h"

#include "cg/core/log.h"
#include "cg/core/platform/native_path.h"
#include "cg/storage/tile_date.h"

#include <archive.h>
#include <archive_entry.h>
#include <utility>

namespace cg::storage {
namespace {

// Legacy TTarWriter.CreateEmpty defaults (Includes/LibTar.pas:776-793): every permission bit set,
// uid/gid 0, empty uname/gname. Reproduced verbatim for every tar entry this backend writes
// (ticket: "per-entry metadata legacy writes ... mode, uid/gid/uname").
constexpr __LA_MODE_T kLegacyTarPerm = 0777;

// F4 (review, major): libarchive's zip writer always writes "made by" = Unix; an entry with no
// perm set therefore extracts as external attributes 0100000 (mode 000, unreadable -- verified
// with `zipinfo -v` / `unzip -d`). Legacy's minizip writes "made by" = NTFS with zero attributes
// (`libminizip.pas:112`), so an unzip tool applies its own default permissions instead -- libarchive
// has no "made-by NTFS" option, so the port sets an explicit regular-file/rw-r--r-- permission
// instead, the closest it can get to legacy's actual *extracted* result (a readable file).
constexpr __LA_MODE_T kZipEntryPerm = 0644;

// F9 (review, minor): legacy clamps a pre-1970 tar mtime to a fixed constant instead of writing the
// real (negative) value: `WriteTarHeader` (Includes/LibTar.pas:539-542) is
// `NullDate := EncodeDate(1970, 1, 1); IF DirRec.DateTime >= NullDate THEN
// Octal(Trunc((DirRec.DateTime - NullDate) * 86400.0), ...) ELSE Octal(Trunc(NullDate * 86400.0), ...)`
// -- i.e. `Trunc(NullDate * 86400.0)` = 2209161600 (year 2040), not 0 or the actual date. Zip's
// `DateTimeToUnix` has no equivalent clamp, so this reproduces for tar only.
constexpr qint64 kLegacyTarPre1970ClampSecs = 2209161600;

[[nodiscard]] QString extLower(const QString& name) {
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 ? name.mid(dot).toLower() : QString();
}

[[nodiscard]] core::Result<archive*>
openWriterArchive(const QString& file, ArchiveTileStorage::Container container, int zipLevel) {
    archive* a = archive_write_new();
    if (a == nullptr) {
        return core::fail(core::ErrorCode::IoError, QStringLiteral("archive_write_new failed"));
    }
    int rc = ARCHIVE_OK;
    if (container == ArchiveTileStorage::Container::Tar) {
        // ustar: the closest standard format to legacy's own ad hoc "ustar"-magic tar (libtar's
        // header layout is the classic pre-POSIX one legacy's TTarHeader record mirrors field for
        // field), readable by legacy's TTarArchive reader (no magic validation, see
        // Includes/LibTar.pas:678) and by GNU tar / bsdtar.
        rc = archive_write_set_format_ustar(a);
    } else {
        rc = archive_write_set_format_zip(a);
        if (rc == ARCHIVE_OK) {
            // ticket's factory takes one `zipLevel` int, not legacy's separate method+level pair
            // (u_ArchiveReadWriteSequentialZip.pas:156-178): 0 -> store (legacy's own default when
            // the Cache Manager's zip config panel is not used), 1-9 -> deflate at that level.
            const QByteArray opts = zipLevel <= 0 ? QByteArrayLiteral("zip:compression=store")
                                                  : QStringLiteral("zip:compression=deflate,zip:"
                                                                   "compression-level=%1")
                                                        .arg(zipLevel)
                                                        .toUtf8();
            rc = archive_write_set_options(a, opts.constData());
        }
    }
    if (rc != ARCHIVE_OK) {
        const QString msg = QString::fromUtf8(archive_error_string(a));
        archive_write_free(a);
        return core::fail(core::ErrorCode::IoError, QStringLiteral("libarchive: %1").arg(msg));
    }
    // T-0335: the narrow libarchive name is read as the ANSI code page on Windows: there the wide name keeps
    // non-ASCII names (both calls exist on every system; kWindowsHost picks one at run time).
    const int opened = core::platform::kWindowsHost
                           ? archive_write_open_filename_w(a, file.toStdWString().c_str())
                           : archive_write_open_filename(a, file.toUtf8().constData());
    if (opened != ARCHIVE_OK) {
        const QString msg = QString::fromUtf8(archive_error_string(a));
        archive_write_free(a);
        return core::fail(core::ErrorCode::IoError,
                          QStringLiteral("cannot open %1 for writing: %2").arg(file, msg));
    }
    return a;
}

[[nodiscard]] core::Result<archive*> openReaderArchive(const QString& file) {
    archive* a = archive_read_new();
    if (a == nullptr) {
        return core::fail(core::ErrorCode::IoError, QStringLiteral("archive_read_new failed"));
    }
    // openForScan carries no Container -- storage.md §2.19's own signature has none, legacy's
    // caller already knows which reader class to build from the archive type it remembers, but
    // this backend's scan path auto-detects instead (libarchive's usual idiom).
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    const int opened = core::platform::kWindowsHost
                           ? archive_read_open_filename_w(a, file.toStdWString().c_str(), 10240)
                           : archive_read_open_filename(a, file.toUtf8().constData(), 10240);
    if (opened != ARCHIVE_OK) {
        const QString msg = QString::fromUtf8(archive_error_string(a));
        archive_read_free(a);
        return core::fail(core::ErrorCode::IoError,
                          QStringLiteral("cannot open %1 for reading: %2").arg(file, msg));
    }
    return a;
}

// F8 (review): wraps a freshly opened reader in a shared_ptr whose deleter does what the old
// destructor-only cleanup did (archive_read_close then archive_read_free). Shared, not unique, so
// a live ArchiveTileEnumerator can hold its own reference and keep the reader alive past this
// storage's destructor.
[[nodiscard]] std::shared_ptr<archive> shareReader(archive* a) {
    return std::shared_ptr<archive>(a, [](archive* p) {
        archive_read_close(p);
        archive_read_free(p);
    });
}

// Ports the write half of TArchiveWriterSequentialTar.Add / TArchiveWriterSequentialZip.Add.
[[nodiscard]] core::Result<void> writeEntry(archive* a, ArchiveTileStorage::Container container,
                                            const QString& name, const QByteArray& data, qint64 mtimeSecs) {
    archive_entry* e = archive_entry_new();
    const QByteArray nameUtf8 = name.toUtf8();
    archive_entry_set_pathname(e, nameUtf8.constData());
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_size(e, data.size());
    if (container == ArchiveTileStorage::Container::Tar && mtimeSecs < 0) {
        mtimeSecs = kLegacyTarPre1970ClampSecs; // F9
    }
    archive_entry_set_mtime(e, mtimeSecs, 0);
    if (container == ArchiveTileStorage::Container::Tar) {
        archive_entry_set_perm(e, kLegacyTarPerm);
        archive_entry_set_uid(e, 0);
        archive_entry_set_gid(e, 0);
        archive_entry_set_uname(e, "");
        archive_entry_set_gname(e, "");
    } else {
        // F4: no uid/gid/uname/gname (still matches legacy's zero-filled mz_zip_file,
        // u_ArchiveReadWriteSequentialZip.pas:211), but DO set a permission -- see kZipEntryPerm.
        archive_entry_set_perm(e, kZipEntryPerm);
    }

    if (archive_write_header(a, e) != ARCHIVE_OK) {
        const QString msg = QString::fromUtf8(archive_error_string(a));
        archive_entry_free(e);
        return core::fail(core::ErrorCode::IoError, QStringLiteral("libarchive: %1").arg(msg));
    }
    if (!data.isEmpty()) {
        const la_ssize_t written = archive_write_data(a, data.constData(), static_cast<size_t>(data.size()));
        if (written != data.size()) {
            const QString msg = QString::fromUtf8(archive_error_string(a));
            archive_entry_free(e);
            return core::fail(core::ErrorCode::IoError, QStringLiteral("short archive write: %1").arg(msg));
        }
    }
    archive_entry_free(e);
    return {};
}

// FEAT-STO-027. Ports TEnumTileInfoArchive (u_TileStorageArchive.pas:336-413).
class ArchiveTileEnumerator final : public IEnumTileInfo {
public:
    ArchiveTileEnumerator(std::shared_ptr<archive> reader, bool ignoreTne, ITileNameCodecPtr codec)
        : reader_(std::move(reader)), ignoreTne_(ignoreTne), codec_(std::move(codec)) {}

    bool next(TileInfo& out, geo::PointI& tile, quint8& zoom) override {
        for (;;) {
            archive_entry* entry = nullptr;
            const int rc = archive_read_next_header(reader_.get(), &entry);
            if (rc == ARCHIVE_EOF) {
                return false; // FReaderSeq.Next returned False -- end of archive
            }
            if (rc < ARCHIVE_OK) {
                // IEnumTileInfo::next has no error channel (coding standard: exceptions/errors are
                // for programmer errors, not "one bad entry mid-scan") -- log and stop, matching
                // this module's "corrupt record" convention for a fatal condition (a non-fatal
                // ARCHIVE_WARN still yielded a usable header, so fall through instead).
                qCWarning(core::logStorage)
                    << "scanTiles: archive read error, stopping:" << archive_error_string(reader_.get());
                if (rc == ARCHIVE_FATAL) {
                    return false;
                }
            }

            QString name = QString::fromUtf8(archive_entry_pathname(entry));
            // F1 (review, major): a legacy-written tar uses PathDelim = '\' in entry names
            // (u_TileFileNameOsmAnd.pas:67 and every folder-name generator; TTarWriter.AddStream
            // never calls ConvertFilename). Legacy's own reader accepts both separators by
            // rewriting '/' to PathDelim before parsing (u_ArchiveReadWriteSequentialTar.pas:
            // 219-221); this port's codec regexes are '/'-only, so normalize the other way instead.
            name.replace(QLatin1Char('\\'), QLatin1Char('/'));
            geo::PointI xy;
            quint8 z = 0;
            if (!codec_->parse(name, xy, z)) {
                continue; // u_TileStorageArchive.pas:409-411: Assert(False, ...) -- release: skip
            }

            const QString ext = extLower(name);
            // F3 (review, major): legacy treats the naive-local load date as UTC on both write and
            // read, for tar mtime (`Includes/LibTar.pas` WriteTarHeader/FindNext) and zip dates
            // (`u_ArchiveReadWriteSequentialZip.pas` Add/Next, Delphi's default AInputIsUTC=True) --
            // i.e. toLegacyUnixSeconds/fromLegacyUnixSeconds (tile_date.h), not the POSIX pair.
            const qint64 mtimeSecs = archive_entry_mtime(entry);
            if (ext == QLatin1String(".tne")) {
                if (ignoreTne_) {
                    continue;
                }
                out = TileInfo{};
                out.type = TileInfoType::TneExists;
                out.loadDate = fromLegacyUnixSeconds(mtimeSecs);
                out.size = 0;
                tile = xy;
                zoom = z;
                return true;
            }

            const la_int64_t size = archive_entry_size(entry);
            if (size <= 0) {
                // u_TileStorageArchive.pas:400-407: `if ATileInfo.FData <> nil then ... else
                // Assert(False, 'Tile data is empty!')` -- release build: silently skip (L-45).
                continue;
            }
            QByteArray data;
            data.resize(static_cast<qsizetype>(size));
            const la_ssize_t got = archive_read_data(reader_.get(), data.data(), static_cast<size_t>(size));
            if (got != size) {
                qCWarning(core::logStorage) << "scanTiles: short read for" << name << "skipping";
                continue;
            }

            out = TileInfo{};
            out.type = TileInfoType::Exists;
            out.loadDate = fromLegacyUnixSeconds(mtimeSecs);
            out.data = data;
            out.size = static_cast<quint32>(size);
            const core::ContentTypeInfo* info = core::ContentTypeRegistry::standard().infoByExt(ext.toUtf8());
            out.contentType = info != nullptr ? info->type : QByteArray();
            tile = xy;
            zoom = z;
            return true;
        }
    }

private:
    std::shared_ptr<archive> reader_; // shared with ArchiveTileStorage::reader_ (F8, see header)
    bool ignoreTne_;
    ITileNameCodecPtr codec_;
};

} // namespace

ArchiveTileStorage::ArchiveTileStorage(TileStorageTypeAbilities typeAbilities,
                                       TileStorageAbilities resolvedAbilities,
                                       MapVersionFactoryPtr mapVersionFactory,
                                       geo::ProjectionSetPtr projectionSet,
                                       TilePyramidNotifierPtr tileNotifier, QString archiveFileName,
                                       core::ContentTypeInfo mainContentType, ITileNameCodecPtr codec)
    : base_(typeAbilities, resolvedAbilities, std::move(mapVersionFactory), std::move(projectionSet),
            std::move(tileNotifier), std::move(archiveFileName)),
      mainContentType_(std::move(mainContentType)), codec_(std::move(codec)) {
    Q_ASSERT(codec_ != nullptr);
    Q_ASSERT(!base_.storagePath().isEmpty()); // u_TileStorageArchive.pas:173
}

ArchiveTileStorage::~ArchiveTileStorage() {
    if (writer_ != nullptr) {
        // Ports TArchiveWriterSequentialTar.Destroy / TArchiveWriterSequentialZip.Destroy
        // (FTar.Finalize via FreeAndNil / mz_zip_writer_close).
        archive_write_close(writer_);
        archive_write_free(writer_);
    }
    // reader_ is a shared_ptr (F8): its deleter (shareReader()) runs archive_read_close/free once
    // the last owner -- this storage or a still-draining scan enumerator -- releases it.
}

core::Result<std::shared_ptr<ArchiveTileStorage>>
ArchiveTileStorage::openForWrite(QString file, Container container, int zipLevel, ITileNameCodecPtr codec,
                                 core::ContentTypeInfo mainContentType, geo::ProjectionSetPtr projectionSet,
                                 MapVersionFactoryPtr mapVersionFactory,
                                 TilePyramidNotifierPtr tileNotifier) {
    Q_ASSERT(!file.isEmpty());
    Q_ASSERT(codec != nullptr);
    Q_ASSERT(tileNotifier != nullptr);

    core::Result<archive*> opened = openWriterArchive(file, container, zipLevel);
    if (!opened.has_value()) {
        return std::unexpected(opened.error());
    }

    TileStorageTypeAbilities typeAbilities = tileStorageTypeAbilities(TileStorageKind::Archive);
    // u_TileStorageTypeArchive.pas:82-107 -- ATileStorageAbilities is caller-supplied; the Cache
    // Manager builds a write-only one for this path (frm_CacheManager.pas:390: a writer is only
    // built `if AStorageAbilities.AllowAdd`).
    typeAbilities.base = TileStorageAbilities{false, false, true, false, false};

    auto storage = std::make_shared<ArchiveTileStorage>(
        typeAbilities, typeAbilities.base, std::move(mapVersionFactory), std::move(projectionSet),
        std::move(tileNotifier), std::move(file), std::move(mainContentType), std::move(codec));
    storage->writer_ = *opened;
    storage->writerContainer_ = container;
    return storage;
}

core::Result<std::shared_ptr<ArchiveTileStorage>>
ArchiveTileStorage::openForScan(QString file, ITileNameCodecPtr codec, geo::ProjectionSetPtr projectionSet,
                                MapVersionFactoryPtr mapVersionFactory, TilePyramidNotifierPtr tileNotifier) {
    Q_ASSERT(!file.isEmpty());
    Q_ASSERT(codec != nullptr);
    Q_ASSERT(tileNotifier != nullptr);

    core::Result<archive*> opened = openReaderArchive(file);
    if (!opened.has_value()) {
        return std::unexpected(opened.error());
    }

    TileStorageTypeAbilities typeAbilities = tileStorageTypeAbilities(TileStorageKind::Archive);
    // frm_CacheManager.pas:382-383: a reader is only built `if AStorageAbilities.AllowScan`.
    typeAbilities.base = TileStorageAbilities{false, true, false, false, false};

    auto storage = std::make_shared<ArchiveTileStorage>(
        typeAbilities, typeAbilities.base, std::move(mapVersionFactory), std::move(projectionSet),
        std::move(tileNotifier), std::move(file), core::ContentTypeInfo{}, std::move(codec));
    storage->reader_ = shareReader(*opened); // F8
    return storage;
}

TileStorageTypeAbilities ArchiveTileStorage::typeAbilities() const {
    return base_.typeAbilities();
}

StorageStatePtr ArchiveTileStorage::state() const {
    return base_.state().get();
}

core::Changeable& ArchiveTileStorage::stateChangeable() {
    return base_.state();
}

const geo::IProjectionSet& ArchiveTileStorage::projectionSet() const {
    return base_.projectionSet();
}

TilePyramidNotifier& ArchiveTileStorage::tileNotifier() {
    return base_.tileNotifier();
}

QString ArchiveTileStorage::tileFileName(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr&) const {
    // Ports GetTileFileName (u_TileStorageArchive.pas:217-225): plain concatenation, no addExt.
    return base_.storagePath() + QLatin1Char('/') + codec_->tilePath(xy, zoom) +
           QString::fromLatin1(mainContentType_.defaultExt);
}

core::Result<TileInfo> ArchiveTileStorage::tileInfo(geo::PointI, quint8, const MapVersionInfoPtr&,
                                                    LoadMode) const {
    return TileInfo{}; // u_TileStorageArchive.pas:227-235: Result := nil, unconditionally (Unknown).
}

core::Result<TileInfo> ArchiveTileStorage::tileInfoEx(geo::PointI, quint8, const MapVersionRequest&,
                                                      LoadMode) const {
    return TileInfo{}; // u_TileStorageArchive.pas:237-245: Result := nil, unconditionally (Unknown).
}

core::Result<bool> ArchiveTileStorage::deleteTile(geo::PointI, quint8, const MapVersionInfoPtr&) {
    return false; // u_TileStorageArchive.pas:247-254: Result := False, unconditionally.
}

core::Result<bool> ArchiveTileStorage::saveTile(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr&,
                                                TileDate loadDate, QByteArrayView contentType,
                                                const QByteArray& data, bool /*overwrite*/) {
    Q_ASSERT(writer_ != nullptr); // spec §3.5 exclusivity -- construction-guaranteed
    if (writer_ == nullptr) {
        return false; // u_TileStorageArchive.pas:270-273: Assert(False); Exit -- Result stays False
    }

    // "Empty contentType OR null data => save a TNE" (tile_storage.h::saveTile), ports
    // `Assigned(AContentType) and Assigned(AData)` (u_TileStorageArchive.pas:276).
    const bool wantsTne = contentType.isEmpty() || data.isNull();

    // F2 (review, Tech Lead ruling 2026-09-28): entry names use `codec_->tilePath`'s '/' separator
    // for BOTH containers, not legacy's `\` (`u_TileFileNameOsmAnd.pas:67` and every generator;
    // `TTarWriter.AddStream` never calls `ConvertFilename`). Deliberate deviation, not a bug: a `\`
    // entry name extracts on every Linux tool as a flat file literally named e.g. `13\1234\678.jpg`
    // -- the path separator is a platform property, and legacy's own `\` result is unusable on this
    // platform. See docs/parity/deviations.md D-34 (tar) for the read-compatibility matrix (port
    // reads both, after F1; legacy reads both, `u_ArchiveReadWriteSequentialTar.pas:219-221`).
    QString entryName;
    QByteArray entryData;
    if (!wantsTne) {
        // supportsMixedContentTypes == true for this backend (matches legacy's constructor call,
        // u_TileStorageTypeArchive.pas:99): the entry extension comes from THIS call's own content
        // type, not the storage's fixed mainContentType_ -- ports `AContentType.GetDefaultExt`,
        // the argument that wins after legacy's second `AddExt` call (`u_TileStorageArchive.pas:
        // 278-279`). That second call is not a bug: legacy's `AddExt` is `ChangeFileExt`-style (it
        // replaces the trailing extension, not append-only), and no codec's base name contains a
        // dot before it runs, so legacy also lands on a single extension (`…/678.jpg.tile`, not
        // `…/678.jpg.jpg.tile`) -- see D-7 in storage.md §8 (corrected 2026-09-28, review F5). The
        // port's one `addExt` call already produces that same string.
        const core::ContentTypeInfo* info = core::ContentTypeRegistry::standard().infoByType(contentType);
        if (info == nullptr) {
            return core::fail(core::ErrorCode::Unsupported,
                              QStringLiteral("saveTile: unregistered content type"));
        }
        entryName = codec_->addExt(codec_->tilePath(xy, zoom), QString::fromLatin1(info->defaultExt));
        entryData = data;
    } else {
        entryName = codec_->addExt(codec_->tilePath(xy, zoom), QStringLiteral(".tne"));
        // entryData stays a null/empty QByteArray -- a zero-byte entry (ticket Scope).
    }

    std::lock_guard<std::mutex> lock(writeMutex_); // spec §3.5: a single std::mutex serialises saveTile
    core::Result<void> written =
        writeEntry(writer_, writerContainer_, entryName, entryData, toLegacyUnixSeconds(loadDate)); // F3
    if (!written.has_value()) {
        return std::unexpected(written.error());
    }
    // No notifyTileUpdate() call: legacy's FTileNotifier is always nil for this backend
    // (u_TileStorageArchive.pas:189) and TTileStorageArchive.SaveTile never fires it.
    return true;
}

core::Result<MapVersionList> ArchiveTileStorage::listVersions(geo::PointI, quint8,
                                                              const MapVersionRequest&) const {
    return MapVersionList{}; // u_TileStorageArchive.pas:314-321: Result := nil, unconditionally.
}

core::Result<TileRectInfoPtr> ArchiveTileStorage::tileRectInfo(geo::RectI, quint8, const MapVersionRequest&,
                                                               std::stop_token) const {
    return TileRectInfoPtr{}; // u_TileStorageArchive.pas:323-332: Result := nil, unconditionally.
}

core::Result<EnumTileInfoPtr> ArchiveTileStorage::scanTiles(bool ignoreTne,
                                                            bool /*ignoreMultiVersionTiles*/) {
    Q_ASSERT(reader_ != nullptr); // spec §3.5 exclusivity -- construction-guaranteed
    if (reader_ == nullptr) {
        // Legacy raises here (u_TileStorageArchive.pas:296-311, `AllowScan` false); this instance
        // can only be write-mode by construction (openForWrite), so this is unreachable from any
        // real caller -- kept as a Result rather than a throw, matching this module's "no
        // exceptions except programmer-error setup" convention.
        return core::fail(core::ErrorCode::Unsupported,
                          QStringLiteral("scanTiles: this instance was opened for writing"));
    }
    return std::make_unique<ArchiveTileEnumerator>(reader_, ignoreTne, codec_);
}

} // namespace cg::storage
