// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/file_system_tile_storage.h"

#include "cg/core/log.h"
#include "cg/core/platform/file_times.h"
#include "cg/storage/content_type_compat.h"
#include "cg/storage/tile_date.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace cg::storage {
namespace {

// TFileSystemTools.GetTileNotFoundExt. A function, not a static QString: a QString at namespace
// scope has static storage duration with a throwing dynamic initializer (bugprone-throwing-
// static-initialization); QStringLiteral itself is cheap to re-evaluate per call.
[[nodiscard]] QString tneExt() {
    return QStringLiteral(".tne");
}

// Ports TFileSystemTools.CreateDirIfNotExists: create the parent directory of `filePath` if it
// does not exist yet.
[[nodiscard]] core::Result<void> ensureParentDir(const QString& filePath) {
    const QString dir = QFileInfo(filePath).path();
    if (!QDir().mkpath(dir)) {
        return core::fail(core::ErrorCode::IoError, QStringLiteral("cannot create directory %1").arg(dir));
    }
    return {};
}

// FEAT-STO-035. Ports TEnumTileInfoByFileIterator (u_EnumTileInfoByFileIterator.pas) plus the
// three legacy iterator classes ScanTiles composes (u_FoldersIteratorRecursiveByLevels,
// u_FileNameIteratorFolderWithSubfolders, u_FileNameIteratorInFolderByMaskList): a breadth-first
// walk of the folder tree (a single global FIFO queue, seeded with the root, each dequeue
// enqueueing its own children before its own files are yielded -- legacy's
// TFoldersIteratorRecursiveByLevels.Next/ProcessAddSubFolders), capped at folder depth
// cMaxFolderDepth = 10 (a folder at depth 10 still yields its files; its children, depth 11, are
// never discovered). Within one folder, tile-extension files are yielded before ".tne" files
// (legacy's mask list order, u_TileStorageFileSystem.pas:574-577) -- not interleaved by directory
// order.
//
// D-27 (deviations.md, F-1 review fix): folder/file order WITHIN one mask is sorted by name,
// case-insensitively (QDir::Name | QDir::IgnoreCase), which reproduces NTFS's $UpCase-folded
// collation order for every name the nine layouts produce -- on other Windows file systems (FAT/
// exFAT, network shares) legacy's order was file-system dependent and is not reproduced. The
// *structure* of the walk (breadth-first by level, tile mask before .tne mask per folder, depth
// cap) is the ported, observable behaviour.
class FileSystemTileEnumerator final : public IEnumTileInfo {
public:
    FileSystemTileEnumerator(QString storagePath, QString tileFileExt, bool ignoreTne,
                             ITileNameCodecPtr codec, QByteArray contentType, const TileStorageBase& base,
                             core::Mutex& fsMutex)
        : storagePath_(std::move(storagePath)), tileFileExt_(std::move(tileFileExt)), ignoreTne_(ignoreTne),
          codec_(std::move(codec)), contentType_(std::move(contentType)), base_(base), fsMutex_(fsMutex) {
        pending_.emplace_back(QString(), 0); // root, depth 0
    }

    bool next(TileInfo& out, geo::PointI& tile, quint8& zoom) override {
        for (;;) {
            if (currentIndex_ >= currentEntries_.size()) {
                if (!advanceFolder()) {
                    return false;
                }
                continue;
            }
            const Entry entry = currentEntries_[currentIndex_++];

            // F-3 (review): u_EnumTileInfoByFileIterator.pas:99 skips any candidate whose
            // root-relative path (the same string handed to the parser, not just the file name)
            // contains a non-ASCII character, before it ever reaches the parser.
            bool hasNonAscii = false;
            for (const QChar ch : entry.relPath) {
                if (ch.unicode() > 0x7F) {
                    hasNonAscii = true;
                    break;
                }
            }
            if (hasNonAscii) {
                continue;
            }

            geo::PointI xy;
            quint8 z = 0;
            // T-0021F1 review's "confirm ITileNameCodec covers parsing" open question: it does --
            // parse() takes the whole storagePath-relative path with any extension and any number
            // of leading directories, exactly what legacy's separate ATileFileNameParser consumed.
            // No separate parser is added.
            if (!codec_->parse(entry.relPath, xy, z)) {
                continue; // foreign file: extension matched the mask, the name didn't parse
            }
            // u_EnumTileInfoByFileIterator.pas:102 rechecks ScanAccess for every candidate, not
            // just once at ScanTiles() construction time.
            if (!base_.state().get()->scanAccess) {
                continue;
            }

            TileInfo info;
            {
                // Per-item lock -- spec §3.2's one required refinement over the coarse backend
                // lock, matching u_EnumTileInfoByFileIterator.pas:103-121 (FLock.BeginRead inside
                // Next(), not held across the whole walk).
                core::ReadLock lock(fsMutex_);
                const QString absPath = storagePath_ + entry.relPath;
                core::Result<qint64> mtime = core::platform::fileMTime(absPath);
                if (!mtime.has_value()) {
                    // Vanished between listing and stat -- GetFileAttributesEx would fail the same
                    // way, legacy's UpdateTileInfoByFile yields titNotExists, and the caller's
                    // `if ATileInfo.FInfoType <> titNotExists` loops to the next candidate instead
                    // of yielding it.
                    continue;
                }
                info.loadDate = fromPosixSeconds(*mtime);
                // Set unconditionally for both branches, matching
                // u_EnumTileInfoByFileIterator.pas:114 (`ATileInfo.FContentType := FMainContentType`
                // runs before the titNotExists check) -- unlike tileInfo()/probeLocked, which only
                // sets it on the Exists branch (a different legacy call site, GetTileInfoByPath).
                info.contentType = contentType_;
                if (entry.isTne) {
                    info.type = TileInfoType::TneExists;
                    info.size = 0;
                } else {
                    info.type = TileInfoType::Exists;
                    QFile file(absPath);
                    // D-27 (deviations.md): legacy's TMemoryStream.LoadFromFile raises uncaught on
                    // a read failure here; IEnumTileInfo::next() has no error channel (coding
                    // standard: exceptions are for programmer errors only), so an unreadable tile
                    // is skipped instead of aborting the whole scan -- logged, matching this
                    // module's BerkeleyDB-corrupt-record precedent ("the enumerator logs, skips and
                    // continues").
                    if (!file.open(QIODevice::ReadOnly)) {
                        qCWarning(core::logStorage) << "scanTiles: cannot open" << absPath
                                                    << "for reading, skipping:" << file.errorString();
                        continue;
                    }
                    info.data = file.readAll();
                    info.size = static_cast<quint32>(info.data.size());
                }
            }
            out = std::move(info);
            tile = xy;
            zoom = z;
            return true;
        }
    }

private:
    struct Entry {
        QString relPath; // storagePath-relative, '/'-separated
        bool isTne = false;
    };

    static constexpr int kMaxFolderDepth = 10; // u_TileStorageFileSystem.pas cMaxFolderDepth

    // Ports TFoldersIteratorRecursiveByLevels.Next + ProcessAddSubFolders, and
    // TFileNameIteratorInFolderByMaskList's per-folder mask order, fused into one step: dequeue
    // the next folder with pending files, en-queue its own subfolders (if under the depth cap),
    // then fill currentEntries_ with that folder's tile-mask files followed by its .tne-mask files.
    bool advanceFolder() {
        while (!pending_.empty()) {
            const auto [folderRel, depth] = pending_.front();
            pending_.pop_front();

            const QString absFolder = storagePath_ + folderRel;
            QDir dir(absFolder);
            if (!dir.exists()) {
                continue; // unreadable/missing folder -- skip silently, legacy's FindFirstFileEx
                          // failing (INVALID_HANDLE_VALUE) does the same.
            }

            if (depth < kMaxFolderDepth) {
                // F-2/F-1 (review): QDir::Hidden lists dot-prefixed and hidden-attribute entries too
                // -- legacy's FindFirstFileEx enumerates every entry regardless of attributes ('.'/
                // '..' are still excluded by NoDotAndDotDot). QDir::IgnoreCase reproduces NTFS's
                // $UpCase-folded collation order for every name the nine codecs produce.
                const QFileInfoList subdirs = dir.entryInfoList(
                    QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name | QDir::IgnoreCase);
                for (const QFileInfo& fi : subdirs) {
                    pending_.emplace_back(folderRel + fi.fileName() + QLatin1Char('/'), depth + 1);
                }
            }

            currentEntries_.clear();
            currentIndex_ = 0;
            const QStringList tileNames =
                dir.entryList({QStringLiteral("*") + tileFileExt_}, QDir::Files | QDir::Hidden,
                              QDir::Name | QDir::IgnoreCase);
            for (const QString& name : tileNames) {
                currentEntries_.push_back({folderRel + name, false});
            }
            if (!ignoreTne_) {
                const QStringList tneNames = dir.entryList(
                    {QStringLiteral("*.tne")}, QDir::Files | QDir::Hidden, QDir::Name | QDir::IgnoreCase);
                for (const QString& name : tneNames) {
                    currentEntries_.push_back({folderRel + name, true});
                }
            }
            if (!currentEntries_.empty()) {
                return true;
            }
            // Folder had no matching files (or none survived ScanAccess/parse in a previous call --
            // not possible here since filtering happens in next(), not advanceFolder()); keep
            // dequeuing.
        }
        return false;
    }

    QString storagePath_;
    QString tileFileExt_;
    bool ignoreTne_;
    ITileNameCodecPtr codec_;
    QByteArray contentType_;
    const TileStorageBase& base_; // borrows the owning storage; caller must keep it alive
    core::Mutex& fsMutex_;        // while draining this enumerator (ordinary iterator lifetime)

    std::deque<std::pair<QString, int>> pending_;
    std::vector<Entry> currentEntries_;
    std::size_t currentIndex_ = 0;
};

} // namespace

FileSystemTileStorage::FileSystemTileStorage(TileStorageTypeAbilities typeAbilities,
                                             TileStorageAbilities resolvedAbilities,
                                             MapVersionFactoryPtr mapVersionFactory,
                                             geo::ProjectionSetPtr projectionSet,
                                             TilePyramidNotifierPtr tileNotifier, QString storagePath,
                                             core::ContentTypeInfo mainContentType, ITileNameCodecPtr codec,
                                             std::shared_ptr<TileInfoMemCache> memCache)
    : base_(typeAbilities, resolvedAbilities, std::move(mapVersionFactory), std::move(projectionSet),
            std::move(tileNotifier), std::move(storagePath)),
      mainContentType_(std::move(mainContentType)),
      tileFileExt_(QString::fromLatin1(mainContentType_.defaultExt)), codec_(std::move(codec)),
      memCache_(std::move(memCache)) {
    Q_ASSERT(codec_ != nullptr);
    Q_ASSERT(!base_.storagePath().isEmpty());
}

TileStorageTypeAbilities FileSystemTileStorage::typeAbilities() const {
    return base_.typeAbilities();
}

StorageStatePtr FileSystemTileStorage::state() const {
    return base_.state().get();
}

core::Changeable& FileSystemTileStorage::stateChangeable() {
    return base_.state();
}

const geo::IProjectionSet& FileSystemTileStorage::projectionSet() const {
    return base_.projectionSet();
}

TilePyramidNotifier& FileSystemTileStorage::tileNotifier() {
    return base_.tileNotifier();
}

QString FileSystemTileStorage::basePath(geo::PointI xy, quint8 zoom) const {
    return codec_->tilePath(xy, zoom);
}

QString FileSystemTileStorage::tilePathFor(const QString& base) const {
    return base_.storagePath() + codec_->addExt(base, tileFileExt_);
}

QString FileSystemTileStorage::tnePathFor(const QString& base) const {
    return base_.storagePath() + codec_->addExt(base, tneExt());
}

QString FileSystemTileStorage::tileFileName(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr&) const {
    // Ports GetTileFileName -- ignores the version, folder caches carry no version in the path.
    return tilePathFor(basePath(xy, zoom));
}

core::Result<TileInfo> FileSystemTileStorage::probeLocked(const QString& tilePath, const QString& tnePath,
                                                          bool withData) const {
    // Ports TFileSystemTools.UpdateTileInfoByFile(False, ...) then, on a miss,
    // UpdateTileInfoByFile(True, ...) on the .tne path -- storage.md §2.13.2 steps 3-5.
    const QFileInfo tileFi(tilePath);
    if (tileFi.isFile()) {
        core::Result<qint64> mtime = core::platform::fileMTime(tilePath);
        if (mtime.has_value()) {
            TileInfo info;
            info.type = TileInfoType::Exists;
            info.loadDate = fromPosixSeconds(*mtime);
            info.contentType = mainContentType_.type;
            if (withData) {
                QFile file(tilePath);
                if (!file.open(QIODevice::ReadOnly)) {
                    return core::fail(core::ErrorCode::IoError,
                                      QStringLiteral("cannot open %1 for reading").arg(tilePath));
                }
                info.data = file.readAll();
                info.size = static_cast<quint32>(info.data.size());
            } else {
                info.size = static_cast<quint32>(tileFi.size());
            }
            return info;
        }
        // F-4 (review): vanished between isFile() and fileMTime() -- legacy's GetFileAttributesEx
        // failing here yields titNotExists, and UpdateTileInfoByFile falls through to the TNE probe
        // below instead of raising. Fall through instead of propagating the error, so a stat race
        // never fails the caller's whole rect/tile.
    }

    const QFileInfo tneFi(tnePath);
    if (tneFi.isFile()) {
        core::Result<qint64> mtime = core::platform::fileMTime(tnePath);
        if (mtime.has_value()) {
            TileInfo info;
            info.type = TileInfoType::TneExists;
            info.loadDate = fromPosixSeconds(*mtime);
            info.size = 0;
            return info;
        }
        // Same fallback as above, now for the .tne path -- falls through to NotExists.
    }

    TileInfo info;
    info.type = TileInfoType::NotExists;
    return info;
}

core::Result<TileInfo> FileSystemTileStorage::tileInfo(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr&,
                                                       LoadMode mode) const {
    // storage.md §2.13.2: mem-cache lookup happens BEFORE the read-access check, and even a
    // negative result gets cached in step 6 below -- reproduce the order exactly.
    if (memCache_) {
        std::optional<TileInfo> cached = memCache_->get(xy, zoom, nullptr, mode, true);
        if (cached.has_value()) {
            return *cached;
        }
    }
    if (!base_.state().get()->readAccess) {
        // Legacy leaves Result = nil here (u_TileStorageFileSystem.pas:409, :416): a default
        // TileInfo (type Unknown) is the port's nil, and it is not put into the mem-cache.
        return TileInfo{};
    }

    const QString base = basePath(xy, zoom);
    const QString tilePath = tilePathFor(base);
    const QString tnePath = tnePathFor(base);

    core::Result<TileInfo> result;
    {
        core::ReadLock lock(fsMutex_);
        result = probeLocked(tilePath, tnePath, mode == LoadMode::WithData);
    }
    if (result.has_value() && memCache_) {
        memCache_->add(xy, zoom, nullptr, *result);
    }
    return result;
}

core::Result<TileInfo> FileSystemTileStorage::tileInfoEx(geo::PointI xy, quint8 zoom,
                                                         const MapVersionRequest&, LoadMode mode) const {
    // Ports GetTileInfoEx: FolderSimple ignores versions, forwards with a null version.
    return tileInfo(xy, zoom, nullptr, mode);
}

core::Result<bool> FileSystemTileStorage::deleteTile(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr&) {
    if (!base_.state().get()->deleteAccess) {
        return false;
    }

    const QString base = basePath(xy, zoom);
    const QString tilePath = tilePathFor(base);
    const QString tnePath = tnePathFor(base);

    core::WriteLock lock(fsMutex_);
    // Ports `Result := DeleteFile(VFileName) or DeleteFile(VTneName)` (u_TileStorageFileSystem.pas:
    // 204). Delphi's default short-circuit `or` evaluation means the .tne is left untouched once
    // the tile file was deleted -- `||` reproduces that exactly (T-0021F1 review finding 2).
    const bool deleted = QFile::remove(tilePath) || QFile::remove(tnePath);
    lock.unlock();

    if (deleted) {
        if (memCache_) {
            TileInfo notExists;
            notExists.type = TileInfoType::NotExists;
            memCache_->add(xy, zoom, nullptr, notExists);
        }
        base_.notifyTileUpdate(xy, zoom, nullptr); // outside the lock -- spec §3.1 rule 3
    }
    return deleted;
}

core::Result<bool> FileSystemTileStorage::saveTile(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr&,
                                                   TileDate loadDate, QByteArrayView contentType,
                                                   const QByteArray& data, bool overwrite) {
    if (!base_.state().get()->addAccess) {
        return false;
    }
    if (!contentType.isEmpty()) {
        // Ports FMainContentType.CheckOtherForSaveCompatible (u_ContentTypeInfo.pas:123-157) via
        // the shared helper (content_type_compat.h) -- also used by RamTileStorage::saveTile
        // (T-0021F1 review "For F2/F3" note: reuse, don't re-derive). Legacy raises here
        // (u_TileStorageFileSystem.pas:SaveTile); the port turns it into a Result, keeping the
        // port's existing Unsupported error code (T-0021F1 review finding 1).
        if (!contentTypeCompatibleForSave(mainContentType_, contentType)) {
            return core::fail(core::ErrorCode::Unsupported,
                              QStringLiteral("Bad content type for this tile storage"));
        }
    }

    const QString base = basePath(xy, zoom);
    const QString tilePath = tilePathFor(base);
    const QString tnePath = tnePathFor(base);
    // "Empty contentType OR null data => save a TNE" (tile_storage.h::saveTile).
    const bool wantsTne = contentType.isEmpty() || data.isNull();

    bool saved = false;
    TileInfo newInfo;

    core::WriteLock lock(fsMutex_);

    if (!overwrite) {
        // Ports SaveTile's own reentrant `GetTileInfo(AXY, AZoom, nil, gtimAsIs)` call. Legacy's
        // FFsLock is recursive so that call re-enters cleanly; fsMutex_ is not (see probeLocked's
        // doc), so the mem-cache is consulted directly and the filesystem fallback calls
        // probeLocked() under the WriteLock already held here instead of tileInfo()'s own
        // ReadLock.
        std::optional<TileInfo> existing =
            memCache_ ? memCache_->get(xy, zoom, nullptr, LoadMode::AsIs, true) : std::nullopt;
        TileInfo existingInfo;
        bool haveExisting = false;
        if (existing.has_value()) {
            existingInfo = *existing;
            haveExisting = true;
        } else if (base_.state().get()->readAccess) {
            // Legacy's GetTileInfo only probes the filesystem -- and caches the result -- when
            // ReadAccess is true (u_TileStorageFileSystem.pas:461 -> :407-426); with ReadAccess
            // false and nothing cached, GetTileInfo returns nil and the save proceeds as an
            // overwrite (T-0021F1 review finding 4a).
            core::Result<TileInfo> probed = probeLocked(tilePath, tnePath, false);
            if (!probed.has_value()) {
                return std::unexpected(probed.error());
            }
            existingInfo = *probed;
            haveExisting = true;
            if (memCache_) {
                // Legacy caches the probed result too (:422-424) -- finding 4b.
                memCache_->add(xy, zoom, nullptr, existingInfo);
            }
        }
        if (haveExisting && (existingInfo.isExists() || existingInfo.isExistsTne())) {
            return false;
        }
    }

    if (!wantsTne) {
        core::Result<void> dirOk = ensureParentDir(tilePath);
        if (!dirOk.has_value()) {
            return std::unexpected(dirOk.error());
        }
        QFile file(tilePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return core::fail(core::ErrorCode::IoError,
                              QStringLiteral("cannot open %1 for writing").arg(tilePath));
        }
        if (file.write(data) != data.size()) {
            return core::fail(core::ErrorCode::IoError, QStringLiteral("short write to %1").arg(tilePath));
        }
        file.close();
        // Legacy ignores FileSetDate's return value (u_TileStorageFileSystem.pas:483 ->
        // u_FileSystemTools.pas:165-176): a failed setFileMTime must NOT fail the save (T-0021F1
        // review finding 3) -- the tile bytes are already on disk, so the sibling is still
        // unlinked and the mem-cache/notification still fire below.
        [[maybe_unused]] core::Result<void> dateOk =
            core::platform::setFileMTime(tilePath, toPosixSeconds(loadDate));
        QFile::remove(tnePath); // unconditional, matching legacy -- a no-op when no .tne exists

        saved = true;
        newInfo.type = TileInfoType::Exists;
        newInfo.loadDate = loadDate;
        newInfo.size = static_cast<quint32>(data.size());
        newInfo.data = data; // mem-cache is populated WITH the tile bytes, ports TTileInfoBasicExistsWithTile
        newInfo.contentType = mainContentType_.type;
    } else if (!QFileInfo::exists(tnePath)) {
        // Quirk kept exactly as legacy: writing a TNE NEVER overwrites an existing TNE, even when
        // `overwrite` is true -- the `AIsOverwrite` flag is never consulted in this branch.
        core::Result<void> dirOk = ensureParentDir(tnePath);
        if (!dirOk.has_value()) {
            return std::unexpected(dirOk.error());
        }
        QFile file(tnePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return core::fail(core::ErrorCode::IoError,
                              QStringLiteral("cannot open %1 for writing").arg(tnePath));
        }
        file.close();
        // Legacy ignores FileSetDate's return value (u_TileStorageFileSystem.pas:526 ->
        // u_FileSystemTools.pas:165-176): a failed setFileMTime must NOT fail the save (T-0021F1
        // review finding 3).
        [[maybe_unused]] core::Result<void> dateOk =
            core::platform::setFileMTime(tnePath, toPosixSeconds(loadDate));
        QFile::remove(tilePath); // unconditional, matching legacy

        saved = true;
        newInfo.type = TileInfoType::TneExists;
        newInfo.loadDate = loadDate;
        newInfo.size = 0;
    }

    lock.unlock();

    if (saved) {
        if (memCache_) {
            memCache_->add(xy, zoom, nullptr, newInfo);
        }
        base_.notifyTileUpdate(xy, zoom, nullptr); // outside the lock -- spec §3.1 rule 3
    }
    return saved;
}

core::Result<MapVersionList> FileSystemTileStorage::listVersions(geo::PointI, quint8,
                                                                 const MapVersionRequest&) const {
    return MapVersionList{}; // FEAT-STO-032 default (u_TileStorageAbstract.pas): empty list
}

core::Result<TileRectInfoPtr> FileSystemTileStorage::tileRectInfo(geo::RectI rect, quint8 zoom,
                                                                  const MapVersionRequest&,
                                                                  std::stop_token stop) const {
    // storage.md §2.13.2 / u_TileStorageFileSystem.pas:GetTileRectInfo:319 -- gated on ReadAccess,
    // not ScanAccess. Legacy leaves Result = nil (not an error) when access is denied; the port
    // mirrors that with a successful null pointer, the same "nothing to give you" convention
    // scanTiles uses below for a denied ScanAccess.
    if (!base_.state().get()->readAccess) {
        return TileRectInfoPtr{};
    }

    geo::RectI r = rect;
    base_.projectionSet().zoom(zoom)->validateTileRect(r);
    const int width = r.width();
    const int height = r.height();
    if (width <= 0 || height <= 0) {
        // Degenerate rect after clamping (e.g. entirely outside the pyramid at this zoom). Legacy's
        // `(VCount.X > 0) and (VCount.Y > 0)` guard also just leaves Result = nil here, same as the
        // ReadAccess-false case above, and the same as the 2048 cap below.
        return TileRectInfoPtr{};
    }
    if (width > 2048 || height > 2048) {
        // Legacy returns nil for a rect over the 2048x2048 cap (u_TileStorageFileSystem.pas, the
        // `(VCount.X <= 2048) and (VCount.Y <= 2048)` guard); a null rect info is a legal result (spec §2.6
        // note 6).
        return TileRectInfoPtr{};
    }

    std::vector<TileRectInfoEntry> items(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    // §2.13.2: "remembers the previous directory name and whether it existed and skips both
    // stat()s for a tile in a directory it has just found missing" (VPrevFolderName/
    // VPrevFolderExist, u_TileStorageFileSystem.pas:314-347).
    QString prevFolder;
    bool prevFolderExists = false;
    bool havePrev = false;

    // spec §3.2: one ReadLock for the whole (read-only) walk -- coarse, deliberately so; only
    // scanTiles' enumerator is called out for per-item locking. Legacy's GetTileRectInfo itself
    // takes no FFsLock at all; §3 is this port's own, stricter thread-safety contract.
    core::ReadLock lock(fsMutex_);
    for (int y = r.top; y < r.bottom; ++y) {
        for (int x = r.left; x < r.right; ++x) {
            if (stop.stop_requested()) {
                return core::fail(core::ErrorCode::Cancelled, QStringLiteral("tileRectInfo cancelled"));
            }

            const geo::PointI tile{x, y};
            const QString base = basePath(tile, zoom);
            const QString tilePath = tilePathFor(base);
            const QString folder = QFileInfo(tilePath).path();

            bool folderExists = false;
            if (havePrev && folder == prevFolder) {
                folderExists = prevFolderExists;
            } else {
                folderExists = QDir(folder).exists();
                prevFolder = folder;
                prevFolderExists = folderExists;
                havePrev = true;
            }

            const qint64 idx =
                static_cast<qint64>(x - r.left) + static_cast<qint64>(y - r.top) * static_cast<qint64>(width);
            TileRectInfoEntry& entry = items[static_cast<std::size_t>(idx)];

            if (!folderExists) {
                entry.type = TileInfoType::NotExists;
                continue;
            }

            const QString tnePath = tnePathFor(base);
            core::Result<TileInfo> probed = probeLocked(tilePath, tnePath, false);
            if (!probed.has_value()) {
                return std::unexpected(probed.error());
            }
            entry.type = probed->type;
            entry.loadDate = probed->loadDate;
            entry.size = probed->size;
        }
    }
    lock.unlock();

    return std::make_shared<const TileRectInfo>(r, zoom, std::move(items));
}

core::Result<EnumTileInfoPtr> FileSystemTileStorage::scanTiles(bool ignoreTne,
                                                               bool /*ignoreMultiVersionTiles*/) {
    // FEAT-STO-035, u_TileStorageFileSystem.pas:ScanTiles:568-571 -- gated on ScanAccess, not
    // ReadAccess; legacy returns nil (not an error) when denied. `ignoreMultiVersionTiles` is
    // ignored: folder caches have no versions (§2.13.4).
    if (!base_.state().get()->scanAccess) {
        return EnumTileInfoPtr{};
    }
    return std::make_unique<FileSystemTileEnumerator>(base_.storagePath(), tileFileExt_, ignoreTne, codec_,
                                                      mainContentType_.type, base_, fsMutex_);
}

} // namespace cg::storage
