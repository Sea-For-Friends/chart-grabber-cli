// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: build smoke test (no FEAT); T-0002B extension: BuildInfo (docs/specs/core.md 2.23)
#include "cg/core/version.h"

#include <gtest/gtest.h>

#if !defined(CG_PROJECT_VERSION) && defined(CG_PROJECT_VERSION_RAW)
#define CG_STR2(x) #x
#define CG_STR(x) CG_STR2(x)
#define CG_PROJECT_VERSION CG_STR(CG_PROJECT_VERSION_RAW) // nocmake: the quote-free spelling
#endif
TEST(Version, IsNonEmpty) {
    EXPECT_STRNE(cg::core::versionString(), "");
}

TEST(Version, BuildInfoFieldsAreNonEmpty) {
    const cg::core::BuildInfo& info = cg::core::buildInfo();
    EXPECT_STREQ(info.version, cg::core::versionString());
    EXPECT_STRNE(info.gitHash, "");
    EXPECT_STRNE(info.buildDate, "");
    EXPECT_STRNE(info.compiler, "");
    EXPECT_STRNE(info.qtVersion, "");
}

// T-0328: one source of truth -- the string is the CMake PROJECT_VERSION, not a second hand-kept copy.
TEST(Version, EqualsProjectVersion) {
    EXPECT_STREQ(cg::core::versionString(), CG_PROJECT_VERSION);
}
