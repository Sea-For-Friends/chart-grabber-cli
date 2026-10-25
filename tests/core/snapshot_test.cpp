// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT (core idiom); docs/specs/core.md 2.2, test plan 7.2
#include "cg/core/snapshot.h"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using cg::core::Snapshot;

namespace {

struct Payload {
    explicit Payload(int v) : value(v) {}
    int value;
};

} // namespace

TEST(Snapshot, DefaultIsNull) {
    const Snapshot<Payload> snapshot;
    EXPECT_EQ(snapshot.get(), nullptr);
}

TEST(Snapshot, SetThenGet) {
    Snapshot<Payload> snapshot;
    snapshot.set(std::make_shared<const Payload>(42));
    ASSERT_NE(snapshot.get(), nullptr);
    EXPECT_EQ(snapshot.get()->value, 42);
}

TEST(Snapshot, ReaderKeepsOldValueAlive) {
    Snapshot<Payload> snapshot{std::make_shared<const Payload>(1)};
    const std::shared_ptr<const Payload> held = snapshot.get();
    snapshot.set(std::make_shared<const Payload>(2));
    EXPECT_EQ(held->value, 1);
    EXPECT_EQ(snapshot.get()->value, 2);
}

TEST(Snapshot, ConcurrentReadersSeeConsistentObject) {
    Snapshot<Payload> snapshot{std::make_shared<const Payload>(0)};
    std::atomic<bool> stop{false};
    std::atomic<int> mismatches{0};

    std::vector<std::thread> readers;
    readers.reserve(8);
    for (int i = 0; i < 8; ++i) {
        readers.emplace_back([&snapshot, &mismatches] {
            for (int loop = 0; loop < 10000; ++loop) {
                const std::shared_ptr<const Payload> value = snapshot.get();
                if (value == nullptr || value->value < 0) {
                    ++mismatches;
                }
            }
        });
    }
    std::thread writer([&snapshot, &stop] {
        for (int loop = 1; !stop.load(); ++loop) {
            snapshot.set(std::make_shared<const Payload>(loop));
        }
    });

    for (std::thread& reader : readers) {
        reader.join();
    }
    stop.store(true);
    writer.join();

    EXPECT_EQ(mismatches.load(), 0);
}
