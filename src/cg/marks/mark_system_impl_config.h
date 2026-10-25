// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkSystemImplConfig.pas  (u_MarkSystemImplConfigBase.pas is ticket M4)

#include <QString>

#include <memory>

namespace cg::marks {

class IMarkSystemImplConfigStatic {
public:
    virtual ~IMarkSystemImplConfigStatic() = default;
    [[nodiscard]] virtual QString fileName() const = 0;
    [[nodiscard]] virtual bool isReadOnly() const = 0;
};
using MarkSystemImplConfigStaticPtr = std::shared_ptr<const IMarkSystemImplConfigStatic>;

} // namespace cg::marks
