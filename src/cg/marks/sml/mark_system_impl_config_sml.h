// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: SML/i_MarkSystemImplConfigSML.pas, SML/u_MarkSystemImplConfigSML.pas (marker subclass, no members)

#include "cg/marks/mark_system_impl_config_base.h"

#include <QUuid>

namespace cg::marks {

// IMarkSystemImplConfigSML's interface GUID (i_MarkSystemImplConfigSML.pas:31); the INI key
// ItemN_Impl holds it (u_MarkSystemConfig.pas:231, :311).
inline constexpr QUuid iidMarkSystemImplConfigSML{0xE687ABD9, 0x5F0C, 0x4AF2, 0x8E, 0x0C, 0xEF,
                                                  0x09,       0xA4,   0x50,   0x6A, 0x36};

// ToDo (legacy): UseUnicodeSchema, StoreInBinaryFormat.
class IMarkSystemImplConfigSML : public virtual IMarkSystemImplConfigStatic {};

class MarkSystemImplConfigSML final : public MarkSystemImplConfigBase, public IMarkSystemImplConfigSML {
public:
    using MarkSystemImplConfigBase::MarkSystemImplConfigBase;
};

} // namespace cg::marks
