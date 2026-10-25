// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/version.h"

#include <QtGlobal>

#ifndef CG_GIT_HASH
#define CG_GIT_HASH                                                                                          \
    "unknown" // set by CMake (src/cg/core/CMakeLists.txt); the nocmake fallback has no git step
#endif

#if !defined(CG_PROJECT_VERSION) && defined(CG_PROJECT_VERSION_RAW)
#define CG_STR2(x) #x
#define CG_STR(x) CG_STR2(x)
#define CG_PROJECT_VERSION CG_STR(CG_PROJECT_VERSION_RAW) // nocmake: the quote-free spelling
#endif
#ifndef CG_PROJECT_VERSION
#error "CG_PROJECT_VERSION must be set by the build (CMake PROJECT_VERSION; tools/nocmake_build.sh)"
#endif

namespace cg::core {

const char* versionString() {
    return CG_PROJECT_VERSION;
}

namespace {

#if defined(__clang__)
constexpr const char* kCompiler = "Clang " __VERSION__;
#elif defined(__GNUC__)
constexpr const char* kCompiler = "GCC " __VERSION__;
#else
constexpr const char* kCompiler = "unknown compiler";
#endif

} // namespace

const BuildInfo& buildInfo() {
    static const BuildInfo info{versionString(), CG_GIT_HASH, __DATE__ " " __TIME__, kCompiler,
                                QT_VERSION_STR};
    return info;
}

} // namespace cg::core
