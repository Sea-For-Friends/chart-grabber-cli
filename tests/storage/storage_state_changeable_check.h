// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Shared by the per-storage "StateChangeable" tests (T-0117): one check that a storage's
// stateChangeable() is the holder behind its own state(), driven through the interface.
#include "cg/storage/tile_storage.h"

#include <gtest/gtest.h>

namespace cg::storage::test {

// `readBefore` is the literal readAccess the storage is built with. The transition is made on the
// holder itself (setReadAccess is the one setter not clamped by abilities), because only
// SqliteFileTileStorage has a state-changing route in legacy; that route has its own test.
inline void expectStateChangeableIsOwn(ITileStorage& storage, bool readBefore) {
    auto* holder = dynamic_cast<StorageStateHolder*>(&storage.stateChangeable());
    ASSERT_NE(holder, nullptr);
    ASSERT_EQ(storage.state()->readAccess, readBefore);
    EXPECT_EQ(holder->get(), storage.state()); // the same published snapshot object

    int fired = 0;
    bool readSeenInSlot = readBefore;
    const auto sub = storage.stateChangeable().changed().subscribe([&] {
        ++fired;
        readSeenInSlot = storage.state()->readAccess;
    });
    holder->setReadAccess(!readBefore);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(readSeenInSlot, !readBefore); // the snapshot is published before changed() fires
    EXPECT_EQ(storage.state()->readAccess, !readBefore);
    holder->setReadAccess(!readBefore); // no transition: no second notification
    EXPECT_EQ(fired, 1);
}

} // namespace cg::storage::test
