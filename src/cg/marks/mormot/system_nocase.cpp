// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/system_nocase.h"

#include <QtGlobal>

#include <array>

namespace cg::marks {

namespace {

constexpr int kUtf8Invalid = 6; // UTF8_INVALID, core/mormot.core.unicode.pas:79

// UTF8_TABLE.Extra[].offset, core/mormot.core.unicode.pas:86-91.
constexpr std::array<qint64, 6> kExtraOffset = {0x00000000, 0x00003080, 0x000e2080,
                                                0x03c82080, 0xfa082080, 0x82082080};

// UTF8_TABLE.Lookup, core/mormot.core.unicode.pas:92-107: the number of bytes that follow a lead
// byte. F8..FD are NOT invalid (5- and 6-byte forms of the original UTF-8 range).
constexpr std::array<quint8, 256> kLookup = {
    7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, //
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, //
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, //
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, //
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, //
    3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, //
};

// WinAnsiToUp, core/mormot.core.unicode.pas:12572-12581 (array[138..255] of byte): CP-1252 upper
// case WITHOUT the accent. Values are reproduced, not derived.
constexpr std::array<quint8, 118> kWinAnsiToUp = {
    83,  139, 140, 141, 90,  143, 144, 145, 146, 147, 148, 149, 150, 151, 152, //
    153, 83,  155, 140, 157, 90,  89,  160, 161, 162, 163, 164, 165, 166, 167, //
    168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, //
    183, 184, 185, 186, 187, 188, 189, 190, 191, 65,  65,  65,  65,  65,  65,  //
    198, 67,  69,  69,  69,  69,  73,  73,  73,  73,  68,  78,  79,  79,  79,  //
    79,  79,  215, 79,  85,  85,  85,  85,  89,  222, 223, 65,  65,  65,  65,  //
    65,  65,  198, 67,  69,  69,  69,  69,  73,  73,  73,  73,  68,  78,  79,  //
    79,  79,  79,  79,  247, 79,  85,  85,  85,  85,  89,  222, 89,
};

// NormToUpper as InitializeUnit fills it (core/mormot.core.unicode.pas:12604-12612): identity,
// 'a'..'z' -> 'A'..'Z', then [138..255] overwritten by WinAnsiToUp.
constexpr std::array<quint8, 256> kNormToUpper = [] {
    std::array<quint8, 256> table{};
    for (std::size_t i = 0; i < 256; ++i) {
        table[i] = static_cast<quint8>(i);
    }
    for (std::size_t i = 'a'; i <= 'z'; ++i) {
        table[i] = static_cast<quint8>(i - 'a' + 'A');
    }
    for (std::size_t i = 0; i < kWinAnsiToUp.size(); ++i) {
        table[138 + i] = kWinAnsiToUp[i];
    }
    return table;
}();

// `value and $ffffff00 = 0`: the test looks at bits 8..31 only, also in the 64-bit build.
constexpr bool isEightBit(qint64 value) noexcept {
    return (value & 0xffffff00LL) == 0;
}

// table[value] for a value that passed isEightBit(). Deviation D-69: see the header.
constexpr qint64 toUpper(qint64 value) noexcept {
    return kNormToUpper[static_cast<quint8>(value)];
}

// Utf8ILComp, core/mormot.core.unicode.pas:7683-7798. Both lengths are > 0 on entry.
qint64 utf8ILComp(const uchar* u1, const uchar* u2, qint64 l1, qint64 l2) noexcept {
    if (u1 == u2) { // :7702 -- the same buffer, whatever the two lengths are
        return 0;
    }
    for (;;) {
        qint64 result = *u1; // :7708-7711
        qint64 c2 = *u2;
        ++u1;
        --l1;
        if (result <= 0x7f) {
            result = kNormToUpper[static_cast<std::size_t>(result)]; // :7714
            if (c2 <= 0x7f) {                                        // :7715-7731, ASCII against ASCII
                result -= kNormToUpper[static_cast<std::size_t>(c2)];
                --l2;
                ++u2;
                if (result != 0) {
                    return result;
                }
                if (l1 != 0) {
                    if (l2 != 0) {
                        continue;
                    }
                    return 1; // u2 is a prefix of u1
                }
                return l2 != 0 ? -1 : 0;
            }
        } else {
            const int extra = kLookup[static_cast<std::size_t>(result)]; // :7736
            if (extra == kUtf8Invalid) {
                return -1; // :7737-7738, invalid lead byte on the left
            }
            l1 -= extra;
            if (l1 < 0) {
                return -1; // :7739-7741, truncated sequence on the left
            }
            for (int i = 0; i < extra; ++i) { // :7742-7747, continuation bytes are not validated
                result = (result << 6) + u1[i];
            }
            u1 += extra;
            result -= kExtraOffset[static_cast<std::size_t>(extra)]; // :7749
            if (isEightBit(result)) {                                // :7750-7751
                result = toUpper(result);
            }
        }
        ++u2; // :7754-7755
        --l2;
        if (c2 <= 0x7f) { // :7756-7761
            result -= kNormToUpper[static_cast<std::size_t>(c2)];
            if (result != 0) {
                return result;
            }
        } else {
            const int extra = kLookup[static_cast<std::size_t>(c2)]; // :7764
            if (extra == kUtf8Invalid) {
                return 1; // :7765-7766, invalid lead byte on the right
            }
            l2 -= extra;
            if (l2 < 0) {
                return 1; // :7767-7769, truncated sequence on the right
            }
            for (int i = 0; i < extra; ++i) { // :7770-7775
                c2 = (c2 << 6) + u2[i];
            }
            u2 += extra;
            c2 -= kExtraOffset[static_cast<std::size_t>(extra)]; // :7777
            if (isEightBit(c2)) {                                // :7778-7781
                result -= toUpper(c2);
            } else {
                result -= c2; // code points above 0xFF are compared raw
            }
            if (result != 0) {
                return result;
            }
        }
        if (l1 == 0) { // :7785-7792
            return l2 == 0 ? 0 : -1;
        }
        if (l2 == 0) {
            return 1;
        }
    }
}

} // namespace

// Utf8_SYSTEMNOCASE, db/mormot.db.raw.sqlite3.pas:6328-6341.
int systemNoCaseCompare(QByteArrayView a, QByteArrayView b) noexcept {
    if (a.size() <= 0) {
        return b.size() <= 0 ? 0 : -1;
    }
    if (b.size() <= 0) {
        return 1;
    }
    const qint64 result = utf8ILComp(reinterpret_cast<const uchar*>(a.data()),
                                     reinterpret_cast<const uchar*>(b.data()), a.size(), b.size());
    // The Pascal callback returns a 32-bit integer: the low 32 bits of the 64-bit difference.
    return static_cast<qint32>(static_cast<quint32>(static_cast<quint64>(result)));
}

} // namespace cg::marks
