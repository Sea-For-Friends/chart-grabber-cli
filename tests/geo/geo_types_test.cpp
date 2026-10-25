// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT id (helper predicate; ported with geo_types, fixed by T-0110).
// Ports: u_GeoFunc.pas PointIsEmpty (:550-552) = IsNan(X) or IsNan(Y). The expected values are
// derived from that Pascal body, not from the port: IsNan(+/-Inf) is False, IsNan(NaN) is True.

#include "cg/geo/geo_types.h"

#include <gtest/gtest.h>

#include <limits>

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

struct PointIsEmptyCase {
    const char* name;
    double x;
    double y;
    bool expected; // u_GeoFunc.pas:552 Result := IsNan(X) or IsNan(Y)
};

} // namespace

TEST(GeoTypes, PointIsEmptyFollowsLegacyXorYNan) {
    const PointIsEmptyCase cases[] = {
        {"finite finite", 1.5, 2.5, false}, {"NaN finite", kNaN, 2.5, true},
        {"finite NaN", 1.5, kNaN, true},    {"NaN NaN", kNaN, kNaN, true},
        {"+Inf finite", kInf, 2.5, false},  {"finite -Inf", 1.5, -kInf, false},
        {"+Inf NaN", kInf, kNaN, true},     {"-Inf -Inf", -kInf, -kInf, false},
    };
    for (const PointIsEmptyCase& c : cases) {
        SCOPED_TRACE(c.name);
        EXPECT_EQ(cg::geo::pointIsEmpty(cg::geo::DoublePoint(c.x, c.y)), c.expected);
    }
}
