// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Shared by the bitmap tests whose error path leaves a C library through longjmp (T-0160 JPEG, T-0161 PNG).
// The GCC ThreadSanitizer runtime aborts with "can't find longjmp buf" on it
// (tsan_interceptors_posix.cpp:582), so these tests skip under --tsan; the ASan / UBSan build and the plain
// build run them.

#include <gtest/gtest.h>

#if defined(__SANITIZE_THREAD__)
#define CG_SKIP_LONGJMP_UNDER_TSAN()                                                                         \
    GTEST_SKIP() << "ThreadSanitizer cannot follow the longjmp of the error path"
#else
#define CG_SKIP_LONGJMP_UNDER_TSAN() static_cast<void>(0)
#endif
