// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_type_hot_key_list_static.h"

#include "cg/maptype/map_type_gui_config.h"

#include <stdexcept>

namespace cg::maptype {

// u_MapTypeHotKeyListStatic.pas:53-74
MapTypeHotKeyListStatic::MapTypeHotKeyListStatic(const IMapTypeSetPtr& mapsSet) {
    if (mapsSet == nullptr) {
        return;
    }
    for (int i = 0; i < mapsSet->count(); ++i) {
        const IMapTypePtr map = mapsSet->item(i);
        if (map == nullptr) {
            throw std::logic_error("MapTypeHotKeyList: nil map");
        }
        const auto guiConfig = map->guiConfig();
        if (guiConfig == nullptr) {
            throw std::logic_error("MapTypeHotKeyList: nil GUI config");
        }
        const quint16 hotKey = guiConfig->hotKey();
        if (guiConfig->enabled() && hotKey != 0 && !maps_.contains(hotKey)) {
            maps_.insert(hotKey, map);
        }
    }
}

// u_MapTypeHotKeyListStatic.pas:76-81
IMapTypePtr MapTypeHotKeyListStatic::mapTypeByHotKey(quint16 hotKey) const {
    return maps_.value(hotKey);
}

} // namespace cg::maptype
