// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/platform/thread_os.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

namespace cg::core::platform {

#ifdef _WIN32
void setCurrentThreadName(const QString& name) noexcept {
    // T-0335: SetThreadDescription exists from Windows 10 1607; resolved at run time, absent = no name.
    using SetDescription = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const auto setDescription = reinterpret_cast<SetDescription>(reinterpret_cast<void (*)()>(
        ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
    if (setDescription != nullptr) {
        const QString shortName = name.left(15);
        setDescription(::GetCurrentThread(), reinterpret_cast<PCWSTR>(shortName.utf16()));
    }
}

long currentThreadId() noexcept {
    return static_cast<long>(::GetCurrentThreadId());
}
#else
void setCurrentThreadName(const QString& name) noexcept {
    const QByteArray bytes = name.toUtf8().left(15);
    ::pthread_setname_np(::pthread_self(), bytes.constData());
}

long currentThreadId() noexcept {
    return static_cast<long>(::gettid());
}
#endif

} // namespace cg::core::platform
