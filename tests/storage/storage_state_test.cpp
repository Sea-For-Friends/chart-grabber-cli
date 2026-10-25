// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-001. Ports i_StorageState.pas, i_StorageStateInternal.pas,
// u_StorageStateInternal.pas, u_StorageStateStatic.pas; docs/specs/storage.md §2.7 (:503-530)
#include "cg/storage/storage_state.h"

#include <gtest/gtest.h>

using cg::storage::StorageStateHolder;
using cg::storage::TileStorageAbilities;

namespace {

TEST(StorageStateHolder, InitialStateMatchesConstructorAbilities) {
    const StorageStateHolder holder(TileStorageAbilities{true, false, true, false, true});
    const auto s = holder.get();
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->readAccess);
    EXPECT_FALSE(s->scanAccess);
    EXPECT_TRUE(s->addAccess);
    EXPECT_FALSE(s->deleteAccess);
    EXPECT_TRUE(s->replaceAccess);
}

TEST(StorageStateHolder, SetScanAddDeleteReplaceAreClampedByConstructorAbilities) {
    // Ability ceiling forbids scan/add; setters must AND with it (u_StorageStateInternal.pas:
    // SetScanAccess/SetAddAccess/SetDeleteAccess/SetReplaceAccess).
    StorageStateHolder holder(TileStorageAbilities{true, false, false, true, true});
    holder.setScanAccess(true);
    holder.setAddAccess(true);
    holder.setDeleteAccess(true);
    holder.setReplaceAccess(true);
    const auto s = holder.get();
    EXPECT_FALSE(s->scanAccess);  // ability forbids scan -- stays false
    EXPECT_FALSE(s->addAccess);   // ability forbids add -- stays false
    EXPECT_TRUE(s->deleteAccess); // ability allows delete -- follows the request
    EXPECT_TRUE(s->replaceAccess);
}

TEST(StorageStateHolder, SetReadAccessIsNotClampedByAbility) {
    // Legacy quirk, reproduced verbatim: SetReadAccess(AValue) stores AValue unconditionally,
    // even though the constructor ability forbids read (u_StorageStateInternal.pas:SetReadAccess
    // has no `and FStorageForceAbilities.AllowRead`, unlike its four siblings).
    StorageStateHolder holder(TileStorageAbilities::none());
    holder.setReadAccess(true);
    EXPECT_TRUE(holder.get()->readAccess);
}

TEST(StorageStateHolder, ChangedFiresOnlyOnARealTransition) {
    StorageStateHolder holder(TileStorageAbilities::readWrite());
    int fired = 0;
    const auto sub = holder.changed().subscribe([&fired] { ++fired; });

    holder.setAddAccess(false); // true -> false: real transition
    EXPECT_EQ(fired, 1);

    holder.setAddAccess(false); // false -> false: no transition
    EXPECT_EQ(fired, 1);

    holder.setAddAccess(true); // false -> true: real transition
    EXPECT_EQ(fired, 2);
}

TEST(StorageStateHolder, GetReturnsAStableSnapshotAcrossLaterMutation) {
    StorageStateHolder holder(TileStorageAbilities::readWrite());
    const auto before = holder.get();
    holder.setDeleteAccess(false);
    EXPECT_TRUE(before->deleteAccess);        // the old snapshot is untouched
    EXPECT_FALSE(holder.get()->deleteAccess); // a fresh get() sees the new state
}

} // namespace
