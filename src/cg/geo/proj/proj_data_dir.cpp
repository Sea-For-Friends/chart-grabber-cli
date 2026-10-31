// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors.

#include "cg/geo/proj/proj_data_dir.h"

#include <QFileInfo>

namespace cg::geo {

QString bundledProjDataDir(const QString& exeDir, bool explicitProjDataSet) {
    if (explicitProjDataSet) {
        return {};
    }
    return QFileInfo(exeDir + QStringLiteral("/proj.db")).isFile() ? exeDir : QString();
}

} // namespace cg::geo
