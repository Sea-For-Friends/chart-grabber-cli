// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: see content_type_manager_bitmap.h (u_ContentTypeManagerSimple.pas:579-762,
// u_ContentTypeInfo.pas:111-140).

#include "cg/bitmap/content_type_manager_bitmap.h"

#include <QFileInfo>
#include <QString>
#include <QStringView>

#include <stdexcept>
#include <utility>

namespace cg::bitmap {

namespace {

// TContentTypeInfoBitmap.Create (u_ContentTypeInfo.pas:111-121).
ContentTypeInfoBitmap makeInfo(QByteArrayView type, QByteArrayView ext, BitmapTileLoaderPtr loader,
                               BitmapTileSaverPtr saver) {
    return ContentTypeInfoBitmap{type.toByteArray(), ext.toByteArray(), std::move(loader), std::move(saver)};
}

// TContentTypeListByKey.Add with Duplicates := dupError: a duplicate key is a programming error.
template <typename Table>
void addUnique(Table& table, QByteArrayView key, const ContentTypeInfoBitmap& info) {
    if (!table.emplace(key.toByteArray(), info).second) {
        throw std::logic_error("ContentTypeManagerBitmap: duplicate key " + key.toByteArray().toStdString());
    }
}

// TContentTypeListByKey.Get: the key is lower-cased (ASCII only), so a non-ASCII key matches nothing.
const ContentTypeInfoBitmap* findInfo(const std::map<QByteArray, ContentTypeInfoBitmap>& table,
                                      QByteArrayView key) {
    const auto it = table.find(key.toByteArray().toLower());
    return it == table.end() ? nullptr : &it->second;
}

} // namespace

ContentTypeManagerBitmap::ContentTypeManagerBitmap(
    const std::shared_ptr<const IBitmapTileSaveLoadFactory>& factory) {
    // unreachable in legacy (u_GlobalState.pas:573 passes the factory it just built); no register row
    if (!factory) {
        throw std::invalid_argument("ContentTypeManagerBitmap: null factory");
    }
    initLists(*factory);
}

const ContentTypeInfoBitmap* ContentTypeManagerBitmap::infoByType(QByteArrayView mime) const {
    return findInfo(infoByType_, mime);
}

const ContentTypeInfoBitmap* ContentTypeManagerBitmap::infoByExt(QByteArrayView ext) const {
    return findInfo(infoByExt_, ext);
}

bool ContentTypeManagerBitmap::isBitmapType(QByteArrayView mime) const {
    return infoByType(mime) != nullptr;
}

bool ContentTypeManagerBitmap::isBitmapExt(QByteArrayView ext) const {
    return infoByExt(ext) != nullptr;
}

BitmapTileLoaderPtr ContentTypeManagerBitmap::bitmapLoaderByFileName(QStringView fileName) const {
    // ExtractFileExt (u_ContentTypeManagerSimple.pas:628): the text after the last dot of the file name, the
    // dot kept. A name without a dot has no extension; a non-ASCII extension matches no ASCII key (:629).
    const QString suffix = QFileInfo(fileName.toString()).suffix();
    if (suffix.isEmpty()) {
        return nullptr;
    }
    const ContentTypeInfoBitmap* info = findInfo(infoByExt_, (QStringLiteral(".") + suffix).toUtf8());
    return info != nullptr ? info->loader : nullptr;
}

QList<QByteArray> ContentTypeManagerBitmap::typeKeys() const {
    QList<QByteArray> keys;
    for (const auto& entry : infoByType_) {
        keys.append(entry.first);
    }
    return keys;
}

void ContentTypeManagerBitmap::addByType(const ContentTypeInfoBitmap& info, QByteArrayView type) {
    addUnique(infoByType_, type, info);
}

void ContentTypeManagerBitmap::addByExt(const ContentTypeInfoBitmap& info) {
    addUnique(infoByExt_, info.defaultExt, info);
}

void ContentTypeManagerBitmap::initLists(const IBitmapTileSaveLoadFactory& factory) {
    // jpeg (:685-698): ".jpeg" is a second extension key of the canonical type "image/jpg"
    const auto jpg = makeInfo("image/jpg", ".jpg", factory.createJpegLoader(), factory.createJpegSaver(85));
    addByType(jpg, "image/jpg");
    addByType(jpg, "image/jpeg");
    addByType(jpg, "image/pjpeg");
    addByExt(jpg);
    addByExt(makeInfo("image/jpg", ".jpeg", jpg.loader, jpg.saver));

    // png (:700-709)
    const auto png = makeInfo("image/png", ".png", factory.createPngLoader(),
                              factory.createPngSaver(ImageColorBitPerPix::I32bpp, 2));
    addByType(png, "image/png");
    addByType(png, "image/x-png");
    addByType(png, "image/png; mode=24bit");
    addByExt(png);

    // gif (:711-718)
    const auto gif = makeInfo("image/gif", ".gif", factory.createGifLoader(), factory.createGifSaver());
    addByType(gif, "image/gif");
    addByExt(gif);

    // bmp (:720-729)
    const auto bmp = makeInfo("image/bmp", ".bmp", factory.createBmpLoader(), factory.createBmpSaver());
    addByType(bmp, "image/bmp");
    addByType(bmp, "image/x-ms-bmp");
    addByType(bmp, "image/x-windows-bmp");
    addByExt(bmp);

    // ico (:731-739): loader only
    const auto ico = makeInfo("image/x-icon", ".ico", factory.createIcoLoader(), nullptr);
    addByType(ico, "image/x-icon");
    addByType(ico, "image/vnd.microsoft.icon");
    addByExt(ico);

    // webp (:741-748)
    const auto webp =
        makeInfo("image/webp", ".webp", factory.createWebpLoader(), factory.createWebpSaver(75));
    addByType(webp, "image/webp");
    addByExt(webp);

    // tiff (:750-761): ".tiff" is a second extension key of "image/tiff"
    const auto tif =
        makeInfo("image/tiff", ".tif", factory.createTiffLoader(),
                 factory.createTiffSaver(ImageColorBitPerPix::I32bpp, ImageTiffCompression::Deflate));
    addByType(tif, "image/tiff");
    addByExt(tif);
    addByExt(makeInfo("image/tiff", ".tiff", tif.loader, tif.saver));
}

} // namespace cg::bitmap
