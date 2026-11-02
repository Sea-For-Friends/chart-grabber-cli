// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors.

#include "cg/core/platform/exe_dir.h"

#include <QFileInfo>

#ifdef __APPLE__
#include <cstdint>
#include <mach-o/dyld.h>
#include <vector>
#endif

namespace cg::core::platform {

QString executableDir() {
#ifdef __APPLE__
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size); // fails and stores the needed size
    std::vector<char> buffer(size + 1U, '\0');
    if (::_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    return QFileInfo(QString::fromLocal8Bit(buffer.data())).canonicalPath();
#else
    // /proc/self/exe exists on Linux only; elsewhere (Windows) the result stays empty.
    return QFileInfo(QStringLiteral("/proc/self/exe")).canonicalPath();
#endif
}

} // namespace cg::core::platform
