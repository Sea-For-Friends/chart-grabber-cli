// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing (T-0335, Windows port). Used by tests only: the hygiene rule keeps every platform #ifdef
// under core/platform/, so the few POSIX-or-Windows calls the tests need live here (header-only, no library
// code).

#include <QFile>
#include <QFileInfo>
#include <QString>

#include <filesystem>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
// after windows.h
#include <tlhelp32.h>
#else
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace cg::core::platform {

// True when QFile::setPermissions on a folder does not stop file creation in it: every Windows user (the
// read-only attribute of a folder is not enforced), and root on POSIX (it ignores the mode).
[[nodiscard]] inline bool directoryPermissionsIgnored() {
#ifdef _WIN32
    return true;
#else
    return ::geteuid() == 0;
#endif
}

// Number of threads of this process (T-0340): the entries of /proc/self/task on Linux, the thread snapshot of
// the toolhelp library filtered by the process id on Windows. -1 when it cannot be read.
[[nodiscard]] inline int processThreadCount() {
#ifdef _WIN32
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return -1;
    }
    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    int count = 0;
    const DWORD self = ::GetCurrentProcessId();
    for (BOOL ok = ::Thread32First(snapshot, &entry); ok; ok = ::Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == self) {
            ++count;
        }
    }
    ::CloseHandle(snapshot);
    return count;
#else
    std::error_code ec;
    int count = 0;
    for ([[maybe_unused]] const auto& e : std::filesystem::directory_iterator("/proc/self/task", ec)) {
        ++count;
    }
    return ec ? -1 : count;
#endif
}

// True when two names that differ only in case are two different entries in `dir` (probed by creating one
// file and looking for it under the other spelling; no operating-system test). A Windows NTFS folder and a
// default macOS volume answer false, a Linux ext4 folder true. `dir` must exist and be writable.
[[nodiscard]] inline bool directoryIsCaseSensitive(const QString& dir) {
    const QString lower = dir + QStringLiteral("/cg-case-probe.tmp");
    const QString upper = dir + QStringLiteral("/CG-CASE-PROBE.TMP");
    QFile probe(lower);
    if (!probe.open(QIODevice::WriteOnly)) {
        return true;
    }
    probe.close();
    const bool distinct = !QFileInfo::exists(upper);
    QFile::remove(lower);
    return distinct;
}

// pipe(2) for both systems: an anonymous pipe (what a plugin hands over as stdin). 0 on success.
[[nodiscard]] inline int makePipe(int* fds) {
#ifdef _WIN32
    return _pipe(fds, 65536, _O_BINARY);
#else
    return ::pipe(fds);
#endif
}

// Waits for an entry to be created in a directory: inotify IN_CREATE on Linux, FindFirstChangeNotification on
// Windows.
class DirectoryWatch {
public:
    explicit DirectoryWatch(const QString& dir) {
#ifdef _WIN32
        handle_ = ::FindFirstChangeNotificationW(reinterpret_cast<const wchar_t*>(dir.utf16()), FALSE,
                                                 FILE_NOTIFY_CHANGE_FILE_NAME);
#else
        fd_ = ::inotify_init1(IN_CLOEXEC);
        if (fd_ >= 0 && ::inotify_add_watch(fd_, dir.toLocal8Bit().constData(), IN_CREATE) < 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }
    ~DirectoryWatch() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::FindCloseChangeNotification(handle_);
        }
#else
        if (fd_ >= 0) {
            ::close(fd_);
        }
#endif
    }
    DirectoryWatch(const DirectoryWatch&) = delete;
    DirectoryWatch& operator=(const DirectoryWatch&) = delete;

    [[nodiscard]] bool valid() const {
#ifdef _WIN32
        return handle_ != INVALID_HANDLE_VALUE;
#else
        return fd_ >= 0;
#endif
    }
    // True when an entry was created within `timeoutMs`.
    [[nodiscard]] bool waitForCreate(int timeoutMs) const {
#ifdef _WIN32
        return ::WaitForSingleObject(handle_, static_cast<DWORD>(timeoutMs)) == WAIT_OBJECT_0;
#else
        pollfd waiting{fd_, POLLIN, 0};
        return ::poll(&waiting, 1, timeoutMs) > 0;
#endif
    }

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

} // namespace cg::core::platform
