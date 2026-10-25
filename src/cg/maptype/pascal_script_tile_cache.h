// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/PascalScript/u_PascalScriptTileCache.pas (the whole unit)
//
// The script's view of a map's tile storage (`TileCache.Read/Write/WriteTne/Delete`). No lock of
// its own.

#include "cg/core/content_type.h"
#include "cg/script/script_host.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_storage.h"

#include <memory>

namespace cg::maptype {

class PascalScriptTileCache final : public script::IScriptTileCache {
public:
    PascalScriptTileCache(std::shared_ptr<storage::ITileStorage> storage,
                          storage::MapVersionFactoryPtr versionFactory,
                          std::shared_ptr<const core::ContentTypeRegistry> contentTypes);

    // A storage error is treated as "no tile info".
    [[nodiscard]] script::TileInfo read(int x, int y, int zoom, const QByteArray& version,
                                        bool withData) const override;
    // Throws std::runtime_error "Unknown Content-Type: <type>" for a type the registry does not
    // know; a storage error (legacy: an exception out of SaveTile/DeleteTile) is thrown with the
    // storage's message.
    bool write(int x, int y, int zoom, const QByteArray& version, const QByteArray& contentType,
               const QByteArray& data, bool overwrite) override;
    bool writeTne(int x, int y, int zoom, const QByteArray& version) override;
    bool remove(int x, int y, int zoom, const QByteArray& version) override;

private:
    [[nodiscard]] storage::MapVersionInfoPtr versionOf(const QByteArray& version) const;

    std::shared_ptr<storage::ITileStorage> storage_;
    storage::MapVersionFactoryPtr versionFactory_;
    std::shared_ptr<const core::ContentTypeRegistry> contentTypes_;
};

} // namespace cg::maptype
