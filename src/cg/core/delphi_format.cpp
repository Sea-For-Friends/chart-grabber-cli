// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/delphi_format.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cg::core {

namespace {

// T-0014C1 rework (finding 1/5/8): the one shared digit-string rounding primitive. Every Delphi
// `FloatToStrF` format (`ffFixed`/`ffExponent`/`ffGeneral`) starts the same way: render the value
// to `sigDigits` significant decimal digits, half-away-from-zero, computed from the *decimal*
// expansion - never `pow(10, n)` scaling, which loses the low bits a tie depends on. A binary
// float always has an exact, finite decimal expansion, so requesting far more digits than
// `sigDigits` from `snprintf("%Le")` (glibc renders `%e`/`%f` exactly, to any requested precision)
// gives real digits, not noise; the guard digits are enough to tell whether the cut digit is a
// genuine tie (`5` followed by nothing but further zeros - `%Le` at high precision would print the
// non-zero continuation if there were one) from an ordinary round up/down.
struct RoundedDigits {
    bool negative = false;
    QByteArray digits; // exactly `sigDigits` chars, no sign, no decimal point
    int exp10 = 0;     // digits[0] is the 10^exp10 place: value == digits (as 0.d1d2...) * 10^(exp10+1)
};

RoundedDigits roundToSignificantDigits(long double v, int sigDigits) {
    RoundedDigits r;
    if (sigDigits < 1) {
        sigDigits = 1;
    }
    if (v == 0.0L) {
        r.digits = QByteArray(sigDigits, '0');
        return r;
    }
    r.negative = v < 0.0L;
    const long double av = r.negative ? -v : v;

    const int guard = std::min(sigDigits + 25, 500); // digits beyond `sigDigits` decide ties only
    char fmtSpec[16];
    std::snprintf(fmtSpec, sizeof(fmtSpec), "%%.%dLe", guard - 1);
    char buf[560];
    std::snprintf(buf, sizeof(buf), fmtSpec, av);
    QByteArray sci(buf); // "d.ddd...e[+-]dd"

    const qsizetype ePos = sci.indexOf('e');
    QByteArray digits = sci.left(ePos);
    digits.remove(digits.indexOf('.'), 1);
    int exp10 = sci.mid(ePos + 1).toInt();

    const bool roundUp = digits.size() > sigDigits && digits.at(sigDigits) >= '5';
    digits.truncate(sigDigits);
    if (roundUp) {
        int i = sigDigits - 1;
        while (i >= 0 && digits.at(i) == '9') {
            digits[i] = '0';
            --i;
        }
        if (i < 0) { // "999..." -> "100...0", one more digit than we had: bump the exponent instead
            digits.prepend('1');
            digits.truncate(sigDigits);
            ++exp10;
        } else {
            digits[i] = static_cast<char>(digits.at(i) + 1);
        }
    }
    r.digits = digits;
    r.exp10 = exp10;
    return r;
}

// `ffExponent`-style layout: one digit, `.`, the rest of `rd.digits`, `E`, a sign, and the
// exponent zero-padded to at least 3 digits (Delphi docs: `Format('%e',[12345.6789])` =
// `'1.23456789000000E+004'` - spec F20/finding 3).
QByteArray sciLayout(const RoundedDigits& rd, char decimalSeparator) {
    QByteArray out;
    if (rd.negative) {
        out += '-';
    }
    out += rd.digits.at(0);
    if (rd.digits.size() > 1) {
        out += decimalSeparator;
        out += rd.digits.mid(1);
    }
    out += 'E';
    out += (rd.exp10 < 0 ? '-' : '+');
    QByteArray expDigits = QByteArray::number(qAbs(rd.exp10));
    if (expDigits.size() < 3) {
        expDigits.prepend(QByteArray(3 - expDigits.size(), '0'));
    }
    return out + expDigits;
}

// Delphi's `ffGeneral`/`FloatToStr` render (spec 4.1 N2, F20 %g): `sigDigits` significant digits,
// fixed notation switching to scientific outside the Borland-convention exponent range, upper-case
// `E`, no `+`, no zero-padded exponent (an earlier ruling left this exponent spelling disputed -
// untouched here). `decimalSeparator` lets %g honour FormatEx's separator; `floatToStr` (always
// '.') is this with sigDigits=15.
//
// T-0014C1 rework (finding 8): the fixed/scientific threshold is `e < -3` (Delphi's documented
// `ffGeneral` switches to scientific only when the value's exponent is below -3), not `e < -1` -
// the old threshold sent everything below 0.01 to scientific form, when Delphi keeps values down
// to 1e-4 in fixed notation. See docs/handoffs/T-0014C1.md "Rework" for the one place this still
// disagrees with the ticket's own restatement of the rule.
QByteArray generalFloatString(long double v, int sigDigits, char decimalSeparator) {
    if (v == 0.0L) {
        return QByteArrayLiteral("0");
    }
    if (std::isnan(v)) {
        return QByteArrayLiteral("NAN");
    }
    if (std::isinf(v)) {
        return v > 0 ? QByteArrayLiteral("INF") : QByteArrayLiteral("-INF");
    }
    if (sigDigits < 1) {
        sigDigits = 1;
    }

    const RoundedDigits rd = roundToSignificantDigits(v, sigDigits);
    QByteArray digits = rd.digits;

    qsizetype lastSig = digits.size() - 1;
    while (lastSig > 0 && digits[lastSig] == '0') {
        --lastSig; // ffGeneral never pads with trailing zeros
    }
    digits.truncate(lastSig + 1);

    const int e = rd.exp10 + 1; // Borland convention used by the threshold below

    QByteArray out;
    if (rd.negative)
        out += '-';
    if (e > sigDigits || e < -3) {
        out += digits[0];
        if (digits.size() > 1) {
            out += decimalSeparator;
            out += digits.mid(1);
        }
        out += 'E';
        out += QByteArray::number(rd.exp10);
    } else if (e <= 0) {
        out += '0';
        out += decimalSeparator;
        out += QByteArray(-e, '0');
        out += digits;
    } else if (e >= digits.size()) {
        out += digits;
        out += QByteArray(e - digits.size(), '0');
    } else {
        out += digits.left(e);
        out += decimalSeparator;
        out += digits.mid(e);
    }
    return out;
}

// `ffFixed` layout shared by `roundEx`, `%f`, `%n` and `%m`: lay `rd` (already rounded to 15
// significant digits) out with exactly `prec` digits after the decimal point, zero-filling any
// position past the 15th significant digit (spec F25: "digits beyond the 15th print as 0"), with
// one more half-away-from-zero round when `prec` cuts into the significant digits themselves
// (e.g. `RoundEx(1.125, 2)`: 15-sig round leaves "112500000000000", asking for only 2 decimals
// rounds the "12500..." tail again, half away, to "13").
QByteArray fixedLayoutFromDigits(const RoundedDigits& rd, int prec, char decimalSeparator) {
    const int sigDigits = static_cast<int>(rd.digits.size());
    const int intDigits = rd.exp10 + 1;

    QByteArray intPart;
    QByteArray fracPart;
    if (intDigits <= 0) {
        intPart = QByteArrayLiteral("0");
        fracPart = QByteArray(-intDigits, '0') + rd.digits;
    } else if (intDigits >= sigDigits) {
        intPart = rd.digits + QByteArray(intDigits - sigDigits, '0');
    } else {
        intPart = rd.digits.left(intDigits);
        fracPart = rd.digits.mid(intDigits);
    }
    if (fracPart.size() < prec + 1) {
        fracPart += QByteArray(prec + 1 - fracPart.size(), '0'); // one guard digit for rounding
    }

    const bool roundUp = fracPart.size() > prec && fracPart.at(prec) >= '5';
    fracPart.truncate(prec);
    if (roundUp) {
        int i = prec - 1;
        while (i >= 0 && fracPart.at(i) == '9') {
            fracPart[i] = '0';
            --i;
        }
        if (i >= 0) {
            fracPart[i] = static_cast<char>(fracPart.at(i) + 1);
        } else {
            int j = static_cast<int>(intPart.size()) - 1;
            while (j >= 0 && intPart.at(j) == '9') {
                intPart[j] = '0';
                --j;
            }
            if (j >= 0) {
                intPart[j] = static_cast<char>(intPart.at(j) + 1);
            } else {
                intPart.prepend('1');
            }
        }
    }

    QByteArray out;
    if (rd.negative)
        out += '-';
    out += intPart;
    if (prec > 0) {
        out += decimalSeparator;
        out += fracPart;
    }
    return out;
}

// `prec` decimal places, zero-padded, on a value first rounded to 15 significant digits (spec
// F25/F20 - Delphi's `Format`/`FloatToStrF` always uses `Precision=15` for a `Double`/`Extended`
// argument; only the decimal-place count is caller-supplied). Always locale-`C`, then the decimal
// point is substituted for `decimalSeparator`.
QByteArray fixedDecimalString(double v, int prec, char decimalSeparator) {
    const RoundedDigits rd = roundToSignificantDigits(static_cast<long double>(v), 15);
    return fixedLayoutFromDigits(rd, prec, decimalSeparator);
}

// F20/F21 %n/%m: `fixedDecimalString` with `sep` inserted every 3 digits of the integer part,
// counted from the right. `sep == '\0'` (FormatEx's un-set ThousandSeparator, spec F21) still
// inserts the raw byte - reproducing the documented "garbage" is the point.
QByteArray groupThousands(double v, int prec, char decimalSeparator, char sep) {
    QByteArray s = fixedDecimalString(v, prec, decimalSeparator);
    const qsizetype sepPos = s.indexOf(decimalSeparator); // -1 when prec == 0 (no fraction at all)
    const qsizetype intEnd = (sepPos < 0) ? s.size() : sepPos;
    qsizetype digitsStart = 0;
    if (digitsStart < intEnd && (s[digitsStart] == '-' || s[digitsStart] == '+')) {
        ++digitsStart;
    }
    QByteArray grouped;
    const qsizetype nDigits = intEnd - digitsStart;
    for (qsizetype i = 0; i < nDigits; ++i) {
        if (i > 0 && (nDigits - i) % 3 == 0) {
            grouped += sep;
        }
        grouped += s[digitsStart + i];
    }
    return s.left(digitsStart) + grouped + s.mid(intEnd);
}

QByteArray zeroPadUnsigned(QByteArray digits, int minDigits) {
    if (minDigits > digits.size()) {
        digits.prepend(QByteArray(minDigits - digits.size(), '0'));
    }
    return digits;
}

// Signed decimal string for %d: sign then `intToStr`'s magnitude, zero-padded (spec: precision
// zero-pads integer specifiers) to at least `minDigits` digits, sign never counted as a digit.
QByteArray zeroPadSigned(qint64 v, int minDigits) {
    const bool negative = v < 0;
    const quint64 magnitude = negative ? (0ULL - static_cast<quint64>(v)) : static_cast<quint64>(v);
    QByteArray digits = zeroPadUnsigned(QByteArray::number(magnitude), minDigits);
    return negative ? (QByteArrayLiteral("-") + digits) : digits;
}

// %e (spec F20, finding 3/4): `prec` is the *total* significant digit count (default 15, Delphi
// docs), not fractional digits; NaN/Inf print `NAN`/`INF`/`-INF` (finding 4) instead of raising;
// ties round half-away-from-zero via the shared digit-string helper, not `ad / pow(10, exp)`.
QByteArray formatScientific(double d, int prec, char decimalSeparator) {
    if (std::isnan(d)) {
        return QByteArrayLiteral("NAN");
    }
    if (std::isinf(d)) {
        return d > 0 ? QByteArrayLiteral("INF") : QByteArrayLiteral("-INF");
    }
    if (prec < 1) {
        prec = 1;
    }
    const RoundedDigits rd = roundToSignificantDigits(static_cast<long double>(d), prec);
    return sciLayout(rd, decimalSeparator);
}

} // namespace

QByteArray intToStr(qint64 v) {
    return QByteArray::number(static_cast<qlonglong>(v));
}

QByteArray intToHex(qint64 v, int digits) {
    QByteArray hex = QByteArray::number(static_cast<qulonglong>(static_cast<quint32>(v)), 16).toUpper();
    if (digits > 0 && hex.size() < digits) {
        hex.prepend(QByteArray(digits - hex.size(), '0'));
    }
    return hex;
}

QByteArray floatToStr(long double v) {
    return generalFloatString(v, 15, '.');
}

qint64 delphiRound(long double v) {
    return static_cast<qint64>(std::rintl(v)); // default FP mode: round-half-to-even
}

qint64 delphiTrunc(long double v) {
    return static_cast<qint64>(std::truncl(v));
}

QByteArray roundEx(double v, int precision) {
    if (std::isnan(v)) {
        return QByteArrayLiteral("-");
    }
    if (std::isinf(v)) {
        return v > 0 ? QByteArrayLiteral("INF") : QByteArrayLiteral("-INF");
    }
    const int p = std::clamp(precision, 0, 18);
    const RoundedDigits rd = roundToSignificantDigits(static_cast<long double>(v), 15);
    const int intDigits = rd.exp10 + 1;
    if (intDigits > 15) {
        // Magnitude too large for 15 significant fixed digits (spec F25): fall back to scientific
        // form. No corpus script or spec worked example exercises this path; the exact layout
        // (Delphi ffExponent convention, matching %e's sign+3-digit exponent) is this port's
        // reasonable-effort reading - see docs/handoffs/T-0014C1.md "Rework".
        return sciLayout(rd, '.');
    }
    return fixedLayoutFromDigits(rd, p, '.'); // FS.DecimalSeparator = '.', always, locale-independent
}

Result<QByteArray> delphiFormat(const QByteArray& fmt, std::span<const FormatArg> args, char decimalSeparator,
                                char thousandsSeparator, const QByteArray& currencyString) {
    QByteArray out;
    out.reserve(fmt.size());
    std::size_t implicitIdx = 0;
    qsizetype i = 0;
    const qsizetype n = fmt.size();

    auto takeImplicitInt = [&](qint64& dst) {
        if (implicitIdx >= static_cast<std::size_t>(args.size())) {
            return false;
        }
        // The script interpreter's implicit integer conversion: Int, Bool (1/0) and Empty (0).
        const FormatArg& a = args[implicitIdx++];
        if (const auto* iv = std::get_if<qint64>(&a)) {
            dst = *iv;
        } else if (const auto* bv = std::get_if<bool>(&a)) {
            dst = *bv ? 1 : 0;
        } else {
            return false;
        }
        return true;
    };

    while (i < n) {
        const char c = fmt.at(i);
        if (c != '%') {
            out += c;
            ++i;
            continue;
        }
        ++i;
        if (i >= n) { // trailing lone '%'
            out += '%';
            break;
        }
        if (fmt.at(i) == '%') {
            out += '%';
            ++i;
            continue;
        }

        // Optional `index:` - a leading digit run followed by ':' is an explicit 0-based index;
        // otherwise (no ':') that same digit run is the field width (spec grammar
        // `%[index:][-][width][.prec]type`: index and width are syntactically identical, the
        // trailing ':' is the only disambiguator).
        const qsizetype digitsStart = i;
        while (i < n && fmt.at(i) >= '0' && fmt.at(i) <= '9') {
            ++i;
        }
        const bool hadDigits = i > digitsStart;
        int explicitIndex = -1;
        int width = -1;
        bool haveWidth = false;
        if (hadDigits && i < n && fmt.at(i) == ':') {
            explicitIndex = fmt.mid(digitsStart, i - digitsStart).toInt();
            ++i;
            // T-0014C1 rework (finding 2): an explicit index *moves* the implicit argument
            // counter to index+1 (documented Delphi: `Format('%d %d %d %0:d %d',[1,2,3,4])` =
            // `'1 2 3 1 2'`) - it does not just borrow an argument and leave the counter alone.
            implicitIdx = static_cast<std::size_t>(explicitIndex) + 1;
        } else if (hadDigits) {
            width = fmt.mid(digitsStart, i - digitsStart).toInt();
            haveWidth = true;
        }

        bool leftAlign = false;
        if (i < n && fmt.at(i) == '-') {
            leftAlign = true;
            ++i;
        }

        if (!haveWidth) {
            if (i < n && fmt.at(i) == '*') {
                qint64 w = 0;
                if (!takeImplicitInt(w)) {
                    return fail(ErrorCode::IoError,
                                QStringLiteral("Format: '*' width needs an integer argument"));
                }
                width = static_cast<int>(w);
                ++i;
            } else {
                const qsizetype ws = i;
                while (i < n && fmt.at(i) >= '0' && fmt.at(i) <= '9') {
                    ++i;
                }
                if (i > ws) {
                    width = fmt.mid(ws, i - ws).toInt();
                }
            }
        }

        int prec = -1;
        if (i < n && fmt.at(i) == '.') {
            ++i;
            if (i < n && fmt.at(i) == '*') {
                qint64 p = 0;
                if (!takeImplicitInt(p)) {
                    return fail(ErrorCode::IoError,
                                QStringLiteral("Format: '*' precision needs an integer argument"));
                }
                prec = static_cast<int>(p);
                ++i;
            } else {
                const qsizetype ps = i;
                while (i < n && fmt.at(i) >= '0' && fmt.at(i) <= '9') {
                    ++i;
                }
                prec = (i > ps) ? fmt.mid(ps, i - ps).toInt() : 0;
            }
        }

        if (i >= n) {
            return fail(ErrorCode::IoError, QStringLiteral("Format: truncated format specifier"));
        }
        const char typeChar = fmt.at(i);
        ++i;
        const char lower = (typeChar >= 'A' && typeChar <= 'Z') ? static_cast<char>(typeChar + 32) : typeChar;

        const FormatArg* value = nullptr;
        int argIdx = 0;
        if (explicitIndex >= 0) {
            if (explicitIndex >= static_cast<int>(args.size())) {
                return fail(ErrorCode::IoError,
                            QStringLiteral("Format: argument index %1 out of range").arg(explicitIndex));
            }
            value = &args[static_cast<std::size_t>(explicitIndex)];
            argIdx = explicitIndex;
        } else {
            if (implicitIdx >= static_cast<std::size_t>(args.size())) {
                return fail(ErrorCode::IoError, QStringLiteral("Format: not enough arguments"));
            }
            argIdx = static_cast<int>(implicitIdx);
            value = &args[implicitIdx++];
        }

        // T-0014C1 rework (finding 6): Delphi's `Format` raises `EConvertError` ("Format '%s'
        // invalid or incompatible with argument") whenever a specifier's argument isn't the exact
        // type it wants - `%d/%u/%x` want an `Integer`, `%s` a string, the float family an
        // `Extended`. The pre-rework code reached this via `Value::toInt()/toReal()/toStr()`,
        // which are the *interpreter's* general implicit-conversion rules (Bool/Empty -> Int,
        // Int -> Real, anything -> Str) and are too permissive for `Format` specifically.
        auto typeError = [&](char spec) {
            return fail(ErrorCode::IoError,
                        QStringLiteral("Format: '%") + QChar::fromLatin1(spec) +
                            QStringLiteral("' invalid or incompatible with argument %1").arg(argIdx));
        };

        QByteArray piece;
        switch (lower) {
        case 'd': {
            if (!std::holds_alternative<qint64>(*value)) {
                return typeError(lower);
            }
            piece = zeroPadSigned(std::get<qint64>(*value), prec >= 0 ? prec : 0);
            break;
        }
        case 'u': {
            if (!std::holds_alternative<qint64>(*value)) {
                return typeError(lower);
            }
            piece = zeroPadUnsigned(
                QByteArray::number(static_cast<qulonglong>(static_cast<quint32>(std::get<qint64>(*value)))),
                prec >= 0 ? prec : 0);
            break;
        }
        case 'x': {
            if (!std::holds_alternative<qint64>(*value)) {
                return typeError(lower);
            }
            piece = intToHex(std::get<qint64>(*value), prec >= 0 ? prec : 0);
            break;
        }
        case 's': {
            if (!std::holds_alternative<QByteArray>(*value)) {
                return typeError(lower);
            }
            piece = std::get<QByteArray>(*value);
            if (prec >= 0 && piece.size() > prec) {
                piece = piece.left(prec);
            }
            break;
        }
        case 'e': {
            if (!std::holds_alternative<long double>(*value)) {
                return typeError(lower);
            }
            piece = formatScientific(static_cast<double>(std::get<long double>(*value)),
                                     prec >= 0 ? prec : 15, decimalSeparator);
            break;
        }
        case 'f': {
            if (!std::holds_alternative<long double>(*value)) {
                return typeError(lower);
            }
            const double d = static_cast<double>(std::get<long double>(*value));
            if (std::isnan(d)) {
                piece = QByteArrayLiteral("NAN"); // finding 4: no runtime error on non-finite input
            } else if (std::isinf(d)) {
                piece = d > 0 ? QByteArrayLiteral("INF") : QByteArrayLiteral("-INF");
            } else {
                piece = fixedDecimalString(d, prec >= 0 ? prec : 2, decimalSeparator);
            }
            break;
        }
        case 'g': {
            if (!std::holds_alternative<long double>(*value)) {
                return typeError(lower);
            }
            piece =
                generalFloatString(std::get<long double>(*value), prec >= 1 ? prec : 15, decimalSeparator);
            break;
        }
        case 'n': {
            if (!std::holds_alternative<long double>(*value)) {
                return typeError(lower);
            }
            const double d = static_cast<double>(std::get<long double>(*value));
            if (std::isnan(d)) {
                piece = QByteArrayLiteral("NAN");
            } else if (std::isinf(d)) {
                piece = d > 0 ? QByteArrayLiteral("INF") : QByteArrayLiteral("-INF");
            } else {
                piece = groupThousands(d, prec >= 0 ? prec : 2, decimalSeparator, thousandsSeparator);
            }
            break;
        }
        case 'm': {
            if (!std::holds_alternative<long double>(*value)) {
                return typeError(lower);
            }
            const double d = static_cast<double>(std::get<long double>(*value));
            if (std::isnan(d)) {
                piece = QByteArrayLiteral("NAN");
            } else if (std::isinf(d)) {
                piece = d > 0 ? QByteArrayLiteral("INF") : QByteArrayLiteral("-INF");
            } else {
                piece = currencyString +
                        groupThousands(d, prec >= 0 ? prec : 2, decimalSeparator, thousandsSeparator);
            }
            break;
        }
        default:
            // Built by concatenation, not QString::arg("...'%%1'"), to sidestep arg()'s own
            // '%'-marker scanning - the message must contain a literal, un-substituted '%'.
            return fail(ErrorCode::IoError, QStringLiteral("unknown format specifier '%") +
                                                QChar::fromLatin1(typeChar) + QStringLiteral("'"));
        }

        if (width > 0 && static_cast<qsizetype>(width) > piece.size()) {
            const QByteArray pad(width - static_cast<int>(piece.size()), ' ');
            piece = leftAlign ? (piece + pad) : (pad + piece);
        }
        out += piece;
    }
    return out;
}

} // namespace cg::core
