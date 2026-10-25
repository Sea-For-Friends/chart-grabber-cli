// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapTypeSetChangeable.pas (the interface only; the two classes of
//        u_MapTypeSetChangeable.pas join this header with the active-maps ticket)

#include "cg/core/changeable.h"
#include "cg/maptype/map_type_set.h"

namespace cg::maptype {

class IMapTypeSetChangeable : public core::Changeable {
public:
    [[nodiscard]] virtual IMapTypeSetPtr getStatic() const = 0; // may be null
};

} // namespace cg::maptype
