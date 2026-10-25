// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/sml/extended80.h"

#include <bit>
#include <cstring>

namespace cg::marks {

namespace {

constexpr std::uint16_t kInfExp = 0x7FFF;
constexpr std::uint64_t kIntegerBit = 1ULL << 63;
constexpr std::uint64_t kDoubleFracMask = (1ULL << 52) - 1;

} // namespace

Extended80Parts unpackExtended80(const std::uint8_t* b) noexcept {
    Extended80Parts p;
    for (int i = 7; i >= 0; --i) {
        p.significand = (p.significand << 8) | b[i];
    }
    const unsigned hi = static_cast<unsigned>(b[8]) | (static_cast<unsigned>(b[9]) << 8);
    p.negative = (hi & 0x8000U) != 0;
    p.exponent = static_cast<std::uint16_t>(hi & 0x7FFFU);
    return p;
}

bool extended80IsNaN(const std::uint8_t* bytes10) noexcept {
    const Extended80Parts p = unpackExtended80(bytes10);
    if (p.exponent == 0) {
        return false; // zero, denormal, pseudo-denormal
    }
    if ((p.significand & kIntegerBit) == 0) {
        return true; // unsupported encoding
    }
    return p.exponent == kInfExp && (p.significand << 1) != 0;
}

double extended80ToDouble(const std::uint8_t* bytes10) noexcept {
    const Extended80Parts p = unpackExtended80(bytes10);
    const std::uint64_t signBit = p.negative ? (1ULL << 63) : 0;
    std::uint64_t bits = 0;
    if (extended80IsNaN(bytes10)) {
        const std::uint64_t frac = (p.significand << 1) >> 12; // the 52 top fraction bits
        bits = signBit | (0x7FFULL << 52) | (1ULL << 51) | frac;
    } else if (p.exponent == kInfExp) {
        bits = signBit | (0x7FFULL << 52);
    } else if (p.significand == 0) {
        bits = signBit;
    } else {
        const int lz = std::countl_zero(p.significand);
        const std::uint64_t sig = p.significand << lz; // integer bit now at 63
        const int effExp = p.exponent == 0 ? 1 : p.exponent;
        const int e = effExp - 16383 - lz; // value = sig / 2^63 * 2^e
        std::uint64_t base = 0;
        int shift = 0; // how many low bits of sig are dropped
        if (e >= -1022) {
            shift = 11;
            base = static_cast<std::uint64_t>(e + 1022) << 52; // the implicit bit of q is added below
        } else {
            shift = 11 + (-1022 - e);
        }
        std::uint64_t q = 0;
        if (shift > 64) {
            q = 0;
        } else if (shift == 64) {
            q = sig > kIntegerBit ? 1 : 0; // exactly one half rounds to even (0)
        } else {
            q = sig >> shift;
            const std::uint64_t rem = sig & ((1ULL << shift) - 1);
            const std::uint64_t half = 1ULL << (shift - 1);
            if (rem > half || (rem == half && (q & 1U) != 0)) {
                ++q;
            }
        }
        bits = base + q;
        if (e > 1023 || bits >= (0x7FFULL << 52)) {
            bits = (0x7FFULL << 52);
        }
        bits |= signBit;
    }
    double d = 0;
    std::memcpy(&d, &bits, sizeof d);
    return d;
}

void doubleToExtended80(double value, std::uint8_t* bytes10) noexcept {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    const bool negative = (bits >> 63) != 0;
    const unsigned expField = static_cast<unsigned>((bits >> 52) & 0x7FF);
    const std::uint64_t frac = bits & kDoubleFracMask;
    std::uint64_t sig = 0;
    unsigned exp = 0;
    if (expField == 0x7FF) {
        exp = kInfExp;
        sig = kIntegerBit | (frac << 11);
        if (frac != 0) {
            sig |= 1ULL << 62; // quiet
        }
    } else if (expField == 0) {
        if (frac != 0) {
            const int lz = std::countl_zero(frac);
            sig = frac << lz;
            exp = static_cast<unsigned>(63 - lz - 1074 + 16383);
        }
    } else {
        sig = kIntegerBit | (frac << 11);
        exp = expField - 1023 + 16383;
    }
    for (int i = 0; i < 8; ++i) {
        bytes10[i] = static_cast<std::uint8_t>(sig >> (8 * i));
    }
    const unsigned hi = exp | (negative ? 0x8000U : 0U);
    bytes10[8] = static_cast<std::uint8_t>(hi & 0xFFU);
    bytes10[9] = static_cast<std::uint8_t>(hi >> 8);
}

} // namespace cg::marks
