// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/RegionProcess/Export/MBTiles/fr_ExportMBTiles.pas (headless half, see export_mbtiles.h)
#include "cg/region/export_mbtiles.h"

#include "cg/core/content_type_func.h"
#include "cg/core/delphi_text.h"
#include "cg/geo/projection.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"

#include <QString>

namespace cg::region {

namespace {

// GetMBTilesContentType of a map's type. The null branch is unreachable in legacy (fr_ExportMBTiles.pas:390
// dereferences the type unguarded; the MapType constructor throws first, map_type.cpp:82); no register row.
MbtilesContentType contentTypeOf(const core::ContentTypeInfo* info) {
    return info != nullptr ? mbtilesContentType(info->type) : MbtilesContentType::Unknown;
}

// _IsValidMap :386-395
bool isValidMap(const maptype::IMapType& map) {
    const MbtilesContentType type = contentTypeOf(map.contentType());
    return map.isBitmapTiles() &&
           (type == MbtilesContentType::Png || type == MbtilesContentType::Jpg ||
            type == MbtilesContentType::Webp) &&
           map.projectionSet()->zoom(0)->projectionType()->projectionEpsg() == geo::kProjEpsgGoogle;
}

} // namespace

MbtilesContentType mbtilesContentType(QByteArrayView contentType) {
    if (core::isJpegContentType(contentType)) {
        return MbtilesContentType::Jpg;
    }
    if (core::isPngContentType(contentType)) {
        return MbtilesContentType::Png;
    }
    if (core::isWebpContentType(contentType)) {
        return MbtilesContentType::Webp;
    }
    return MbtilesContentType::Unknown;
}

bool allowExport(const maptype::IMapType& map) {
    return map.isBitmapTiles();
}

bool isLayer(const ExportMbtilesParams& p) {
    return !p.map;
}

maptype::IMapTypePtr mapType(const ExportMbtilesParams& p) {
    return p.map ? p.map : p.overlay;
}

bool directTilesCopy(const ExportMbtilesParams& p) {
    // _IsTileReencodingRequired :377-384
    if (p.addVisibleLayers || p.addVisibleOverlays || p.useRecolor || p.usePrevZoom) {
        return false;
    }
    // :410-415: exactly one of the map and the overlay is selected.
    // :417 (the format is automatic) always holds in stage 0: there is no format input.
    if (p.map && !p.overlay) {
        return isValidMap(*p.map);
    }
    if (!p.map && p.overlay) {
        return isValidMap(*p.overlay);
    }
    return false;
}

QString imgFormat(const ExportMbtilesParams& p) {
    // GetBitmapTileSaver :478-515 with the automatic format. A saver exists only for a bitmap type that the
    // frame lists (fr_ImageFormatSelect.pas:300-328 with the list of fr_ExportMBTiles.pas:272-276): JPEG,
    // PNG, WebP. The raise at :503 is unreachable with the automatic format: the format then follows the
    // type.
    const maptype::IMapTypePtr map = mapType(p);
    const core::ContentTypeInfo* info = map ? map->contentType() : nullptr;
    if (info != nullptr && info->kind == core::ContentKind::Bitmap) {
        switch (mbtilesContentType(info->type)) {
        case MbtilesContentType::Jpg:
            return QStringLiteral("jpg");
        case MbtilesContentType::Png:
            return QStringLiteral("png");
        case MbtilesContentType::Webp:
            return QStringLiteral("webp");
        case MbtilesContentType::Unknown:
            break;
        }
    }
    // Saver-less branch :505-513: jpg for a map, png for an overlay or no selection.
    return p.map ? QStringLiteral("jpg") : QStringLiteral("png");
}

} // namespace cg::region
