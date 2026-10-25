// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MathExpressionEvaluator.pas
//
// The integer arithmetic of a URL-template tag such as {z-1} or {x*2}: grammar
// expr := term (('+'|'-') term)*, term := number (('*'|'/') number)*, number := 'x'|'y'|'z'|digits.
// 32-bit wrapping arithmetic, truncating division. Trailing text after a valid expression is not
// an error (docs/parity/legacy-defects.md, L-137).

#include <QString>
#include <QStringView>
#include <QtGlobal>

namespace cg::maptype {

class MathExpressionEvaluator {
public:
    // "" when the expression is empty or any operand is missing / does not fit Integer / divides by zero.
    // FEAT-NET-004
    [[nodiscard]] QString evaluate(QStringView expression, qint32 x, qint32 y, qint32 z);

private:
    [[nodiscard]] bool tryParseExpression(QStringView& pos, qint32& value);
    [[nodiscard]] bool tryParseTerm(QStringView& pos, qint32& value);
    [[nodiscard]] bool tryParseNumber(QStringView& pos, qint32& value);

    qint32 x_ = 0;
    qint32 y_ = 0;
    qint32 z_ = 0;
};

} // namespace cg::maptype
