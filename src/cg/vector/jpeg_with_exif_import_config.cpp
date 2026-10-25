// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/jpeg_with_exif_import_config.h"

#include <utility>

// Ports u_JpegWithExifImportConfig.pas (V10): constructor :55-72 (the two nil arguments of the inherited
// constructor are :67-68), getters :74-82.

namespace cg::vector {

JpegWithExifImportConfig::JpegWithExifImportConfig(CategoryPtr rootCategory,
                                                   ImportCategoryParamsPtr categoryParams,
                                                   ImportPointParamsPtr pointParams, bool useThumbnailAsIcon,
                                                   int resolutionLimit)
    : ImportConfig(std::move(rootCategory), std::move(categoryParams), std::move(pointParams), nullptr,
                   nullptr),
      useThumbnailAsIcon_(useThumbnailAsIcon), resolutionLimit_(resolutionLimit) {}

bool JpegWithExifImportConfig::useThumbnailAsIcon() const {
    return useThumbnailAsIcon_;
}

int JpegWithExifImportConfig::resolutionLimit() const {
    return resolutionLimit_;
}

} // namespace cg::vector
