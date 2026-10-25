// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/ContentType/u_ContentTypeManagerSimple.pas:46-97, 577-762 (TContentTypeManagerBitmap: the key
// lists, InitLists, GetBitmapLoaderByFileName, GetIsBitmapType, GetIsBitmapExt) and
// Src/ContentType/u_ContentTypeInfo.pas:49-66, 109-143 (TContentTypeInfoBitmap), docs/specs/bitmap.md §2.6.
// Not ported here: ConverterMatrixUpdateBitmaps (u_ContentTypeManagerSimple.pas:539-575, T-0120; typeKeys()
// serves it), the performance counter lists (:603-604), CheckOtherForSaveCompatible (already in
// storage/content_type_compat.h).

#include "cg/bitmap/bitmap_tile_save_load.h"
#include "cg/bitmap/bitmap_tile_save_load_factory.h"

#include <QByteArray>
#include <QList>
#include <QStringView>

#include <map>
#include <memory>

namespace cg::bitmap {

// TContentTypeInfoBitmap: one row of a key list. The loader and the saver objects are shared by every key of
// a group.
struct ContentTypeInfoBitmap {
    QByteArray type;       // canonical MIME type, e.g. "image/jpg"
    QByteArray defaultExt; // with the dot; ".jpeg" for the second JPEG extension key
    BitmapTileLoaderPtr loader;
    BitmapTileSaverPtr saver; // null for ico
};

// FEAT-STO-033
class ContentTypeManagerBitmap final { // TContentTypeManagerBitmap
public:
    explicit ContentTypeManagerBitmap(const std::shared_ptr<const IBitmapTileSaveLoadFactory>& factory);

    // Keys are matched ASCII case-insensitively (TContentTypeListByKey, as core::ContentTypeRegistry).
    [[nodiscard]] const ContentTypeInfoBitmap* infoByType(QByteArrayView mime) const; // FBitmapTypeList.Get
    [[nodiscard]] const ContentTypeInfoBitmap* infoByExt(QByteArrayView ext) const;   // FBitmapExtList.Get
    [[nodiscard]] bool isBitmapType(QByteArrayView mime) const;                       // GetIsBitmapType
    [[nodiscard]] bool isBitmapExt(QByteArrayView ext) const;                         // GetIsBitmapExt
    [[nodiscard]] BitmapTileLoaderPtr
    bitmapLoaderByFileName(QStringView fileName) const; // GetBitmapLoaderByFileName
    [[nodiscard]] QList<QByteArray> typeKeys() const;   // the keys of FBitmapTypeList, in key order

private:
    void initLists(const IBitmapTileSaveLoadFactory& factory);              // InitLists
    void addByType(const ContentTypeInfoBitmap& info, QByteArrayView type); // AddByType
    void addByExt(const ContentTypeInfoBitmap& info);                       // AddByExt, keyed by defaultExt

    std::map<QByteArray, ContentTypeInfoBitmap> infoByType_; // FBitmapTypeList
    std::map<QByteArray, ContentTypeInfoBitmap> infoByExt_;  // FBitmapExtList
};

} // namespace cg::bitmap
