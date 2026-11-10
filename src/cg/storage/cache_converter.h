// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ThreadCacheConverter.pas (Process :148-183, OnSourceTileStorageScan :185-251, the
// constructor's FIgnoreMultiVersionTiles derivation :118-131), i_CacheConverterProgressInfo.pas
// (TilesProcessed/TilesSkipped/TilesSize -- the counters `progress` below replaces).
//
// The engine behind the Cache Manager's "Convert" button: copies every tile (and TNE) from one
// ITileStorage to another. Pure ITileStorage -> ITileStorage copying, synchronous and blocking
// like every other storage method -- the caller owns the thread and the std::stop_token, same as
// legacy's TThreadCacheConverter being the one thread the Cache Manager dialog drives. The dialog
// itself (frm_CacheManager.*, frm_ProgressCacheConvrter.*) and the thread/progress-dialog wiring
// (FPaused/FSleepEvent) belong to `ui` -- not implemented here.

#include "cg/core/result.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_storage.h"

#include <QtGlobal>

#include <functional>
#include <stop_token>

namespace cg::storage {

// The engine only (see the file header). Ports the subset of TThreadCacheConverter's constructor
// parameters that describe *what* to copy.
struct CacheConvertOptions {
    bool ignoreTne = false;                     // ports ASourceIgnoreTne -> ScanTiles(ASourceIgnoreTne, ...)
    bool removeSource = false;                  // ports ASourceRemoveTiles
    bool overwriteDestination = false;          // ports ADestOverwriteTiles
    MapVersionInfoPtr sourceVersionFilter;      // null = every version (ports ASourceVersionInfo)
    MapVersionInfoPtr forcedDestinationVersion; // ports ADestVersionInfo/FReplaceDestVersion
    // Ports FProgressInfo.TilesProcessed/TilesSkipped/TilesSize: invoked after each scanned tile,
    // processed or skipped, with the running totals. Unlike ICacheConverterProgressInfo there is
    // no separate LastTileName/Finished field -- the caller observes completion by convertCache
    // returning (the interface has no such fields; do not add them).
    std::function<void(qint64 processed, qint64 skipped, qint64 bytes)> progress;
};

// Ports TThreadCacheConverter::Process/OnSourceTileStorageScan as one synchronous loop over
// `from.scanTiles()`. `from` must stay alive and undisturbed by any other writer for the whole
// call: the scan enumerator this function drains borrows the source storage's internal state by
// reference (file_system_tile_storage.cpp's FileSystemTileEnumerator). `std::stop_token` is polled
// at the top of every iteration; a mid-scan storage-level error (an incompatible destination
// content type, a read/write failure) stops the loop and is returned as the error -- legacy
// instead lets the unhandled exception unwind out of the unguarded per-tile call, where
// `TThreadCacheManagerAbstract.Execute`'s `try..except` catches it, shows it via
// `TExceptionManager.ShowExceptionInfo`, and its `finally` still marks the progress info
// `Finished := True` (u_ThreadCacheManagerAbstract.pas:93-97) -- not silent. No legacy equivalent
// for a Result-typed abort; the port fails loudly instead (returns the error to its caller),
// consistent with "no silent divergence"; showing the error and finishing the progress dialog is
// the UI's job.
[[nodiscard]] core::Result<void> convertCache(ITileStorage& from, ITileStorage& to,
                                              const CacheConvertOptions& options,
                                              const std::stop_token& stop);

} // namespace cg::storage
