// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_ExportConfig.pas
//
// Marker interface of an exporter's settings: no members of its own, as in legacy
// (IExportConfig = interface(IConfigDataElement)). The concrete configs come with their exporters.

#include "cg/core/config_element.h"

namespace cg::vector {

class IExportConfig : public core::ConfigElement {};

} // namespace cg::vector
