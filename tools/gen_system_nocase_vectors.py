#!/usr/bin/env python3
"""T-0062 / FEAT-GEO-034 (held open): vectors for the SYSTEMNOCASE collation of Marks.db3.

Writes tests/fixtures/marks/system_nocase_vectors.tsv. The comparison below is transcribed from the
Pascal source of mORMot 2 (checkout 2.4.17538, head b770705, 2026-10-11) statement by statement and
shares nothing with the C++ port (src/cg/marks/mormot/system_nocase.cpp), which the fixture tests:

  db/mormot.db.raw.sqlite3.pas:6328-6341   Utf8_SYSTEMNOCASE (the SQLite callback, returns integer)
  core/mormot.core.unicode.pas:7683-7798   Utf8ILComp (returns PtrInt)
  core/mormot.core.unicode.pas:84-107      UTF8_TABLE (Extra[].offset, Lookup[])
  core/mormot.core.unicode.pas:12572-12581 WinAnsiToUp
  core/mormot.core.unicode.pas:12604-12612 NormToUpper = identity, 'a'..'z' -> 'A'..'Z', [138..255] = WinAnsiToUp

Integer model: the 64-bit Windows build (PtrInt = Int64, cardinal lengths, the callback's 32-bit
`integer` result is the low 32 bits of the PtrInt). A table index outside 0..255 has no defined result
in Pascal (range checks are off); the model raises and such an input is never written as a vector.

Line format: <hex of a>\t<hex of b>\t<sign: -1|0|1>\t<branch label>; "-" is the empty string.
Usage: tools/gen_system_nocase_vectors.py [output path]
"""
import sys

UTF8_INVALID = 6

# core/mormot.core.unicode.pas:84-107
EXTRA_OFFSET = (0x00000000, 0x00003080, 0x000E2080, 0x03C82080, 0xFA082080, 0x82082080)
LOOKUP = (
    [7] + [0] * 127          # 00..7F
    + [6] * 64               # 80..BF
    + [1] * 32               # C0..DF
    + [2] * 16               # E0..EF
    + [3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6]  # F0..FF
)

# core/mormot.core.unicode.pas:12572-12581, array[138..255] of byte
WIN_ANSI_TO_UP = (
    83,  139, 140, 141, 90,  143, 144, 145, 146, 147, 148, 149, 150, 151, 152,
    153, 83,  155, 140, 157,  90,  89, 160, 161, 162, 163, 164, 165, 166, 167,
    168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182,
    183, 184, 185, 186, 187, 188, 189, 190, 191, 65,  65,  65,  65,  65,  65,
    198, 67,  69,  69,  69,  69,  73,  73,  73,  73,  68,  78,  79,  79,  79,
    79,  79,  215, 79,  85,  85,  85,  85,  89,  222, 223, 65,  65,  65,  65,
    65,  65,  198, 67,  69,  69,  69,  69,  73,  73,  73,  73,  68,  78,  79,
    79,  79,  79,  79,  247, 79,  85,  85,  85,  85,  89,  222, 89)
assert len(LOOKUP) == 256 and len(WIN_ANSI_TO_UP) == 118

# InitializeUnit, :12604-12612
NORM_TO_UPPER = list(range(256))
NORM_TO_UPPER[ord('a'):ord('a') + 26] = NORM_TO_UPPER[ord('A'):ord('A') + 26]
NORM_TO_UPPER[138:256] = WIN_ANSI_TO_UP


class PascalOutOfBounds(Exception):
    """table[] indexed outside 0..255: undefined in the Pascal source."""


def table(i):
    if not 0 <= i <= 255:
        raise PascalOutOfBounds(i)
    return NORM_TO_UPPER[i]


def utf8_il_comp(u1, u2):
    """Utf8ILComp(u1, u2, L1, L2) for two distinct, non-nil buffers."""
    L1, L2 = len(u1), len(u2)
    p1 = p2 = 0
    if L1 == 0:
        return -1                                   # neg
    if L2 == 0:
        return 1                                    # pos
    while True:
        result = u1[p1]
        c2 = u2[p2]
        p1 += 1
        L1 -= 1
        if result <= 0x7F:
            result = table(result)
            if c2 <= 0x7F:
                result -= table(c2)
                L2 -= 1
                p2 += 1
                if result != 0:
                    return result
                elif L1 != 0:
                    if L2 != 0:
                        continue
                    else:
                        return 1                    # goto pos
                elif L2 != 0:
                    return -1                       # goto neg
                else:
                    return result                   # 0
        else:
            extra = LOOKUP[result]
            if extra == UTF8_INVALID:
                return -1                           # goto neg
            L1 -= extra
            if L1 < 0:                              # integer(L1) < 0
                return -1                           # goto neg
            i = 0
            while True:
                result = result << 6
                result += u1[p1 + i]
                i += 1
                if i == extra:
                    break
            p1 += extra
            result -= EXTRA_OFFSET[extra]
            if result & 0xFFFFFF00 == 0:
                result = table(result)
        p2 += 1
        L2 -= 1
        if c2 <= 0x7F:
            result -= table(c2)
            if result != 0:
                return result
        else:
            extra = LOOKUP[c2]
            if extra == UTF8_INVALID:
                return 1                            # goto pos
            L2 -= extra
            if L2 < 0:
                return 1                            # goto pos
            i = 0
            while True:
                c2 = c2 << 6
                c2 += u2[p2 + i]
                i += 1
                if i == extra:
                    break
            p2 += extra
            c2 -= EXTRA_OFFSET[extra]
            if c2 & 0xFFFFFF00 == 0:
                result -= table(c2)
            else:
                result -= c2
            if result != 0:
                return result
        if L1 == 0:
            if L2 == 0:
                return result                       # 0
            else:
                return -1                           # goto neg
        elif L2 == 0:
            return 1                                # goto pos


def utf8_systemnocase(s1, s2):
    """Utf8_SYSTEMNOCASE; the PtrInt result is assigned to a 32-bit integer."""
    if len(s1) <= 0:
        return 0 if len(s2) <= 0 else -1
    if len(s2) <= 0:
        return 1
    r = utf8_il_comp(s1, s2)
    return ((r + 0x80000000) & 0xFFFFFFFF) - 0x80000000


def sign(v):
    return (v > 0) - (v < 0)


def u(text):
    return text.encode('utf-8')


def cp(code):
    return chr(code).encode('utf-8')


def vectors():
    out = []

    def add(label, a, b, both=True):
        out.append((label, a, b))
        if both:
            out.append((label + '/swapped', b, a))

    # --- empties (Utf8_SYSTEMNOCASE) ---
    add('empty-both', b'', b'', both=False)
    add('empty-vs-ascii', b'', b'a')
    add('empty-vs-multibyte', b'', u('é'))
    add('empty-vs-invalid', b'', b'\x80')

    # --- ASCII fast path ---
    add('ascii-case-equal', b'abc', b'ABC')
    add('ascii-equal', b'sasgis', b'sasgis', both=False)
    add('ascii-mixed-case-equal', b'SasGis', b'sASgIS')
    add('ascii-differs-first', b'a', b'b')
    add('ascii-differs-case-crossing', b'a', b'B')
    add('ascii-upper-Z-vs-lower-a', b'Z', b'a')
    add('ascii-bracket-vs-lower-a', b'[', b'a')      # 0x5B > 'A' after folding, 0x5B < 'a' raw
    add('ascii-underscore-vs-lower-z', b'_', b'z')   # 0x5F > 'Z' after folding, 0x5F < 'z' raw
    add('ascii-backtick-vs-upper-A', b'`', b'A')
    add('ascii-brace-vs-upper-Z', b'{', b'Z')
    add('ascii-digits', b'10', b'9')
    add('ascii-space', b'a b', b'a_b')
    add('ascii-differs-last', b'abcdef', b'abcdeg')
    add('ascii-prefix', b'abc', b'abcd')
    add('ascii-prefix-case', b'ABC', b'abcd')
    add('ascii-embedded-nul-equal', b'a\x00b', b'A\x00B')
    add('ascii-embedded-nul-differs-after', b'a\x00b', b'a\x00c')
    add('ascii-trailing-nul-is-longer', b'a', b'a\x00')
    add('ascii-nul-vs-char', b'\x00', b'\x01')

    # --- Latin-1: every code point U+0080..U+00FF through the WinAnsi table ---
    for code in range(0x80, 0x100):
        folded = NORM_TO_UPPER[code]
        image = bytes([folded]) if folded <= 0x7F else cp(folded)
        add('latin1-U+%04X-vs-table-image' % code, cp(code), image)
        add('latin1-U+%04X-vs-Z' % code, cp(code), b'Z')
        add('latin1-U+%04X-vs-U+00DF' % code, cp(code), cp(0xDF))
    add('latin1-e-acute-vs-E', u('é'), b'E')
    add('latin1-e-acute-vs-e', u('é'), b'e')
    add('latin1-E-acute-vs-e-grave', u('É'), u('è'))
    add('latin1-sharp-s-vs-SS', u('ß'), b'SS')
    add('latin1-sharp-s-vs-s', u('ß'), b's')
    add('latin1-ae-case', u('Æ'), u('æ'))
    add('latin1-eth-vs-D', u('ð'), b'd')
    add('latin1-thorn-case', u('Þ'), u('þ'))
    add('latin1-multiply-vs-divide', u('×'), u('÷'))
    add('latin1-word-deaccented', u('Café'), b'CAFE')
    add('latin1-word-deaccented-longer', u('café'), b'cafes')
    add('latin1-word-umlauts', u('Müller'), b'muller')
    add('c1-U+008A-vs-S', cp(0x8A), b's')
    add('c1-U+009A-vs-S', cp(0x9A), b'S')
    add('c1-U+009F-vs-Y', cp(0x9F), b'y')
    add('c1-U+008C-vs-U+009C', cp(0x8C), cp(0x9C))
    add('c1-U+008E-vs-Z', cp(0x8E), b'z')

    # --- the 0xFF / 0x100 boundary ---
    add('boundary-U+00FF-vs-Y', cp(0xFF), b'Y')
    add('boundary-U+00FF-vs-U+0178', cp(0xFF), cp(0x178))
    add('boundary-U+0100-vs-A', cp(0x100), b'A')
    add('boundary-U+0100-vs-U+0101', cp(0x100), cp(0x101))
    add('boundary-U+0100-vs-U+00FF', cp(0x100), cp(0xFF))
    add('boundary-U+0100-vs-U+00C0', cp(0x100), cp(0xC0))
    add('boundary-U+0100-equal', cp(0x100), cp(0x100), both=False)
    add('boundary-U+017F-vs-S', cp(0x17F), b'S')

    # --- above 0xFF: raw code points, case-sensitive ---
    add('cyrillic-case-differs', u('Привет'), u('привет'))
    add('cyrillic-equal', u('Привет'), u('Привет'), both=False)
    add('cyrillic-differs-last', u('Метка'), u('Метки'))
    add('cyrillic-prefix', u('Метка'), u('Метка 2'))
    add('cyrillic-vs-ascii', u('я'), b'z')
    add('cyrillic-vs-latin1', u('А'), u('À'))
    add('cyrillic-after-ascii-prefix', u('abc Я'), u('ABC я'))
    add('greek-case-differs', u('Ω'), u('ω'))
    add('two-byte-max-U+07FF-vs-three-byte-min-U+0800', cp(0x7FF), cp(0x800))

    # --- 3- and 4-byte sequences ---
    add('three-byte-equal', u('€'), u('€'), both=False)
    add('three-byte-neighbours', cp(0x20AC), cp(0x20AD))
    add('three-byte-cjk', u('中文'), u('中国'))
    add('three-byte-vs-ascii', u('€'), b'E')
    add('three-byte-vs-two-byte', u('€'), u('я'))
    add('three-byte-U+FFFF-vs-four-byte-U+10000', cp(0xFFFF), cp(0x10000))
    add('four-byte-equal', cp(0x1F600), cp(0x1F600), both=False)
    add('four-byte-neighbours', cp(0x1F600), cp(0x1F601))
    add('four-byte-vs-three-byte', cp(0x1F600), u('€'))
    add('four-byte-vs-ascii', cp(0x1F600), b'~')
    add('four-byte-max-U+10FFFF', cp(0x10FFFF), cp(0x10FFFE))
    add('four-byte-prefix', cp(0x1F600), cp(0x1F600) + b'a')
    add('mixed-widths-equal-after-fold', u('aé€') + cp(0x1F600), u('AÉ€') + cp(0x1F600))
    add('mixed-widths-prefix', u('aé€'), u('AÉ€') + cp(0x1F600))

    # --- lead bytes F8..FD: decoded as 5- and 6-byte sequences, not rejected ---
    add('five-byte-equal', b'\xF8\x88\x80\x80\x80', b'\xF8\x88\x80\x80\x80', both=False)
    add('five-byte-neighbours', b'\xF8\x88\x80\x80\x80', b'\xF8\x88\x80\x80\x81')
    add('five-byte-vs-ascii', b'\xF8\x88\x80\x80\x80', b'a')
    add('five-byte-vs-four-byte', b'\xF8\x88\x80\x80\x80', cp(0x10FFFF))
    add('five-byte-overlong-U+0041-vs-a', b'\xF8\x80\x80\x81\x81', b'a')
    add('six-byte-equal', b'\xFC\x84\x80\x80\x80\x80', b'\xFC\x84\x80\x80\x80\x80', both=False)
    add('six-byte-neighbours', b'\xFC\x84\x80\x80\x80\x80', b'\xFC\x84\x80\x80\x80\x81')
    add('six-byte-vs-ascii', b'\xFC\x84\x80\x80\x80\x80', b'a')
    add('six-byte-vs-five-byte', b'\xFC\x84\x80\x80\x80\x80', b'\xFB\xBF\xBF\xBF\xBF')
    add('six-byte-max-vs-ascii', b'\xFD\xBF\xBF\xBF\xBF\xBF', b'a')
    add('six-byte-max-vs-negative-difference-wraps', b'\xFD\xBF\xBF\xBF\xBF\xBF', b'\xC0\x00')

    # --- continuation bytes are not validated; overlong forms decode ---
    add('overlong-C1-81-is-A', b'\xC1\x81', b'a')
    add('overlong-C0-80-is-NUL', b'\xC0\x80', b'\x00')
    add('overlong-E0-80-C9-is-latin1-E', b'\xE0\x83\x89', b'e')
    add('bad-continuation-C3-41-is-U+0081', b'\xC3\x41', cp(0x81))
    add('bad-continuation-C0-00-is-negative', b'\xC0\x00', b'a')
    add('bad-continuation-C0-00-vs-cyrillic', b'\xC0\x00', u('я'))
    add('bad-continuation-E2-28-A1', b'\xE2\x28\xA1', b'\xE2\x82\xAC')

    # --- invalid lead bytes: left -> -1, right -> +1 (not antisymmetric when both are invalid) ---
    for lead in (0x80, 0xBF, 0xFE, 0xFF):
        bad = bytes([lead])
        add('invalid-lead-%02X-vs-ascii' % lead, bad, b'a')
        add('invalid-lead-%02X-vs-multibyte' % lead, bad, u('я'))
        add('invalid-lead-%02X-vs-itself' % lead, bad, bad, both=False)
        add('invalid-lead-%02X-after-equal-prefix' % lead, b'ab' + bad, b'ABc')
        add('invalid-lead-%02X-after-multibyte-prefix' % lead, u('é') + bad, u('Éz'))
    add('invalid-lead-both-different', b'\x80', b'\xFF')
    add('invalid-lead-vs-longer-ascii', b'\x80', b'abc')

    # --- truncated sequences: left -> -1, right -> +1 ---
    add('truncated-two-byte-vs-ascii', b'\xC3', b'a')
    add('truncated-two-byte-vs-complete', b'\xC3', u('é'))
    add('truncated-two-byte-vs-itself', b'\xC3', b'\xC3', both=False)
    add('truncated-three-byte-one-missing', b'\xE2\x82', u('€'))
    add('truncated-three-byte-two-missing', b'\xE2', u('€'))
    add('truncated-four-byte', b'\xF0\x9F\x98', cp(0x1F600))
    add('truncated-six-byte', b'\xFC\x84\x80', b'\xFC\x84\x80\x80\x80\x80')
    add('truncated-after-equal-prefix', b'abc\xC3', u('ABCé'))
    add('truncated-vs-invalid-lead', b'\xC3', b'\x80')
    add('truncated-both', b'\xE2\x82', b'\xC3')

    # --- equal prefixes ordered by length ---
    add('prefix-multibyte-then-ascii', u('é'), u('éa'))
    add('prefix-ascii-then-multibyte', b'a', u('aé'))
    add('prefix-multibyte-then-multibyte', u('é'), u('éé'))
    add('prefix-folded-equal', u('é'), b'Ea')
    add('prefix-cyrillic', u('П'), u('Пр'))
    add('prefix-ascii-vs-cyrillic-tail', b'ab', u('abя'))
    return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'tests/fixtures/marks/system_nocase_vectors.tsv'
    lines = [
        '# Generated by tools/gen_system_nocase_vectors.py -- do not edit by hand.',
        '# <hex of a>\t<hex of b>\t<sign>\t<branch label>; "-" is the empty string.',
    ]
    seen = set()
    for label, a, b in vectors():
        if (a, b) in seen:
            continue
        seen.add((a, b))
        expected = sign(utf8_systemnocase(a, b))  # PascalOutOfBounds here = not a legal vector
        lines.append('%s\t%s\t%d\t%s' % (a.hex() or '-', b.hex() or '-', expected, label))
    with open(path, 'w', encoding='ascii', newline='\n') as out:
        out.write('\n'.join(lines) + '\n')
    print('%d vectors -> %s' % (len(lines) - 2, path))


if __name__ == '__main__':
    main()
