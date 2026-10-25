// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/cache_converter.h"

#include "cg/core/delphi_text.h"

#include <QByteArray>
#include <QByteArrayView>

#include <utility>

namespace cg::storage {
namespace {

// Ports the derivation in TThreadCacheConverter.Create (:118-131): whether ScanTiles should
// collapse a multi-version source tile down to a single (current) entry.
bool deriveIgnoreMultiVersionTiles(const ITileStorage& from, const ITileStorage& to,
                                   bool checkSourceVersion) {
    if (from.typeAbilities().version != VersionSupport::MultiVersions) {
        return true; // no versioned tiles in source storage
    }
    if (checkSourceVersion) {
        return false; // process all versions matching the filter
    }
    // process all versions if dest storage supports versioned tiles
    return to.typeAbilities().version != VersionSupport::MultiVersions;
}

// Ports OnSourceTileStorageScan (:185-251). `tileInfo` is a scanned Exists/TneExists entry;
// returns the storage-level error (if any) that should abort convertCache's whole loop -- legacy
// lets this unwind as an unhandled exception instead (see cache_converter.h).
core::Result<void> convertOneTile(ITileStorage& to, const TileInfo& tileInfo, geo::PointI xy, quint8 zoom,
                                  const CacheConvertOptions& options, const MapVersionInfoPtr& destVersion,
                                  qint64& processed, qint64& skipped, qint64& bytes) {
    if (!options.overwriteDestination) {
        core::Result<TileInfo> existing = to.tileInfo(xy, zoom, destVersion, LoadMode::WithoutData);
        if (!existing.has_value()) {
            return std::unexpected(existing.error());
        }
        // A destination TNE only blocks a source TNE; a destination TNE never blocks a real
        // source tile from being written (u_ThreadCacheConverter.pas:209's exact condition).
        if (existing->isExists() || (existing->isExistsTne() && tileInfo.type == TileInfoType::TneExists)) {
            ++skipped;
            return {};
        }
    }

    // Empty contentType / null data => TNE (tile_storage.h's saveTile contract), matching legacy
    // passing nil/nil for a titTneExists source entry.
    QByteArrayView contentType;
    QByteArray data;
    if (tileInfo.type == TileInfoType::Exists) {
        contentType = QByteArrayView(tileInfo.contentType);
        data = tileInfo.data;
    }
    // `overwrite` is always true here: legacy passes the literal True
    // (u_ThreadCacheConverter.pas:227/:237), not ADestOverwriteTiles -- the "already exists" case
    // was already handled above.
    core::Result<bool> saved = to.saveTile(xy, zoom, destVersion, tileInfo.loadDate, contentType, data, true);
    if (!saved.has_value()) {
        return std::unexpected(saved.error());
    }
    // The bool payload (whether saveTile actually wrote) is ignored, matching legacy: SaveTile's
    // own return value is never consulted in OnSourceTileStorageScan, only its error channel is
    // (an unhandled exception there, per cache_converter.h's header comment).
    ++processed;
    if (tileInfo.type == TileInfoType::Exists) {
        bytes += tileInfo.data.size();
    }
    return {};
}

// Ports SameText(VTileInfo.FVersionInfo.StoreString, FSourceVersionInfo.StoreString)
// (u_ThreadCacheConverter.pas:163): ASCII case-insensitive equality of the two store strings
// (delphiLowerCase folds 7-bit ASCII only; Qt::CaseInsensitive would fold Unicode).
// Deliberately not MapVersionInfo::isSame (map_version.cpp:5-13), which is case-sensitive and
// adds a hash short-circuit -- other callers of isSame rely on that stricter behaviour, so this
// filter gets its own comparison instead of changing isSame.
bool versionStoreStringSameText(const MapVersionInfo& a, const MapVersionInfo& b) {
    return core::delphiSameText(a.storeString(), b.storeString());
}

} // namespace

core::Result<void> convertCache(ITileStorage& from, ITileStorage& to, const CacheConvertOptions& options,
                                const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return core::fail(core::ErrorCode::Cancelled, QStringLiteral("convertCache cancelled"));
    }

    const bool checkSourceVersion = options.sourceVersionFilter != nullptr;
    const bool replaceDestVersion = options.forcedDestinationVersion != nullptr;
    const bool ignoreMultiVersionTiles = deriveIgnoreMultiVersionTiles(from, to, checkSourceVersion);

    core::Result<EnumTileInfoPtr> enumResult = from.scanTiles(options.ignoreTne, ignoreMultiVersionTiles);
    if (!enumResult.has_value()) {
        return std::unexpected(enumResult.error());
    }
    EnumTileInfoPtr enumerator = std::move(*enumResult);
    if (!enumerator) {
        // The source denies scanning outright (ScanAccess false, or a backend that never
        // supports it at all -- u_TileStorageAbstract.pas's own default). Legacy's ScanTiles
        // cannot return a nil enumerator without VEnum.Next raising immediately; treat it as the
        // same kind of caller error instead of crashing.
        return core::fail(core::ErrorCode::Unsupported,
                          QStringLiteral("convertCache: source storage does not support scanning"));
    }

    qint64 processed = 0;
    qint64 skipped = 0;
    qint64 bytes = 0;

    TileInfo tileInfo;
    geo::PointI xy;
    quint8 zoom = 0;
    while (enumerator->next(tileInfo, xy, zoom)) {
        if (stop.stop_requested()) {
            return core::fail(core::ErrorCode::Cancelled, QStringLiteral("convertCache cancelled"));
        }

        // Version-filter comparison (u_ThreadCacheConverter.pas:162-170): `Assigned(VersionInfo)
        // and SameText(...)`. Legacy only matches when the scanned entry itself carries a
        // version; an unversioned entry (every entry a plain file-system enumerator yields,
        // u_EnumTileInfoByFileIterator.pas:113) takes the `else` branch and is SKIPPED, not
        // treated as a match -- reproduced here rather than the port's earlier reading of a null
        // MapVersionInfoPtr as "nothing to filter against" (legacy-defects.md L-1/L-2: this is
        // the data-loss combination with removeSource on a plain file cache).
        const bool versionMatches =
            !checkSourceVersion ||
            (tileInfo.version && versionStoreStringSameText(*tileInfo.version, *options.sourceVersionFilter));

        if (versionMatches) {
            if (tileInfo.type != TileInfoType::Exists && tileInfo.type != TileInfoType::TneExists) {
                // IEnumTileInfo's contract (tile_info.h) says a scan never yields Unknown, and no
                // backend's scanTiles yields NotExists; kept as a defensive abort (legacy's
                // OnSourceTileStorageScan falling through to Result=False, which breaks Process's
                // whole loop) rather than silently mis-saving an unintended TNE.
                return core::fail(core::ErrorCode::Unsupported,
                                  QStringLiteral("convertCache: unexpected scan entry type"));
            }
            const MapVersionInfoPtr destVersion =
                replaceDestVersion ? options.forcedDestinationVersion : tileInfo.version;
            core::Result<void> step =
                convertOneTile(to, tileInfo, xy, zoom, options, destVersion, processed, skipped, bytes);
            if (!step.has_value()) {
                return step;
            }
        } else {
            ++skipped;
        }

        if (options.progress) {
            options.progress(processed, skipped, bytes);
        }

        // u_ThreadCacheConverter.pas:174-178: removeSource deletes the scanned source entry
        // unconditionally (not just when it was actually copied) -- it runs after a
        // version-filter skip too. Verified against the legacy source; kept as a quirk ("legacy
        // wins", T-0027 dispatch) even though it differs from the ticket's own paraphrase.
        if (options.removeSource &&
            (tileInfo.type == TileInfoType::Exists || tileInfo.type == TileInfoType::TneExists)) {
            core::Result<bool> deleted = from.deleteTile(xy, zoom, tileInfo.version);
            if (!deleted.has_value()) {
                return std::unexpected(deleted.error());
            }
        }
    }

    // next() returned false: end of scan or a failed scan (legacy: the exception out of Next).
    if (auto st = enumerator->status(); !st.has_value()) {
        return std::unexpected(st.error());
    }
    return {};
}

} // namespace cg::storage
