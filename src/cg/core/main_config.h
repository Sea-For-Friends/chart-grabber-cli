// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/u_SASMainConfigProvider.pas (class TSASMainConfigProvider)
//
// Composes, outside-in:
//   ConfigProviderWithGlobal( main   = ConfigProviderIni over <configDir>/chart-grabber.conf,
//                             prefix = "sas:\",
//                             global = ConfigProviderVirtualWithSubItem("Resource",
//                                          ConfigProviderResource(":/cg")) )
// The "sas:\" prefix and the "Resource" child name are on-disk/behavioural constants: the two ui
// call sites pass the literals "sas:\Resource\ABOUTICON.png" and "sas:\Resource\LOGOI.jpg".

#include "cg/core/app_paths.h"
#include "cg/core/config_writer.h"
#include "cg/core/result.h"

namespace cg::core {

// FEAT-UI-004
[[nodiscard]] Result<IConfigWriterPtr> makeMainConfigProvider(const AppPaths& paths);

// The IniFile behind it, for the explicit save at shutdown (legacy saved in the destructor; the
// port makes it explicit, see config_provider_ini.h).
[[nodiscard]] Result<void> saveMainConfig(const IConfigWriterPtr& provider);

} // namespace cg::core
