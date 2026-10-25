// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/platform/file_times.h"

#include <QByteArray>
#include <QFile>

#include <cerrno>
#include <cstring>
#include <ctime>
#ifdef _WIN32
#include <QFileInfo>

#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace cg::core::platform {
namespace {

#ifndef _WIN32
[[nodiscard]] Unexpected ioFail(const QString& path, const char* op) {
    return fail(ErrorCode::IoError,
                QStringLiteral("%1 failed for '%2': %3")
                    .arg(QString::fromLatin1(op), path, QString::fromLocal8Bit(std::strerror(errno))));
}

#else
// T-0335: wide Win32 calls (a path with non-ASCII characters survives); error text is the system code.
[[nodiscard]] Unexpected ioFail(const QString& path, const char* op, DWORD code) {
    return fail(ErrorCode::IoError, QStringLiteral("%1 failed for '%2': error %3")
                                        .arg(QString::fromLatin1(op), path, QString::number(code)));
}

constexpr qint64 kSecsFrom1601To1970 = 11'644'473'600;
constexpr qint64 kTicksPerSecond = 10'000'000; // FILETIME counts 100 ns
#endif

} // namespace

#ifdef _WIN32
Result<void> setFileMTime(const QString& path, qint64 secs) {
    // FILE_WRITE_ATTRIBUTES only: the file is never opened for data; BACKUP_SEMANTICS lets a directory work
    // too.
    const HANDLE handle = ::CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()), FILE_WRITE_ATTRIBUTES,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return ioFail(path, "CreateFileW", ::GetLastError());
    }
    const auto ticks = static_cast<std::uint64_t>((secs + kSecsFrom1601To1970) * kTicksPerSecond);
    FILETIME modified{static_cast<DWORD>(ticks & 0xFFFFFFFFU), static_cast<DWORD>(ticks >> 32U)};
    const BOOL ok = ::SetFileTime(handle, nullptr, nullptr, &modified); // atime untouched
    const DWORD code = ::GetLastError();
    ::CloseHandle(handle);
    if (!ok) {
        return ioFail(path, "SetFileTime", code);
    }
    return {};
}

Result<qint64> fileMTime(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        return ioFail(path, "stat", ERROR_FILE_NOT_FOUND);
    }
    return info.lastModified().toSecsSinceEpoch(); // whole seconds, truncated
}
#else
Result<void> setFileMTime(const QString& path, qint64 secs) {
    const QByteArray localPath = QFile::encodeName(path);
    struct timespec times[2];
    times[0].tv_sec = 0;
    times[0].tv_nsec = UTIME_OMIT; // atime: leave untouched
    times[1].tv_sec = secs;
    times[1].tv_nsec = 0;
    if (::utimensat(AT_FDCWD, localPath.constData(), times, 0) != 0) {
        return ioFail(path, "utimensat");
    }
    return {};
}

Result<qint64> fileMTime(const QString& path) {
    const QByteArray localPath = QFile::encodeName(path);
    struct stat st{};
    if (::stat(localPath.constData(), &st) != 0) {
        return ioFail(path, "stat");
    }
    return static_cast<qint64>(st.st_mtim.tv_sec);
}
#endif

} // namespace cg::core::platform
