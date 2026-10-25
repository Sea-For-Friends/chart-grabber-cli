// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/Export/MBTiles/fr_ExportMBTiles.pas -- the headless half: GetMBTilesContentType
// :208-228, GetAllowExport :321-324, GetIsLayer :346-352, GetMapType :359-368, GetDirectTilesCopy :375-418,
// GetBitmapTileSaver :478-515 (the format half). The frame's widgets are the ui module's.
//
// ExportMbtilesParams is the state of the frame's controls. Stage 0 has no image-format selector input:
// the automatic format is the only one, the recompose-only formats are absent (FEAT-REG-069 held open).
// docs/specs/region-export.md 2.7.

#include "cg/core/content_type.h"
#include "cg/maptype/map_type.h"

#include <QByteArrayView>
#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <vector>

namespace cg::region {

struct ExportMbtilesParams {
    maptype::IMapTypePtr map;               // FfrMapSelect selection (may be null)
    maptype::IMapTypePtr overlay;           // FfrOverlaySelect selection (may be null)
    QString path;                           // edtTargetFile text as typed; trimmed when read (:370-373)
    std::vector<quint8> zooms;              // FfrZoomsSelect.GetZoomList
    QString name;                           // edtName text as typed; trimmed when read (:331-334)
    QString description;                    // edtDesc text as typed; trimmed when read (:336-339)
    QString attribution;                    // edtAttr text as typed; trimmed when read (:341-344)
    bool forceDropTarget = true;            // chkForceDropTarget, Checked = True (dfm:105)
    bool replaceExistingTiles = true;       // chkReplaceExistingTiles, Checked = True (dfm:125)
    bool useXYZScheme = false;              // chkUseXYZScheme, unchecked (dfm:147-155)
    bool makeTileMillCompatibility = false; // chkMakeTileMillStruct, unchecked (dfm:109-117)
    // The four re-encoding switches of :377-384: any of them set makes the direct copy impossible.
    bool addVisibleLayers = false;   // chkAddVisibleLayers
    bool addVisibleOverlays = false; // chkAddVisibleOverlays
    bool useRecolor = false;         // chkUseRecolor
    bool usePrevZoom = false;        // chkUsePrevZoom
};

enum class MbtilesContentType : std::uint8_t { Unknown, Png, Jpg, Webp }; // TMBTilesContentType :208

// :210-228, the order Jpeg, Png, Webp.
[[nodiscard]] MbtilesContentType mbtilesContentType(QByteArrayView contentType);
[[nodiscard]] bool allowExport(const maptype::IMapType& map);             // GetAllowExport :321-324
[[nodiscard]] bool isLayer(const ExportMbtilesParams& p);                 // GetIsLayer :346-352
[[nodiscard]] maptype::IMapTypePtr mapType(const ExportMbtilesParams& p); // GetMapType :359-368
[[nodiscard]] bool directTilesCopy(const ExportMbtilesParams& p);         // GetDirectTilesCopy :375-418
[[nodiscard]] QString imgFormat(const ExportMbtilesParams& p);            // GetBitmapTileSaver :478-515

} // namespace cg::region
