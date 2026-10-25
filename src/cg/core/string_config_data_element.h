// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_StringConfigDataElement.pas

#include "cg/core/config_element.h"

#include <QString>

namespace cg::core {

// FEAT-UI-207
class IStringConfigDataElement : public ConfigElement {
public:
    [[nodiscard]] virtual QString value() const = 0;
    virtual void setValue(const QString& value) = 0;
    [[nodiscard]] virtual QString defaultValue() const = 0;
};

} // namespace cg::core
