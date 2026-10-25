// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapTypeListChangeable.pas (the interface only; u_MapTypeListChangeableActiveBitmapLayers.pas
//        is a later ticket, spec maptype.md section 2.11.8)

#include "cg/core/changeable.h"
#include "cg/maptype/map_type_list.h"

namespace cg::maptype {

class IMapTypeListChangeable : public core::Changeable {
public:
    [[nodiscard]] virtual IMapTypeListStaticPtr list() const = 0; // may be null (empty list)
};

} // namespace cg::maptype
