// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT (core idiom); docs/specs/core.md 2.3, test plan 7.1
#include "cg/core/result.h"

#include <QString>

#include <gtest/gtest.h>

using cg::core::Error;
using cg::core::ErrorCode;
using cg::core::fail;
using cg::core::Result;

namespace {

Result<int> parsePositive(int value) {
    if (value <= 0) {
        return fail(ErrorCode::ParseError, QStringLiteral("not positive"));
    }
    return value;
}

} // namespace

TEST(Result, ValueRoundTrip) {
    const Result<int> result = parsePositive(7);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 7);
}

TEST(Result, ErrorCarriesCodeAndMessage) {
    const Result<int> result = parsePositive(0);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::ParseError);
    EXPECT_EQ(result.error().what(), QStringLiteral("not positive"));
}

TEST(Result, VoidSpecialisation) {
    const Result<void> good{};
    EXPECT_TRUE(good.has_value());
    const Result<void> bad = fail(ErrorCode::IoError, QStringLiteral("disk"));
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code(), ErrorCode::IoError);
}

TEST(Result, ValueOrReturnsFallback) {
    EXPECT_EQ(parsePositive(3).value_or(-1), 3);
    EXPECT_EQ(parsePositive(0).value_or(-1), -1);
}

TEST(Result, FailHelperBuildsUnexpected) {
    const cg::core::Unexpected unexpected = fail(ErrorCode::NotFound, QStringLiteral("gone"));
    EXPECT_EQ(unexpected.error().code(), ErrorCode::NotFound);
    EXPECT_EQ(unexpected.error().what(), QStringLiteral("gone"));

    const Error defaulted;
    EXPECT_EQ(defaulted.code(), ErrorCode::None);
    EXPECT_TRUE(defaulted.what().isEmpty());
}
