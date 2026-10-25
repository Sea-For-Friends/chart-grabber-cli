// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/tile_storage_type_list.h"

namespace cg::storage {

TileStorageTypeList::TileStorageTypeList(std::vector<ITileStorageTypeListItemPtr> items)
    : items_(std::move(items)) {}

int TileStorageTypeList::count() const {
    return static_cast<int>(items_.size());
}

ITileStorageTypeListItemPtr TileStorageTypeList::item(int index) const {
    return items_.at(static_cast<std::size_t>(index));
}

ITileStorageTypeListItemPtr TileStorageTypeList::itemByCode(int code) const {
    for (const ITileStorageTypeListItemPtr& item : items_) {
        if (item->intCode() == code) {
            return item;
        }
    }
    return nullptr;
}

} // namespace cg::storage
