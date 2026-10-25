// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-004. Ports u_MathExpressionEvaluator.pas; docs/specs/maptype.md 2.8, 7.3
// (trailing-text and failure cases; no legacy test exists, vectors derived by hand from the Pascal).
#include "cg/maptype/math_expression_evaluator.h"

#include <QString>

#include <gtest/gtest.h>

using cg::maptype::MathExpressionEvaluator;

namespace {

TEST(MathExpressionEvaluator, VectorsForTileX200Y300Z18) {
    struct Row {
        const char16_t* expr;
        const char16_t* want;
    };
    const Row rows[] = {
        {u"z-1", u"17"},
        {u"x*2", u"400"},
        {u"y/2", u"150"},
        {u"z+1", u"19"},
        {u"x/1024", u"0"},
        {u"y/1024", u"0"},
        {u"x+y*2", u"800"},
        {u"y-x-z", u"82"},
        {u"y/7", u"42"},
        {u"10-3-2", u"5"},
        {u"100/7/2", u"7"},
        {u"7", u"7"},
        {u"x*y", u"60000"},
        {u"x", u"200"},
        {u"2*3+4*5", u"26"},
        // trailing text is not an error: evaluation stops where the expression cannot continue (L-137)
        {u"z + 1", u"18"},
        {u"x*2abc", u"400"},
        {u"x y", u"200"},
        // failures give ""
        {u"", u""},
        {u"+1", u""},
        {u"-1", u""},
        {u"x+", u""},
        {u"x*", u""},
        {u"x/0", u""},
        {u"x/y-z/0", u""},
        {u"abc", u""},
        {u"{z-1", u""},
        {u" 1", u""},
        {u"X*2", u""}, // the template lower-cases, the evaluator does not
        {u"2147483648", u""},
        {u"2147483647", u"2147483647"},
        // 32-bit wrap
        {u"2147483647+1", u"-2147483648"},
        {u"65536*65536", u"0"},
    };
    MathExpressionEvaluator eval;
    for (const Row& r : rows) {
        SCOPED_TRACE(QString::fromUtf16(r.expr).toStdString());
        EXPECT_EQ(eval.evaluate(r.expr, 200, 300, 18), QString::fromUtf16(r.want));
    }
}

} // namespace
