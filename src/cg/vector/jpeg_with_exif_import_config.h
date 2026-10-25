// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_JpegWithExifImportConfig.pas (IJpegWithExifImportConfig, no separate interface class here),
// u_JpegWithExifImportConfig.pas (TJpegWithExifImportConfig).
//
// The import settings of the JPEG-with-EXIF importer: the common import config plus two values of its own
// (use the embedded thumbnail as the icon, resolution limit). Line and poly params are null, as in legacy.

#include "cg/vector/import_config.h"

namespace cg::vector {

// Ports TJpegWithExifImportConfig (u_JpegWithExifImportConfig.pas:33-82). Derives from the landed
// ImportConfig, which legacy TImportConfig also is. The importer reaches the extra values with
// dynamic_pointer_cast.
class JpegWithExifImportConfig final : public ImportConfig {
public:
    JpegWithExifImportConfig(CategoryPtr rootCategory, ImportCategoryParamsPtr categoryParams,
                             ImportPointParamsPtr pointParams, bool useThumbnailAsIcon, int resolutionLimit);

    [[nodiscard]] bool useThumbnailAsIcon() const;
    [[nodiscard]] int resolutionLimit() const;

private:
    bool useThumbnailAsIcon_;
    int resolutionLimit_;
};

} // namespace cg::vector
