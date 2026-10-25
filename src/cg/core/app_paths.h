// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: the bootstrap in Src/u_GlobalState.pas (three TPathConfig.Create('', VProgramPath, nil)
// plus the cache root) and the portable-vs-XDG decision of ADR-0011.
//
// Legacy is unconditionally portable: every root is the executable's own directory. The port keeps
// that as a first-class, auto-detected mode (an existing Windows install directory copied onto
// Linux just works) and falls back to XDG locations for a packaged install.

#include "cg/core/result.h"

#include <QString>
#include <QStringList>

namespace cg::core {

// FEAT-UI-004
struct AppPaths {
    QString applicationDir; // read-only install dir: QCoreApplication::applicationDirPath()
    QString configDir;      // where chart-grabber.conf and MainFormState.ini live
    QString dataDir;        // Logs, TrackLog, MarksIcons, UserData, Marks.db3, ...
    QString cacheDir;       // tile caches; equals dataDir in both modes (ADR-0011)
    bool portable = false;
    bool legacyIni = false; // T-0311 (a): configDir is an old SAS.Planet dir whose SASPlanet.ini is read

    // Decides portable vs. XDG (ADR-0011). Creates configDir/dataDir/cacheDir if missing.
    // applicationDirOverride: test-only seam for the portable-marker detection (chart-grabber.conf /
    // portable.txt next to the exe), which otherwise always reads the real
    // QCoreApplication::applicationDirPath(); empty (the default) keeps production behavior.
    // roamingDataDirOverride: test-only seam for the T-0360 one-time move out of the roaming data folder
    // (empty = Qt's AppDataLocation, which differs from the local one only on Windows).
    [[nodiscard]] static Result<AppPaths> detect(const QStringList& arguments,
                                                 const QString& applicationDirOverride = QString(),
                                                 const QString& roamingDataDirOverride = QString());

    [[nodiscard]] QString mainConfigFilePath() const; // configDir + "/chart-grabber.conf"
};

} // namespace cg::core
