// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MapTypeHotKeyListStatic.pas (TMapTypeHotKeyListStatic) and its interface
//        i_MapTypeHotKeyListStatic.pas (merged into the class: one implementation, no second user)
//
// The keyboard shortcuts of the enabled maps of one map set, built once and never changed.

#include "cg/maptype/map_type.h"
#include "cg/maptype/map_type_set.h"

#include <QHash>
#include <QtGlobal>

namespace cg::maptype {

// FEAT-NET-030 (contributes; held open)
class MapTypeHotKeyListStatic final {
public:
    // A map is listed when its GUI config is enabled and its hot key is not 0. The first such map
    // in set order keeps a key that several maps share (TIDInterfaceList.Add does not replace). A
    // null set gives an empty list (legacy: access violation; see deviations.md). A null map or a
    // null GUI config inside the set throws std::logic_error (legacy: access violation; a set built
    // with allowNil may hold a null map).
    explicit MapTypeHotKeyListStatic(const IMapTypeSetPtr& mapsSet);

    // Null when no listed map has the key. Key 0 is never listed.
    [[nodiscard]] IMapTypePtr mapTypeByHotKey(quint16 hotKey) const;

private:
    QHash<quint16, IMapTypePtr> maps_;
};

} // namespace cg::maptype
