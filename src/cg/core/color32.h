// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Bitmap32/t_Bitmap32.pas (TColor32), u_Color32.pas (SetAlpha), c_Color32.pas (cl...32),
// and the GR32 inlines Color32(TColor) / WinColor / AlphaComponent that legacy calls through them.
// Feature: FEAT-GEO-037
//
// Color32 is 32-bit ARGB ($AARRGGBB, ADR-0002). TColor is the Windows 0x00BBGGRR integer.

#include <QtGlobal>

namespace cg::core {

using Color32 = quint32;

// Predefined constants: only those a port unit uses (c_Color32.pas:31-119); add the rest on demand.
inline constexpr Color32 clBlack32 = 0xFF000000U;
inline constexpr Color32 clLightGray32 = 0xFFBFBFBFU; // c_Color32.pas:34 (T-0142)
inline constexpr Color32 clWhite32 = 0xFFFFFFFFU;
inline constexpr Color32 clGreen32 = 0xFF007F00U; // c_Color32.pas:37 (T-0142)
inline constexpr Color32 clRed32 = 0xFFFF0000U;
inline constexpr Color32 clYellow32 = 0xFFFFFF00U;
inline constexpr Color32 clBlue32 = 0xFF0000FFU; // c_Color32.pas:45 (T-0142)
inline constexpr Color32 clAqua32 = 0xFF00FFFFU;
inline constexpr Color32 clDarkBlue32 = 0xFF00008BU;  // c_Color32.pas:66 (T-0142)
inline constexpr Color32 clDarkCyan32 = 0xFF008B8BU;  // c_Color32.pas:67 (T-0142)
inline constexpr Color32 clDarkGray32 = 0xFFA9A9A9U;  // c_Color32.pas:69 (T-0142)
inline constexpr Color32 clDarkGreen32 = 0xFF006400U; // c_Color32.pas:70 (T-0142)
inline constexpr Color32 clDarkMagenta32 = 0xFF8B008BU;
inline constexpr Color32 clDarkRed32 = 0xFF8B0000U; // c_Color32.pas:77 (T-0142)
inline constexpr Color32 clLightYellow32 = 0xFFFFFFE0U;

// GR32 Color32(WinColor: TColor): swap R and B, alpha $FF. The Windows system-colour branch of GR32
// (negative TColor -> GetSysColor) has no equivalent here: the low 24 bits are used as they are.
[[nodiscard]] constexpr Color32 color32FromWinColor(qint32 winColor) noexcept {
    const auto c = static_cast<quint32>(winColor);
    return 0xFF000000U | ((c & 0xFFU) << 16) | (c & 0xFF00U) | ((c >> 16) & 0xFFU);
}

// GR32 WinColor(Color32): swap R and B, drop alpha.
[[nodiscard]] constexpr qint32 winColor(Color32 color) noexcept {
    return static_cast<qint32>(((color & 0xFFU) << 16) | (color & 0xFF00U) | ((color >> 16) & 0xFFU));
}

[[nodiscard]] constexpr qint32 alphaComponent(Color32 color) noexcept {
    return static_cast<qint32>(color >> 24);
}

// u_Color32.SetAlpha: NewAlpha is clamped to 0..255.
[[nodiscard]] constexpr Color32 setAlpha(Color32 color, qint32 newAlpha) noexcept {
    const quint32 a = newAlpha < 0 ? 0U : (newAlpha > 255 ? 255U : static_cast<quint32>(newAlpha));
    return (color & 0x00FFFFFFU) | (a << 24);
}

} // namespace cg::core
