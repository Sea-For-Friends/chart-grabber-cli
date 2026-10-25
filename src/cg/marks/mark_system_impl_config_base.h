// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MarkSystemImplConfigBase.pas

#include "cg/marks/mark_system_impl_config.h"

#include <QString>

#include <utility>

namespace cg::marks {

// Virtual base so the IMarkSystemImplConfigSML/ORM interfaces share it. Not final: the SML and ORM
// impl-configs derive from it (u_MarkSystemImplConfigBase.pas:31-67).
class MarkSystemImplConfigBase : public virtual IMarkSystemImplConfigStatic {
public:
    MarkSystemImplConfigBase(QString fileName, bool isReadOnly)
        : fileName_(std::move(fileName)), isReadOnly_(isReadOnly) {}
    [[nodiscard]] QString fileName() const override { return fileName_; }
    [[nodiscard]] bool isReadOnly() const override { return isReadOnly_; }

private:
    QString fileName_;
    bool isReadOnly_;
};

} // namespace cg::marks
