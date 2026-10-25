// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/pascal_script_tile_cache.h"

#include "cg/storage/tile_date.h"

#include <stdexcept>
#include <utility>

namespace cg::maptype {

namespace {

template <class T> T valueOrThrow(core::Result<T> result) {
    if (!result) {
        throw std::runtime_error(result.error().what().toStdString());
    }
    return *std::move(result);
}

} // namespace

PascalScriptTileCache::PascalScriptTileCache(std::shared_ptr<storage::ITileStorage> storage,
                                             storage::MapVersionFactoryPtr versionFactory,
                                             std::shared_ptr<const core::ContentTypeRegistry> contentTypes)
    : storage_(std::move(storage)), versionFactory_(std::move(versionFactory)),
      contentTypes_(std::move(contentTypes)) {}

// The script's `version` is an AnsiString; the port reads it as UTF-8.
storage::MapVersionInfoPtr PascalScriptTileCache::versionOf(const QByteArray& version) const {
    return versionFactory_->byStoreString(QString::fromUtf8(version));
}

// u_PascalScriptTileCache.pas:118-181
script::TileInfo PascalScriptTileCache::read(int x, int y, int zoom, const QByteArray& version,
                                             bool withData) const {
    script::TileInfo result; // :93-101, :132
    const auto ver = versionOf(version);
    const geo::PointI xy(x, y);
    const auto zoomByte = static_cast<quint8>(zoom);

    auto first = storage_->tileInfo(xy, zoomByte, ver,
                                    withData ? storage::LoadMode::WithData : storage::LoadMode::WithoutData);
    if (!first ||
        first->type == storage::TileInfoType::Unknown) { // :143-145 (nil); a storage error is "no info"
        return result;
    }
    storage::TileInfo info = *std::move(first);
    result.isExists = info.isExists();
    result.isExistsTne = info.isExistsTne();

    // :150-156: a miss with data loaded is asked again without it, and the rest of the result is
    // filled from the second answer.
    if (!info.isExists() && !info.isExistsTne() && withData) {
        auto second = storage_->tileInfo(xy, zoomByte, ver, storage::LoadMode::WithoutData);
        if (!second || second->type == storage::TileInfoType::Unknown) { // :152-154 (nil)
            return result;
        }
        info = *std::move(second);
        result.isExistsTne = info.isExistsTne();
    }

    if (info.isExists() || info.isExistsTne()) { // :158-160
        result.loadDate = info.loadDate;
    }
    if (info.version) { // :162-164
        result.version = info.version->storeString().toUtf8();
    }
    if (info.isExists()) { // :166-179
        result.size = info.size;
        result.contentType = info.contentType;
        if (withData && !info.data.isEmpty()) {
            result.data = info.data;
        }
    }
    return result;
}

// :183-207
bool PascalScriptTileCache::write(int x, int y, int zoom, const QByteArray& version,
                                  const QByteArray& contentType, const QByteArray& data, bool overwrite) {
    const auto ver = versionOf(version);
    const auto* info = contentTypes_->infoByType(contentType);
    if (info == nullptr) { // :201-203
        throw std::runtime_error("Unknown Content-Type: " + contentType.toStdString());
    }
    // CreateByAnsiString('') is a non-nil 0-byte buffer (u_BinaryData.pas:72-90): an empty script
    // string is a 0-byte tile, not a TNE request, so the array handed over must not be null.
    const QByteArray bytes = data.isNull() ? QByteArray("", 0) : data;
    return valueOrThrow(storage_->saveTile(geo::PointI(x, y), static_cast<quint8>(zoom), ver,
                                           storage::tileDateNow(), info->type, bytes, overwrite));
}

// :209-220: no content type and no data make the storage write a TNE marker.
bool PascalScriptTileCache::writeTne(int x, int y, int zoom, const QByteArray& version) {
    return valueOrThrow(storage_->saveTile(geo::PointI(x, y), static_cast<quint8>(zoom), versionOf(version),
                                           storage::tileDateNow(), {}, {}, true));
}

// :222-233
bool PascalScriptTileCache::remove(int x, int y, int zoom, const QByteArray& version) {
    return valueOrThrow(
        storage_->deleteTile(geo::PointI(x, y), static_cast<quint8>(zoom), versionOf(version)));
}

} // namespace cg::maptype
