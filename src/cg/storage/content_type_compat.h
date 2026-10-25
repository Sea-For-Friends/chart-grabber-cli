// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: IContentTypeInfoBasic.CheckOtherForSaveCompatible (u_ContentTypeInfo.pas:123-157).
//
// Shared by every backend's saveTile: `u_TileStorageFileSystem.pas:SaveTile` and
// `u_TileStorageInRAM.pas:300` both call `FMainContentType.CheckOtherForSaveCompatible(AContentType)`
// before accepting a save with real data, and raise on a mismatch. T-0021F1 review finding 1
// resolved what "compatible" means (same ContentKind + same defaultExt, looked up via the registry,
// not MIME string equality); this header is the one place that rule lives, so a second backend
// reuses it instead of re-deriving it (T-0021F1 review "For F2/F3" note).

#include "cg/core/content_type.h"

#include <QByteArrayView>

namespace cg::storage {

// `mainContentType` is the storage's own (single, fixed) content type; `contentType` is what the
// caller is trying to save. Returns false when `contentType` is not a registered MIME type, or its
// kind/defaultExt differ from `mainContentType`'s.
[[nodiscard]] inline bool contentTypeCompatibleForSave(const core::ContentTypeInfo& mainContentType,
                                                       QByteArrayView contentType) {
    const core::ContentTypeInfo* other = core::ContentTypeRegistry::standard().infoByType(contentType);
    return other != nullptr && other->kind == mainContentType.kind &&
           other->defaultExt == mainContentType.defaultExt;
}

} // namespace cg::storage
