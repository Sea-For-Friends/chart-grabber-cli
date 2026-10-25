// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/math_expression_evaluator.h"

#include "cg/core/delphi_text.h"

namespace cg::maptype {

namespace {

// Delphi's Integer arithmetic wraps (overflow checking is off in the release build).
qint32 wrap(quint32 v) {
    return static_cast<qint32>(v);
}

// The Pascal reads PChar up to the terminating #0; an exhausted view plays that part.
QChar peek(QStringView pos) {
    return pos.isEmpty() ? QChar(u'\0') : pos.front();
}

} // namespace

QString MathExpressionEvaluator::evaluate(QStringView expression, qint32 x, qint32 y, qint32 z) {
    if (expression.isEmpty()) {
        return {};
    }
    x_ = x;
    y_ = y;
    z_ = z;
    QStringView pos = expression;
    qint32 value = 0;
    if (tryParseExpression(pos, value)) {
        return QString::number(value);
    }
    return {};
}

bool MathExpressionEvaluator::tryParseExpression(QStringView& pos, qint32& value) {
    if (!tryParseTerm(pos, value)) {
        return false;
    }
    qint32 number = 0;
    while (peek(pos) == u'+' || peek(pos) == u'-') {
        const bool plus = pos.front() == u'+';
        pos = pos.sliced(1);
        if (!tryParseTerm(pos, number)) {
            return false;
        }
        value = plus ? wrap(static_cast<quint32>(value) + static_cast<quint32>(number))
                     : wrap(static_cast<quint32>(value) - static_cast<quint32>(number));
    }
    return true;
}

bool MathExpressionEvaluator::tryParseTerm(QStringView& pos, qint32& value) {
    if (!tryParseNumber(pos, value)) {
        return false;
    }
    qint32 number = 0;
    while (peek(pos) == u'*' || peek(pos) == u'/') {
        const bool times = pos.front() == u'*';
        pos = pos.sliced(1);
        if (!tryParseNumber(pos, number)) {
            return false;
        }
        if (times) {
            value = wrap(static_cast<quint32>(value) * static_cast<quint32>(number));
        } else {
            if (number == 0) {
                return false;
            }
            value /= number; // operands come from tile coordinates and digit runs, never INT_MIN / -1
        }
    }
    return true;
}

bool MathExpressionEvaluator::tryParseNumber(QStringView& pos, qint32& value) {
    switch (peek(pos).unicode()) {
    case u'x':
        value = x_;
        pos = pos.sliced(1);
        return true;
    case u'y':
        value = y_;
        pos = pos.sliced(1);
        return true;
    case u'z':
        value = z_;
        pos = pos.sliced(1);
        return true;
    default:
        break;
    }
    qsizetype len = 0;
    while (len < pos.size() && pos[len] >= u'0' && pos[len] <= u'9') {
        ++len;
    }
    if (len == 0) {
        return false;
    }
    const QString digits = pos.first(len).toString();
    pos = pos.sliced(len);
    int parsed = 0;
    if (!core::delphiTryStrToInt(digits, parsed)) { // a digit run that does not fit Integer
        return false;
    }
    value = parsed;
    return true;
}

} // namespace cg::maptype
