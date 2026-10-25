// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: the memory layout of the Delphi Extended (x87 80-bit) values stored by t_GeometryPointSML.pas.
//
// Ten bytes, little-endian: bytes 0..7 hold the 64-bit significand with an EXPLICIT integer bit (bit 63),
// bytes 8..9 hold the sign (bit 15) and the 15-bit exponent biased by 16383. Conversion is explicit, never
// through `long double` or a memcpy into a floating type. See docs/handoffs/T-0109.md for what is and is not
// established about the files that real Win32/Win64 builds wrote.

#include <cstdint>

namespace cg::marks {

inline constexpr int kExtended80Size = 10;

struct Extended80Parts {
    bool negative = false;
    std::uint16_t exponent = 0;    // biased, 0..0x7FFF
    std::uint64_t significand = 0; // bit 63 is the explicit integer bit
};

[[nodiscard]] Extended80Parts unpackExtended80(const std::uint8_t* bytes10) noexcept;

// True for a NaN and also for every encoding the x87 reports as "unsupported" (exponent != 0 and integer bit
// clear: unnormals, pseudo-NaN, pseudo-infinity). Legacy IsNan() would say False for the latter but any
// comparison on them raises EInvalidOp; the port folds them into NaN (an "empty" point).
[[nodiscard]] bool extended80IsNaN(const std::uint8_t* bytes10) noexcept;

// Round-to-nearest-even narrowing (the x87 default the Delphi RTL runs with). Overflow gives +-infinity,
// underflow gives a double subnormal or a signed zero. NaN keeps its sign and the top payload bits and is
// made quiet. A pseudo-denormal (exponent 0, integer bit set) is read as exponent 1.
[[nodiscard]] double extended80ToDouble(const std::uint8_t* bytes10) noexcept;

// Exact widening. A double subnormal is normalised; a NaN keeps its sign and payload (made quiet), so the
// default Delphi NaN (sign 1, 0xFFF8...) becomes FF FF C0 00.. exactly.
void doubleToExtended80(double value, std::uint8_t* bytes10) noexcept;

// The fixed byte patterns the legacy writer uses for its separators (u_GeometryToStreamSML.pas:88-92,
// :254-255).
inline constexpr std::uint8_t kExtended80NaN[kExtended80Size] = {0, 0, 0, 0, 0, 0, 0, 0xC0, 0xFF, 0xFF};
inline constexpr std::uint8_t kExtended80MinusOne[kExtended80Size] = {0, 0, 0, 0, 0, 0, 0, 0x80, 0xFF, 0xBF};

} // namespace cg::marks
