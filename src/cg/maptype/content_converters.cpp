// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/content_converters.h"

#include "cg/maptype/content_converter_bitmap.h"

#include <stdexcept>
#include <utility>

namespace cg::maptype {

ContentConverters::ContentConverters(const core::ContentTypeRegistry& types,
                                     const std::shared_ptr<const bitmap::ContentTypeManagerBitmap>& bitmaps) {
    if (!bitmaps) {
        throw std::invalid_argument("ContentConverters: null bitmap content type manager");
    }
    // UpdateConverterMatrix :297-303; the order is the contract.
    updateFixed(types);
    updateSynonyms(types);
    updateFixedWithSynonyms(types);
    updateBitmaps(*bitmaps);
}

ContentConverterPtr ContentConverters::converter(QByteArrayView sourceType, QByteArrayView targetType) const {
    return matrix_.get(sourceType, targetType);
}

void ContentConverters::updateFixed(const core::ContentTypeRegistry& types) {
    const auto copy = [&](QByteArrayView source, QByteArrayView target) {
        // unreachable in legacy (all keys are in the standard registry); no register row
        if (types.infoByType(source) != nullptr && types.infoByType(target) != nullptr) {
            matrix_.add(source, target, std::make_shared<const ContentConverterSimpleCopy>());
        }
    };
    copy("application/vnd.google-earth.kml+xml", "application/vnd.sas.wikimapia.kml+xml"); // :313-323
    // TODO(T-0178): kml -> wikimapia kmz, kmz -> kml, kml -> kmz (u_ContentTypeManagerSimple.pas:325-362)
    copy("application/json", "application/geo+json"); // :365-375
    copy("application/geo+json", "application/json"); // :377-387
}

void ContentConverters::updateSynonyms(const core::ContentTypeRegistry& types) {
    const QList<QByteArray> keys = types.typeKeys();
    for (const QByteArray& sourceName : keys) {
        const core::ContentTypeInfo* source = types.infoByType(sourceName);
        for (const QByteArray& targetName : keys) {
            const core::ContentTypeInfo* target = types.infoByType(targetName);
            if (matrix_.get(sourceName, targetName) == nullptr && source->type == target->type) {
                matrix_.add(sourceName, targetName, std::make_shared<const ContentConverterSimpleCopy>());
            }
        }
    }
}

ContentConverterPtr ContentConverters::findConverterWithSynonyms(const core::ContentTypeRegistry& types,
                                                                 const QByteArray& sourceType,
                                                                 const QByteArray& targetType) const {
    const QList<QByteArray> keys = types.typeKeys();
    for (const QByteArray& sourceName : keys) {
        const ContentConverterPtr toSource = matrix_.get(sourceType, sourceName);
        if (toSource == nullptr || !toSource->isSimpleCopy()) {
            continue;
        }
        for (const QByteArray& targetName : keys) {
            const ContentConverterPtr toTarget = matrix_.get(targetName, targetType);
            if (toTarget != nullptr && toTarget->isSimpleCopy()) {
                if (ContentConverterPtr found = matrix_.get(sourceName, targetName)) {
                    return found;
                }
            }
        }
    }
    return nullptr;
}

void ContentConverters::updateFixedWithSynonyms(const core::ContentTypeRegistry& types) {
    const QList<QByteArray> keys = types.typeKeys();
    for (const QByteArray& sourceName : keys) {
        for (const QByteArray& targetName : keys) {
            if (matrix_.get(sourceName, targetName) == nullptr) {
                if (ContentConverterPtr found = findConverterWithSynonyms(types, sourceName, targetName)) {
                    matrix_.add(sourceName, targetName, std::move(found));
                }
            }
        }
    }
}

void ContentConverters::updateBitmaps(const bitmap::ContentTypeManagerBitmap& bitmaps) {
    const QList<QByteArray> keys = bitmaps.typeKeys();
    for (const QByteArray& sourceName : keys) {
        const bitmap::ContentTypeInfoBitmap* source = bitmaps.infoByType(sourceName);
        for (const QByteArray& targetName : keys) {
            const bitmap::ContentTypeInfoBitmap* target = bitmaps.infoByType(targetName);
            if (matrix_.get(sourceName, targetName) == nullptr && source->loader != nullptr &&
                target->saver != nullptr) {
                matrix_.add(sourceName, targetName,
                            std::make_shared<const ContentConverterBitmap>(source->loader, target->saver));
            }
        }
    }
}

} // namespace cg::maptype
