// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Test/u_SASTestCase.pas:38-70 (TSASTestCase.CheckDoublePointsEquals,
// u_ProjectionSet_Test.pas:69-76 cEpsilon). Shared by every ported projection test
// (docs/specs/geo.md §11.0).

#include "cg/geo/geo_types.h"

#include <gtest/gtest.h>

#include <cmath>

namespace cg::geo::test {

// Ports TSASTestCase.CheckDoublePointsEquals. Note the "both empty" rule: when BOTH points are
// NaN-empty, legacy performs no check at all and passes. Reproduce it.
[[nodiscard]] inline ::testing::AssertionResult DoublePointsEqual(DoublePoint expected, DoublePoint actual,
                                                                  double delta) {
    const bool expectedEmpty = std::isnan(expected.x()) || std::isnan(expected.y());
    const bool actualEmpty = std::isnan(actual.x()) || std::isnan(actual.y());
    if (expectedEmpty && actualEmpty) {
        return ::testing::AssertionSuccess();
    }
    if (expectedEmpty != actualEmpty) {
        return ::testing::AssertionFailure()
               << "one of the points is empty (NaN) and the other is not: expected=(" << expected.x() << ", "
               << expected.y() << ") actual=(" << actual.x() << ", " << actual.y() << ")";
    }
    if (std::abs(expected.x() - actual.x()) > delta || std::abs(expected.y() - actual.y()) > delta) {
        return ::testing::AssertionFailure()
               << "expected (" << expected.x() << ", " << expected.y() << ") actual (" << actual.x() << ", "
               << actual.y() << ") delta " << delta;
    }
    return ::testing::AssertionSuccess();
}

// u_ProjectionSet_Test.pas:69-76. cEpsilon = 1/(1 shl 30 + (1 shl 30 - 1)) = 1/2147483647.
inline constexpr double kProjectionSetEpsilon = 1.0 / 2147483647.0;

} // namespace cg::geo::test
