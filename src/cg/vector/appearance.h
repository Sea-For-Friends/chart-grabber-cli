// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_Appearance.pas

#include "cg/core/hash.h"

#include <memory>

namespace cg::vector {

class IAppearance {
public:
    virtual ~IAppearance() = default;
    [[nodiscard]] virtual core::HashValue hash() const noexcept = 0;
    [[nodiscard]] virtual bool isEqual(const IAppearance* value) const = 0;
};
using AppearancePtr = std::shared_ptr<const IAppearance>;

} // namespace cg::vector
