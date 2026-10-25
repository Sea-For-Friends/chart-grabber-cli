// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Network/u_ContentDetecter.pas:60-148 (TryDetectContentTypeByMagic only;
// TryDetectContentTypeByWinApi is dropped -- no Linux equivalent, and QMimeDatabase would return a
// different, larger vocabulary the expected-MIME check would then reject [D-13]).
// FEAT-NET-011.

#include <QByteArray>
#include <QByteArrayView>

namespace cg::core {

// Returns "" when nothing matched. Magic table is evaluated in a fixed order; an entry matches
// iff `data.size() > n` (strictly greater) and its first `n` bytes compare equal. The RIFF/WEBP
// entry additionally requires bytes 8..11 to be "WEBP" when size > 12; otherwise the scan stops
// and returns "" without trying the remaining entries.
[[nodiscard]] QByteArray detectContentType(QByteArrayView data);

} // namespace cg::core
