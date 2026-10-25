// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_download_result_saver_stuped.h"

#include "cg/maptype/tile_request.h"
#include "cg/storage/tile_date.h"

#include <QCoreApplication>

#include <exception>
#include <utility>

namespace cg::maptype {

namespace {

constexpr const char* kTrContext = "TileDownloadResultSaver";

// resourcestring rsNoWriteAccess (:134); also the text of the exception at :302.
QString noWriteAccessText() {
    return QCoreApplication::translate(kTrContext, "No write access to tile storage");
}

// SAS_ERR_BadMIMEForDownloadRastr (u_ResStrings.pas:102), `%0:s` -> %1.
SaveTileDownloadError badMimeForDownloadRastr(const QByteArray& contentType) {
    return SaveTileDownloadError(
        QCoreApplication::translate(kTrContext, "Server returned type \"%1\", this is not bitmap image")
            .arg(QString::fromLatin1(contentType)));
}

QString messageOf(const std::exception& e) {
    return QString::fromUtf8(e.what());
}

} // namespace

SaveTileDownloadError::SaveTileDownloadError(const QString& message)
    : std::runtime_error(message.toStdString()) {}

TileDownloadResultSaverStuped::TileDownloadResultSaverStuped(
    std::shared_ptr<const net::IGlobalDownloadConfig> downloadConfig,
    PredicateByBinaryData emptyTilePredicate, const core::ContentTypeRegistry& contentTypes,
    std::shared_ptr<const ContentConverters> converters, core::ContentTypeSubst contentTypeSubst,
    core::ContentTypeInfo saveContentType, TilePostDownloadCropConfigStaticPtr cropConfig,
    storage::ITileStoragePtr storage)
    : downloadConfig_(std::move(downloadConfig)), contentTypes_(contentTypes),
      converters_(std::move(converters)), contentTypeSubst_(std::move(contentTypeSubst)),
      saveContentType_(std::move(saveContentType)), emptyTilePredicate_(std::move(emptyTilePredicate)),
      cropConfig_(std::move(cropConfig)), storage_(std::move(storage)),
      state_(std::make_shared<TileDownloaderStateInternal>()) {
    if (!downloadConfig_ || !converters_ || !cropConfig_ || !storage_) {
        throw std::invalid_argument("TileDownloadResultSaverStuped: null collaborator");
    }
    storageStateSubscription_ =
        storage_->stateChangeable().changed().subscribe([this] { onStorageStateChange(); }); // :181-182
    onStorageStateChange();                                                                  // :184
}

std::shared_ptr<ITileDownloaderStateChangeable> TileDownloadResultSaverStuped::state() const {
    return state_;
}

void TileDownloadResultSaverStuped::onStorageStateChange() {
    if (!storage_->state()->addAccess) { // :238
        state_->disable(
            QStringLiteral("No write access to tile storage")); // gettext_NoOp: stored untranslated
    } else {
        state_->enable();
    }
}

ITileRequestResultPtr TileDownloadResultSaverStuped::saveDownloadResult(const net::DownloadRequest& request,
                                                                        const net::DownloadResult& result) {
    if (result.kind == net::ResultKind::Ok) { // :258
        try {
            saveTileDownload(request, result);
            return std::make_shared<TileRequestResultOk>(request, result); // :261, the original result
        } catch (const std::exception& e) {                                // :262-269
            // For a request that is not a tile request this constructor throws as well (legacy :264-267: the
            // cast `Request as ITileDownloadRequest` inside the handler raises); that exception leaves here.
            return std::make_shared<TileRequestResultErrorAfterDownloadRequest>(request, result,
                                                                                messageOf(e));
        }
    }
    if (result.kind == net::ResultKind::DataNotExists) { // :273
        const ITileRequestPtr tile = tileRequestFromSource(request.source);
        if (!tile) {
            throw std::logic_error(
                "This was not tile request"); // :283, no handler here (the caller wraps it)
        }
        saveTileToStorage(tile->tile(), tile->zoom(), tile->versionInfo(), std::nullopt); // :276-281
        return std::make_shared<TileRequestResultOk>(request, result);                    // :285
    }
    return nullptr;
}

void TileDownloadResultSaverStuped::saveTileToStorage(geo::PointI xy, quint8 zoom,
                                                      const storage::MapVersionInfoPtr& version,
                                                      const std::optional<QByteArray>& data) const {
    const storage::StorageStatePtr storageState = storage_->state(); // :300
    if (!storageState->addAccess) {                                  // :301
        throw SaveTileDownloadError(noWriteAccessText());
    }
    core::Result<bool> saved{false};
    if (data) { // :304
        saved = storage_->saveTile(xy, zoom, version, storage::tileDateNow(), saveContentType_.type, *data,
                                   storageState->replaceAccess);
    } else if (downloadConfig_->isSaveTileNotExists()) { // :314-315
        saved = storage_->saveTile(xy, zoom, version, storage::tileDateNow(), QByteArrayView(), QByteArray(),
                                   storageState->replaceAccess);
    }
    if (!saved) { // legacy SaveTile raises, the exception text is the message
        throw SaveTileDownloadError(saved.error().what());
    }
}

void TileDownloadResultSaverStuped::saveTileDownload(const net::DownloadRequest& request,
                                                     const net::DownloadResult& result) const {
    const QByteArray contentType = contentTypeSubst_.substitute(result.contentType); // :426-427
    const ITileRequestPtr tile = tileRequestFromSource(request.source);
    if (!tile) { // :428-432
        throw std::logic_error("This was not tile request");
    }
    if (saveContentType_.kind == core::ContentKind::Bitmap &&
        (cropConfig_->isCropOnDownload() || cropConfig_->isCutOnDownload())) { // :434-435
        // :436-440, :478-485: an unknown or non-bitmap received type is the BadMIME error (every bitmap type
        // has a loader in the standard table, so the `Loader = nil` branch of :478 is not reachable; not
        // traced beyond the table).
        if (!contentTypes_.isBitmapType(contentType)) {
            throw badMimeForDownloadRastr(contentType);
        }
        // TODO(T-0119) R-13: decode, CropOnDownload (:202-231) / CutDownloadedBitmap (:328-407), re-encode,
        // empty check, save (:441-477). Needs module `bitmap`; OQ-16: the branch throws, it is not silent.
        // until then the map is refused: docs/specs/bitmap.md §1.7
        throw SaveTileDownloadError(QCoreApplication::translate(
            kTrContext, "Crop and cut of downloaded tiles are not supported for this map"));
    }
    // :487-510
    const ContentConverterPtr converter = converters_->converter(contentType, saveContentType_.type); // :488
    if (!converter) { // :507-509
        throw badMimeForDownloadRastr(contentType);
    }
    std::optional<QByteArray> data = converter->convert(result.data); // :489
    if (data && emptyTilePredicate_ && emptyTilePredicate_(*data)) {  // :490-495
        data.reset();
    }
    // :496-505 would build the "Tile is recognized as empty" result; SaveDownloadResult discards it
    // (:260-261).
    saveTileToStorage(tile->tile(), tile->zoom(), tile->versionInfo(), data); // :506
}

} // namespace cg::maptype
