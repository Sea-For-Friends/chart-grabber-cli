// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileStorageTypeList.pas (ITileStorageTypeListStatic), u_TileStorageTypeList.pas
//
// The static, ordered list of storage types; looked up by the integer cache-type code.

#include "cg/storage/tile_storage_type_list_item.h"

#include <vector>

namespace cg::storage {

class ITileStorageTypeList {
public:
    virtual ~ITileStorageTypeList() = default;

    [[nodiscard]] virtual int count() const = 0;
    [[nodiscard]] virtual ITileStorageTypeListItemPtr item(int index) const = 0;
    // nullptr when no item has `code`.
    [[nodiscard]] virtual ITileStorageTypeListItemPtr itemByCode(int code) const = 0;
};
using ITileStorageTypeListPtr = std::shared_ptr<const ITileStorageTypeList>;

class TileStorageTypeList : public ITileStorageTypeList {
public:
    explicit TileStorageTypeList(std::vector<ITileStorageTypeListItemPtr> items);

    [[nodiscard]] int count() const override;
    [[nodiscard]] ITileStorageTypeListItemPtr item(int index) const override;
    [[nodiscard]] ITileStorageTypeListItemPtr itemByCode(int code) const override;

private:
    std::vector<ITileStorageTypeListItemPtr> items_;
};

} // namespace cg::storage
