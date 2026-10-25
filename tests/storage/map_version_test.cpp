// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-032 (model only). Ports Src/MapVersion (u_MapVersionInfo.pas,
// u_MapVersionFactorySimpleString.pas, u_MapVersionRequest.pas); docs/specs/storage.md §2.2
#include "cg/core/hash.h"
#include "cg/storage/map_version.h"

#include <QString>

#include <gtest/gtest.h>

using cg::core::HashValue;
using cg::core::kNoHash;
using cg::storage::MapVersionFactory;
using cg::storage::MapVersionInfo;
using cg::storage::MapVersionInfoPtr;
using cg::storage::MapVersionRequest;

namespace {

TEST(MapVersion, StoreStringAndHashReturnConstructorValues) {
    const MapVersionInfo v(QStringLiteral("layer-2"), HashValue{42});
    EXPECT_EQ(v.storeString(), QStringLiteral("layer-2"));
    EXPECT_EQ(v.hash(), HashValue{42});
}

TEST(MapVersion, IsEmptyTrueForDefaultFalseForNonEmpty) {
    const MapVersionInfo empty;
    EXPECT_TRUE(empty.isEmpty());
    const MapVersionInfo nonEmpty(QStringLiteral("v1"), kNoHash);
    EXPECT_FALSE(nonEmpty.isEmpty());
}

TEST(MapVersion, IsSameSameObjectIdentityReturnsTrueRegardlessOfHash) {
    const MapVersionInfo v(QStringLiteral("v1"), HashValue{123});
    EXPECT_TRUE(v.isSame(v));
}

TEST(MapVersion, IsSameBothHashesNonZeroAndDifferReturnsFalseEvenIfStringsMatch) {
    const MapVersionInfo a(QStringLiteral("same"), HashValue{1});
    const MapVersionInfo b(QStringLiteral("same"), HashValue{2});
    EXPECT_FALSE(a.isSame(b));
}

TEST(MapVersion, IsSameBothHashesZeroFallsBackToStringEquality) {
    const MapVersionInfo a(QStringLiteral("same"), kNoHash);
    const MapVersionInfo b(QStringLiteral("same"), kNoHash);
    const MapVersionInfo c(QStringLiteral("other"), kNoHash);
    EXPECT_TRUE(a.isSame(b));
    EXPECT_FALSE(a.isSame(c));
}

TEST(MapVersion, IsSameOneHashZeroFallsBackToStringEquality) {
    const MapVersionInfo a(QStringLiteral("same"), kNoHash);
    const MapVersionInfo b(QStringLiteral("same"), HashValue{999});
    EXPECT_TRUE(a.isSame(b));
}

TEST(MapVersion, IsSameDifferentStringsReturnsFalseEvenWithEqualHash) {
    const MapVersionInfo a(QStringLiteral("foo"), HashValue{111});
    const MapVersionInfo b(QStringLiteral("bar"), HashValue{111});
    EXPECT_FALSE(a.isSame(b));
}

TEST(MapVersion, RequestShowOtherVersionsAlwaysValid) {
    MapVersionRequest req;
    req.showOtherVersions = true;
    EXPECT_TRUE(req.isValid(nullptr));
    EXPECT_TRUE(req.isValid(std::make_shared<const MapVersionInfo>(QStringLiteral("x"), kNoHash)));
}

TEST(MapVersion, RequestNullBaseVersionOnlyNullVersionIsValid) {
    // Deviation D-1: legacy crashes dereferencing a null FBaseVersion here; the port treats it as
    // "only the empty/unversioned request matches".
    MapVersionRequest req; // baseVersion stays null, showOtherVersions stays false
    EXPECT_TRUE(req.isValid(nullptr));
    EXPECT_FALSE(req.isValid(std::make_shared<const MapVersionInfo>(QStringLiteral("x"), kNoHash)));
}

TEST(MapVersion, RequestNonNullBaseVersionUsesIsSame) {
    MapVersionRequest req;
    req.baseVersion = std::make_shared<const MapVersionInfo>(QStringLiteral("a"), HashValue{1});
    EXPECT_TRUE(req.isValid(std::make_shared<const MapVersionInfo>(QStringLiteral("a"), HashValue{1})));
    EXPECT_FALSE(req.isValid(std::make_shared<const MapVersionInfo>(QStringLiteral("b"), HashValue{2})));
    EXPECT_FALSE(req.isValid(nullptr));
}

TEST(MapVersion, FactoryInternsEqualStoreStrings) {
    MapVersionFactory factory;
    const MapVersionInfoPtr p1 = factory.byStoreString(QStringLiteral("abc"));
    const MapVersionInfoPtr p2 = factory.byStoreString(QStringLiteral("abc"));
    const MapVersionInfoPtr p3 = factory.byStoreString(QStringLiteral("xyz"));
    EXPECT_EQ(p1.get(), p2.get());
    EXPECT_NE(p1.get(), p3.get());
    EXPECT_EQ(p1->storeString(), QStringLiteral("abc"));
}

TEST(MapVersion, FactoryByVersionNullReturnsInternedEmptyVersion) {
    MapVersionFactory factory;
    const MapVersionInfoPtr empty = factory.byVersion(nullptr);
    EXPECT_TRUE(empty->isEmpty());
    EXPECT_EQ(empty.get(), factory.byStoreString(QString()).get());

    const MapVersionInfoPtr v1 = factory.byStoreString(QStringLiteral("v1"));
    const MapVersionInfoPtr v2 = factory.byVersion(v1);
    EXPECT_EQ(v1.get(), v2.get());
}

} // namespace
