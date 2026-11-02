// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors.

#include "cg/core/platform/log_noise.h"

#include <QByteArrayView>
#include <QtGlobal>

namespace cg::core::platform {

void silenceQtPlatformLogNoise() {
#ifdef __APPLE__
    if (!qEnvironmentVariableIsSet("QT_LOGGING_RULES")) {
        qputenv("QT_LOGGING_RULES", "qt.qpa.fonts.warning=false");
    }
#endif
}

} // namespace cg::core::platform
