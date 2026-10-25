// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_VectorItemSubsetChangeable.pas
//
// A changeable holder of the current item subset (interface only; the implementations are
// ported with their owners).

#include "cg/core/changeable.h"
#include "cg/vector/vector_item_subset.h"

namespace cg::vector {

class IVectorItemSubsetChangeable : public core::Changeable {
public:
    [[nodiscard]] virtual VectorItemSubsetPtr getStatic() const = 0; // may be null (no items)
};

} // namespace cg::vector
