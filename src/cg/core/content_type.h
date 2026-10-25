// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ContentTypeInfo.pas, i_ContentTypeManager.pas, u_ContentTypeInfo.pas,
//        u_ContentTypeListByKey.pas, u_ContentTypeManagerBase.pas, and the registration tables of
//        u_ContentTypeManagerSimple.pas
//
// The MIME <-> extension registry. Keys are matched case-insensitively (ASCII); extension keys
// include the leading dot; there is no fallback content type -- an unknown type or extension is
// nullptr, and callers must handle that.

#include "cg/core/result.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QStringView>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace cg::core {

enum class ContentKind : std::uint8_t { Bitmap, Vector };

struct ContentTypeInfo {
    QByteArray type;       // canonical MIME, e.g. "image/jpg"
    QByteArray defaultExt; // WITH the leading dot, e.g. ".jpg"
    ContentKind kind = ContentKind::Bitmap;
};

class ContentTypeRegistry {
public:
    ContentTypeRegistry() = default;

    // The built-in table of core.md 2.21.1, built once. Thread-safe to share; immutable after
    // the caller stops mutating it (the shared standard() instance is never mutated after build).
    [[nodiscard]] static const ContentTypeRegistry& standard();

    // Registration, for tests and for a future user-extensible table. Duplicate keys are an error
    // (legacy TContentTypeListByKey uses Duplicates := dupError).
    [[nodiscard]] Result<void> addByType(const QByteArray& mimeKey, ContentTypeInfo info);
    [[nodiscard]] Result<void> addByExt(const QByteArray& extKey, ContentTypeInfo info);

    // nullptr when unknown. There is NO fallback/"unknown" content type in legacy.
    [[nodiscard]] const ContentTypeInfo* infoByType(QByteArrayView mime) const;
    [[nodiscard]] const ContentTypeInfo* infoByExt(QByteArrayView ext) const;
    [[nodiscard]] const ContentTypeInfo* infoByFileName(QStringView fileName) const;

    [[nodiscard]] bool isBitmapType(QByteArrayView mime) const;
    [[nodiscard]] bool isBitmapExt(QByteArrayView ext) const;
    [[nodiscard]] bool isVectorType(QByteArrayView mime) const;
    [[nodiscard]] bool isVectorExt(QByteArrayView ext) const;

    // T-0115 (OQ-14). Ports `IContentTypeManager.GetConverter(source, target) <> nil and
    // converter.GetIsSimpleCopy`, the only converters of the matrix that need no codec and no archive:
    // `u_ContentTypeManagerSimple.pas` ConverterMatrixUpdateSynonyms (:390-424: two registered type keys
    // whose canonical types are equal, a key with itself included) and the three fixed copies of
    // ConverterMatrixUpdateFixed (:305-388: google kml -> wikimapia kml, json <-> geo+json). Both keys are
    // type keys, matched like infoByType; an unknown key is false (legacy Get answers nil). The other
    // converters (bitmap <-> bitmap, kml <-> kmz) are NOT here: they need `render` / an archive writer, and
    // answer false until they are ported. Input data of a simple copy is the output.
    // The maptype lookup ContentConverters (T-0120) supersedes this for the saver.
    [[nodiscard]] bool hasSimpleCopyConverter(QByteArrayView sourceType, QByteArrayView targetType) const;

    // Every registered extension key, sorted. Ports GetKnownExtList.
    [[nodiscard]] QList<QByteArray> knownExtensions() const;

    // Every registered type key, lower-cased, sorted ascending (the key order of legacy TypeList).
    [[nodiscard]] QList<QByteArray> typeKeys() const;

private:
    std::unordered_map<std::string, ContentTypeInfo> byType_;
    std::unordered_map<std::string, ContentTypeInfo> byExt_;
};

} // namespace cg::core
