// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors.

#pragma once
// Ports: nothing (Linux-only addition; legacy is Windows, where libproj finds proj.db beside the DLL).
// T-0376: where a relocated Linux CLI finds proj.db. The bundle ships `<exe dir>/proj.db`, the
// same layout as the Windows zip (tools/vm/win10/package_in_vm.sh:70-71, where libproj looks in its
// own directory). On Linux libproj knows only PROJ_DATA and its compiled-in prefix, so the
// directory is handed to the context explicitly.
//
// FEAT-GEO-002 (custom CRS via PROJ4 strings / EPSG).

#include <QString>

namespace cg::geo {

// Returns `exeDir` when `<exeDir>/proj.db` is a file and the user has set neither PROJ_DATA nor
// PROJ_LIB (`explicitProjDataSet`); otherwise an empty string (keep PROJ's own lookup).
[[nodiscard]] QString bundledProjDataDir(const QString& exeDir, bool explicitProjDataSet);

} // namespace cg::geo
