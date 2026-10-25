// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/ConfigData/u_ConfigProviderHelpers.pas -- ReadColor32 (:155-181), WriteColor32 (:183-190).
// (ReadGUID/WriteGUID are IConfigProvider::readUuid / IConfigWriter::writeUuid.)
// Feature: FEAT-GEO-037

#include "cg/core/color32.h"
#include "cg/core/config_writer.h"

#include <QStringView>

namespace cg::core {

// Reads "<id>Hex" ("$AARRGGBB"); if that key is absent or empty, the legacy keys "<id>Alfa" (alpha)
// and "<id>" (a TColor integer). An unparsable Hex text yields def. A null provider yields def.
[[nodiscard]] Color32 readColor32(const IConfigProviderPtr& provider, QStringView id, Color32 def);

// Writes "<id>Hex" = "$" + 8 upper-case hex digits of the ARGB value (e.g. "$A6FFFF00").
void writeColor32(IConfigWriter& provider, QStringView id, Color32 value);

} // namespace cg::core
