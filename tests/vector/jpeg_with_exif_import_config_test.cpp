// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_JpegWithExifImportConfig.pas (T-0151, V10).
#include "cg/vector/appearance_of_mark_point.h"
#include "cg/vector/category.h"
#include "cg/vector/import_config.h"
#include "cg/vector/jpeg_with_exif_import_config.h"

#include <gtest/gtest.h>

#include <memory>

using namespace cg::vector;

// :55-72: the three given objects by identity, line and poly params null, the two own values.
TEST(JpegWithExifImportConfig, KeepsArgumentsAndNullLinePolyParams) {
    const auto category = std::make_shared<const Category>(QStringLiteral("root"));
    const auto categoryParams = std::make_shared<const ImportCategoryParams>(true, false, true, false);
    const auto appearance = std::make_shared<AppearanceOfMarkPoint>(1, 2, 3, 0xFF000001U, 0xFF000002U, 12, 32,
                                                                    QStringLiteral("p.png"), nullptr);
    const auto pointParams =
        std::make_shared<const ImportPointParams>(appearance, true, false, false, false, false);

    const JpegWithExifImportConfig a(category, categoryParams, pointParams, true, 640);
    EXPECT_EQ(a.rootCategory(), category);
    EXPECT_EQ(a.categoryParams(), categoryParams);
    EXPECT_EQ(a.pointParams(), pointParams);
    EXPECT_EQ(a.lineParams(), nullptr);
    EXPECT_EQ(a.polyParams(), nullptr);
    EXPECT_TRUE(a.useThumbnailAsIcon());
    EXPECT_EQ(a.resolutionLimit(), 640);

    const JpegWithExifImportConfig b(category, categoryParams, pointParams, false, 0);
    EXPECT_FALSE(b.useThumbnailAsIcon());
    EXPECT_EQ(b.resolutionLimit(), 0);
}
