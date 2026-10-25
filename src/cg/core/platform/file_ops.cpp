// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/platform/file_ops.h"

#include "cg/core/platform/native_path.h"

#include <QFile>

#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace cg::core::platform {

std::error_code replaceFile(const QString& from, const QString& to) {
#ifdef _WIN32
    if (::MoveFileExW(reinterpret_cast<const wchar_t*>(from.utf16()),
                      reinterpret_cast<const wchar_t*>(to.utf16()),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == 0) {
        return {static_cast<int>(::GetLastError()), std::system_category()};
    }
    return {};
#else
    std::error_code ec;
    std::filesystem::rename(nativePath(from), nativePath(to), ec);
    return ec;
#endif
}

bool renamePath(const QString& from, const QString& to) {
#ifdef _WIN32
    return _wrename(reinterpret_cast<const wchar_t*>(from.utf16()),
                    reinterpret_cast<const wchar_t*>(to.utf16())) == 0;
#else
    return std::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
#endif
}

QByteArray nativeFilePathFromUrlPath(const QByteArray& path) {
#ifdef _WIN32
    // file:///C:/x leaves "/C:/x"; the drive letter form needs "C:/x" (D-11 listed these forms as not
    // handled).
    if (path.size() >= 3 && path.at(0) == '/' && path.at(2) == ':' &&
        ((path.at(1) >= 'A' && path.at(1) <= 'Z') || (path.at(1) >= 'a' && path.at(1) <= 'z')) &&
        (path.size() == 3 || path.at(3) == '/')) {
        return path.mid(1);
    }
#endif
    return path;
}

} // namespace cg::core::platform
