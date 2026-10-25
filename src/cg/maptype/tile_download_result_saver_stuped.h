// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileDownloadResultSaver.pas, u_TileDownloadResultSaverStuped.pas (the non-bitmap path, R-9)
//
// Stores what a download produced: an OK result is converted to the map's content type (ContentConverters,
// the converter matrix of T-0120) and saved, a
// data-not-exists result is saved as a TNE marker when the global option says so. The crop / cut half of the
// unit (:202-231, :328-407, :434-486) needs `bitmap` and is R-13; it ends in a ticketed exception here.
//
// Not ported here, with the reason: the constructor arguments of legacy that only that half (or the discarded
// result below) uses: ImageResampler, Bitmap32StaticFactory, ResultFactory. A saver that built the
// "Tile is recognized as empty" download result would throw it away: SaveDownloadResult (:257-261) overwrites
// the request result with Ok(original result), so no code builds it.

#include "cg/core/content_type.h"
#include "cg/core/content_type_subst.h"
#include "cg/core/notifier.h"
#include "cg/maptype/content_converters.h"
#include "cg/maptype/predicate_by_static_sample_list.h"
#include "cg/maptype/tile_downloader_state.h"
#include "cg/maptype/tile_post_download_crop_config.h"
#include "cg/maptype/tile_request_result.h"
#include "cg/net/download_request.h"
#include "cg/net/download_result.h"
#include "cg/net/global_download_config.h"
#include "cg/storage/tile_storage.h"

#include <QString>

#include <memory>
#include <optional>
#include <stdexcept>

namespace cg::maptype {

// i_TileDownloadResultSaver.pas:32-38
class ITileDownloadResultSaver {
public:
    virtual ~ITileDownloadResultSaver() = default;
    [[nodiscard]] virtual std::shared_ptr<ITileDownloaderStateChangeable> state() const = 0;

    // Returns null for every result kind but Ok and DataNotExists (the caller classifies those). Throws like
    // legacy: the data-not-exists path lets every exception through (the caller wraps it, see the .cpp).
    [[nodiscard]] virtual ITileRequestResultPtr saveDownloadResult(const net::DownloadRequest& request,
                                                                   const net::DownloadResult& result) = 0;
};

// ESaveTileDownloadError (u_TileDownloadResultSaverStuped.pas:113); the message is the exception text.
class SaveTileDownloadError final : public std::runtime_error {
public:
    explicit SaveTileDownloadError(const QString& message);
};

class TileDownloadResultSaverStuped final : public ITileDownloadResultSaver {
public:
    // Argument order as the Pascal constructor (:141-151) minus the three unported arguments (ImageResampler,
    // Bitmap32StaticFactory, ResultFactory: TODO(T-0119)): downloadConfig, emptyTilePredicate,
    // contentTypes (AContentTypeManager), converters (its GetConverter half), contentTypeSubst,
    // saveContentType, cropConfig, storage. downloadConfig: legacy IGlobalDownloadConfig, IsSaveTileNotExists
    // is read at every save (:315). The saver subscribes to storage->stateChangeable().changed() (legacy
    // FStorage.State.ChangeNotifier, :181-182). emptyTilePredicate may be empty (legacy nil). Null
    // downloadConfig, cropConfig or storage are port preconditions, as a null converters (legacy asserts
    // them, assertions are off in the shipped build, the only creator passes non-nil): std::invalid_argument.
    // The state is evaluated once here.
    TileDownloadResultSaverStuped(std::shared_ptr<const net::IGlobalDownloadConfig> downloadConfig,
                                  PredicateByBinaryData emptyTilePredicate,
                                  const core::ContentTypeRegistry& contentTypes,
                                  std::shared_ptr<const ContentConverters> converters,
                                  core::ContentTypeSubst contentTypeSubst,
                                  core::ContentTypeInfo saveContentType,
                                  TilePostDownloadCropConfigStaticPtr cropConfig,
                                  storage::ITileStoragePtr storage);

    [[nodiscard]] std::shared_ptr<ITileDownloaderStateChangeable> state() const override;
    [[nodiscard]] ITileRequestResultPtr saveDownloadResult(const net::DownloadRequest& request,
                                                           const net::DownloadResult& result) override;

private:
    void onStorageStateChange(); // :233-243
    void saveTileDownload(const net::DownloadRequest& request,
                          const net::DownloadResult& result) const; // :409-511
    void saveTileToStorage(geo::PointI xy, quint8 zoom, const storage::MapVersionInfoPtr& version,
                           const std::optional<QByteArray>& data) const; // :291-326

    std::shared_ptr<const net::IGlobalDownloadConfig> downloadConfig_;
    const core::ContentTypeRegistry& contentTypes_;
    std::shared_ptr<const ContentConverters> converters_;
    core::ContentTypeSubst contentTypeSubst_;
    core::ContentTypeInfo saveContentType_;
    PredicateByBinaryData emptyTilePredicate_;
    TilePostDownloadCropConfigStaticPtr cropConfig_;
    storage::ITileStoragePtr storage_;
    std::shared_ptr<TileDownloaderStateInternal> state_;
    // Declared after storage_ and state_, so it is destroyed first: the listener is gone before the storage
    // (whose holder it is subscribed to) or the state it writes can die. Legacy :187-194 intends the same
    // (its destructor calls Add instead of Remove, so legacy never unsubscribes; the port does).
    // This covers unsubscription only: `Subscription::reset` does not wait for a callback in flight, so the
    // owner (R-10) must make sure the storage state no longer changes on another thread before the saver is
    // destroyed (docs/specs/maptype.md §2.13.5 item 8).
    core::Subscription storageStateSubscription_;
};

} // namespace cg::maptype
