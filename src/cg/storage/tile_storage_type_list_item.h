// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileStorageTypeListItem.pas, u_TileStorageTypeListItem.pas
//
// One row of the storage type list: the cache-type GUID and integer code, the caption, the storage
// type and the two flags of the "Map properties" combo box. Immutable, shared by pointer.

#include "cg/storage/tile_storage_type.h"

#include <QString>
#include <QUuid>

#include <memory>
#include <utility>

namespace cg::storage {

class ITileStorageTypeListItem {
public:
    virtual ~ITileStorageTypeListItem() = default;

    [[nodiscard]] virtual QUuid guid() const = 0;
    [[nodiscard]] virtual int intCode() const = 0;
    [[nodiscard]] virtual QString caption() const = 0;
    [[nodiscard]] virtual ITileStorageTypePtr storageType() const = 0;
    [[nodiscard]] virtual bool canUseAsDefault() const = 0;
    [[nodiscard]] virtual bool isChangeable() const = 0;
};
using ITileStorageTypeListItemPtr = std::shared_ptr<const ITileStorageTypeListItem>;

class TileStorageTypeListItem final : public ITileStorageTypeListItem {
public:
    TileStorageTypeListItem(const QUuid& guid, int intCode, QString caption, ITileStorageTypePtr storageType,
                            bool canUseAsDefault, bool isChangeable)
        : guid_(guid), intCode_(intCode), caption_(std::move(caption)), storageType_(std::move(storageType)),
          canUseAsDefault_(canUseAsDefault), isChangeable_(isChangeable) {}

    [[nodiscard]] QUuid guid() const override { return guid_; }
    [[nodiscard]] int intCode() const override { return intCode_; }
    [[nodiscard]] QString caption() const override { return caption_; }
    [[nodiscard]] ITileStorageTypePtr storageType() const override { return storageType_; }
    [[nodiscard]] bool canUseAsDefault() const override { return canUseAsDefault_; }
    [[nodiscard]] bool isChangeable() const override { return isChangeable_; }

private:
    QUuid guid_;
    int intCode_;
    QString caption_;
    ITileStorageTypePtr storageType_;
    bool canUseAsDefault_;
    bool isChangeable_;
};

} // namespace cg::storage
