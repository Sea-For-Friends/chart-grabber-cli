// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/i_StringProvider.pas

#include <QString>

#include <memory>

namespace cg::vector {

class IStringProvider {
public:
    virtual ~IStringProvider() = default;
    [[nodiscard]] virtual QString value() const = 0;
};
using StringProviderPtr = std::shared_ptr<const IStringProvider>;

} // namespace cg::vector
