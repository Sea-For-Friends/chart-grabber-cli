// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/content_type.h"

#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <cctype>

namespace cg::core {
namespace {

[[nodiscard]] bool isAscii(QByteArrayView key) {
    return std::all_of(key.cbegin(), key.cend(),
                       [](char ch) { return static_cast<unsigned char>(ch) < 0x80; });
}

[[nodiscard]] std::string toLowerKey(QByteArrayView key) {
    std::string result(key.constData(), static_cast<std::size_t>(key.size()));
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

// Registers into `registry`, asserting success: the standard table below is a fixed, hand-checked
// literal set, so a failure here (duplicate key, non-ASCII) is a typo in this file, not a runtime
// condition to handle.
void registerType(ContentTypeRegistry& registry, const QByteArray& key, const ContentTypeInfo& info) {
    const Result<void> added = registry.addByType(key, info);
    Q_ASSERT(added.has_value());
    Q_UNUSED(added);
}
void registerExt(ContentTypeRegistry& registry, const QByteArray& key, const ContentTypeInfo& info) {
    const Result<void> added = registry.addByExt(key, info);
    Q_ASSERT(added.has_value());
    Q_UNUSED(added);
}

// core.md 2.21.1: the exact table registered by u_ContentTypeManagerSimple.pas. Nothing more,
// nothing less.
ContentTypeRegistry buildStandardRegistry() {
    ContentTypeRegistry registry;

    const auto bitmap = [&registry](const QByteArray& canonicalType, const QByteArray& defaultExt,
                                    std::initializer_list<QByteArray> typeAliases, const QByteArray& extKey) {
        const ContentTypeInfo info{canonicalType, defaultExt, ContentKind::Bitmap};
        for (const QByteArray& alias : typeAliases) {
            registerType(registry, alias, info);
        }
        if (!extKey.isEmpty()) {
            registerExt(registry, extKey, info);
        }
    };

    // B1: image/jpg, .jpg
    bitmap("image/jpg", ".jpg", {"image/jpg", "image/jpeg", "image/pjpeg"}, ".jpg");
    // B2: image/jpg but its OWN .jpeg descriptor (ext-only; legacy really creates a second object).
    registerExt(registry, ".jpeg", ContentTypeInfo{"image/jpg", ".jpeg", ContentKind::Bitmap});
    // B3: image/png, .png
    bitmap("image/png", ".png", {"image/png", "image/x-png", "image/png; mode=24bit"}, ".png");
    // B4: image/gif, .gif
    bitmap("image/gif", ".gif", {"image/gif"}, ".gif");
    // B5: image/bmp, .bmp
    bitmap("image/bmp", ".bmp", {"image/bmp", "image/x-ms-bmp", "image/x-windows-bmp"}, ".bmp");
    // B6: image/x-icon, .ico
    bitmap("image/x-icon", ".ico", {"image/x-icon", "image/vnd.microsoft.icon"}, ".ico");
    // B7: image/webp, .webp
    bitmap("image/webp", ".webp", {"image/webp"}, ".webp");
    // B8: image/tiff, .tif
    bitmap("image/tiff", ".tif", {"image/tiff"}, ".tif");
    // B9: image/tiff but its OWN .tiff descriptor (ext-only).
    registerExt(registry, ".tiff", ContentTypeInfo{"image/tiff", ".tiff", ContentKind::Bitmap});

    const auto vectorTypeOnly = [&registry](const QByteArray& type, const QByteArray& ext) {
        registerType(registry, type, ContentTypeInfo{type, ext, ContentKind::Vector});
    };
    const auto vectorTypeAndExt = [&registry](const QByteArray& type, const QByteArray& ext) {
        const ContentTypeInfo info{type, ext, ContentKind::Vector};
        registerType(registry, type, info);
        registerExt(registry, ext, info);
    };

    // V1/V2: wikimapia KML/KMZ, type-only -- must never win an extension lookup against V3/V4.
    vectorTypeOnly("application/vnd.sas.wikimapia.kml+xml", ".kml");
    vectorTypeOnly("application/vnd.sas.wikimapia.kmz", ".kmz");
    // V3..V8: type and extension both.
    vectorTypeAndExt("application/vnd.google-earth.kml+xml", ".kml");
    vectorTypeAndExt("application/vnd.google-earth.kmz", ".kmz");
    vectorTypeAndExt("application/gpx+xml", ".gpx");
    vectorTypeAndExt("application/json", ".json");
    vectorTypeAndExt("application/geo+json", ".geojson");
    vectorTypeAndExt("application/vnd.sas.wikimapia.txt", ".wikitxt");

    return registry;
}

} // namespace

const ContentTypeRegistry& ContentTypeRegistry::standard() {
    static const ContentTypeRegistry instance = buildStandardRegistry();
    return instance;
}

Result<void> ContentTypeRegistry::addByType(const QByteArray& mimeKey, ContentTypeInfo info) {
    if (!isAscii(mimeKey)) {
        return fail(ErrorCode::ParseError,
                    QStringLiteral("content type is not ASCII: %1").arg(QString::fromLatin1(mimeKey)));
    }
    const std::string key = toLowerKey(mimeKey);
    if (byType_.contains(key)) {
        return fail(ErrorCode::ParseError,
                    QStringLiteral("duplicate content type key: %1").arg(QString::fromLatin1(mimeKey)));
    }
    byType_.emplace(key, std::move(info));
    return {};
}

Result<void> ContentTypeRegistry::addByExt(const QByteArray& extKey, ContentTypeInfo info) {
    if (!isAscii(extKey)) {
        return fail(ErrorCode::ParseError,
                    QStringLiteral("content extension is not ASCII: %1").arg(QString::fromLatin1(extKey)));
    }
    const std::string key = toLowerKey(extKey);
    if (byExt_.contains(key)) {
        return fail(ErrorCode::ParseError,
                    QStringLiteral("duplicate content extension key: %1").arg(QString::fromLatin1(extKey)));
    }
    byExt_.emplace(key, std::move(info));
    return {};
}

const ContentTypeInfo* ContentTypeRegistry::infoByType(QByteArrayView mime) const {
    if (!isAscii(mime)) {
        return nullptr;
    }
    const auto it = byType_.find(toLowerKey(mime));
    return it == byType_.end() ? nullptr : &it->second;
}

const ContentTypeInfo* ContentTypeRegistry::infoByExt(QByteArrayView ext) const {
    if (!isAscii(ext)) {
        return nullptr;
    }
    const auto it = byExt_.find(toLowerKey(ext));
    return it == byExt_.end() ? nullptr : &it->second;
}

const ContentTypeInfo* ContentTypeRegistry::infoByFileName(QStringView fileName) const {
    const QString suffix = QFileInfo(fileName.toString()).suffix();
    if (suffix.isEmpty()) {
        return nullptr;
    }
    return infoByExt(QByteArray(".") + suffix.toUtf8());
}

bool ContentTypeRegistry::isBitmapType(QByteArrayView mime) const {
    return infoByType(mime) && infoByType(mime)->kind == ContentKind::Bitmap;
}
bool ContentTypeRegistry::isBitmapExt(QByteArrayView ext) const {
    return infoByExt(ext) && infoByExt(ext)->kind == ContentKind::Bitmap;
}
bool ContentTypeRegistry::isVectorType(QByteArrayView mime) const {
    return infoByType(mime) && infoByType(mime)->kind == ContentKind::Vector;
}
bool ContentTypeRegistry::isVectorExt(QByteArrayView ext) const {
    return infoByExt(ext) && infoByExt(ext)->kind == ContentKind::Vector;
}

bool ContentTypeRegistry::hasSimpleCopyConverter(QByteArrayView sourceType, QByteArrayView targetType) const {
    const ContentTypeInfo* source = infoByType(sourceType);
    const ContentTypeInfo* target = infoByType(targetType);
    if (source == nullptr || target == nullptr) {
        return false;
    }
    if (source->type ==
        target->type) { // ConverterMatrixUpdateSynonyms :390-424, exact (case-sensitive) compare
        return true;
    }
    // ConverterMatrixUpdateFixed :305-388, the three TContentConverterSimpleCopy entries.
    struct Pair {
        const char* from;
        const char* to;
    };
    static constexpr Pair kFixedCopies[] = {
        {"application/vnd.google-earth.kml+xml", "application/vnd.sas.wikimapia.kml+xml"}, // :316-323
        {"application/json", "application/geo+json"},                                      // :365-375
        {"application/geo+json", "application/json"},                                      // :377-387
    };
    return std::ranges::any_of(
        kFixedCopies, [&](const Pair& pair) { return source->type == pair.from && target->type == pair.to; });
}

QList<QByteArray> ContentTypeRegistry::knownExtensions() const {
    QList<QByteArray> result;
    result.reserve(static_cast<qsizetype>(byExt_.size()));
    for (const auto& [key, info] : byExt_) {
        result.append(QByteArray::fromStdString(key));
    }
    std::sort(result.begin(), result.end());
    return result;
}

QList<QByteArray> ContentTypeRegistry::typeKeys() const {
    QList<QByteArray> result;
    result.reserve(static_cast<qsizetype>(byType_.size()));
    for (const auto& [key, info] : byType_) {
        result.append(QByteArray::fromStdString(key));
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace cg::core
