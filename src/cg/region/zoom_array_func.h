// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/u_ZoomArrayFunc.pas
//
// The zoom list of a download region: a sorted array of zoom bytes, and its text form ("1-3,5").
// Callers keep the array 0-based; the text form is 1-based (the legacy session file and captions),
// so zoomArrayFromStr returns values as written and the caller subtracts one.

#include <QString>
#include <QtGlobal>

#include <vector>

namespace cg::region {

[[nodiscard]] bool isSortedZoomArray(const std::vector<quint8>& array);
void sortZoomArray(std::vector<quint8>& array);
[[nodiscard]] bool zoomArrayFromStr(const QString& str, std::vector<quint8>& out);
[[nodiscard]] QString zoomArrayToStr(const std::vector<quint8>& zoomArr);
[[nodiscard]] bool isZoomInZoomArray(quint8 zoom, const std::vector<quint8>& zoomArr);

} // namespace cg::region
