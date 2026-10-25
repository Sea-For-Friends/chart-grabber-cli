// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-001 (prerequisite slice, held open). Ports MapType/u_MapTypeSetChangeableSimple.pas;
// spec maptype.md §2.11.4.
#include "cg/maptype/map_type_set_changeable_simple.h"

#include <gtest/gtest.h>

#include "fake_map_type.h"

using cg::maptype::IMapTypeSetPtr;
using cg::maptype::MapTypeSetBuilderFactory;
using cg::maptype::MapTypeSetChangeableSimple;
using cg::maptype::test::fakeMap;

namespace {

IMapTypeSetPtr oneMapSet(int n) {
    auto b = MapTypeSetBuilderFactory().build(false);
    b->add(fakeMap(QUuid(static_cast<uint>(n), 0, 0, 0, 0, 0, 0, 0, 0, 0, 1)));
    return b->makeAndClear();
}

std::shared_ptr<const cg::maptype::IMapTypeSetBuilderFactory> factory() {
    return std::make_shared<MapTypeSetBuilderFactory>();
}

TEST(MapTypeSetChangeableSimple, ConstructorStoresTheSetOrNull) {
    const auto set = oneMapSet(1);
    EXPECT_EQ(MapTypeSetChangeableSimple(factory(), set).getStatic(), set);
    EXPECT_EQ(MapTypeSetChangeableSimple(factory(), nullptr).getStatic(), nullptr);
}

// u_MapTypeSetChangeableSimple.pas:76-92 never calls a notify routine: no phase fires, whether
// the value differs, is the same object, an equal set, or null.
TEST(MapTypeSetChangeableSimple, SetStaticNeverNotifies) {
    const auto first = oneMapSet(1);
    MapTypeSetChangeableSimple holder(factory(), first);
    int fired = 0;
    const auto s1 = holder.beforeChanged().subscribe([&] { ++fired; });
    const auto s2 = holder.changed().subscribe([&] { ++fired; });
    const auto s3 = holder.afterChanged().subscribe([&] { ++fired; });

    const auto other = oneMapSet(2);
    const auto equalToFirst = oneMapSet(1); // different pointers inside: not isEqual, but also no event
    struct Row {
        const char* name;
        IMapTypeSetPtr value;
        IMapTypeSetPtr expectedAfter;
    };
    const Row rows[] = {
        {"different set", other, other},
        {"same object again", other, other},
        {"set built from the same content", equalToFirst, equalToFirst},
        {"null (empty no-nil builder result is null)", nullptr, nullptr},
        {"back to a set", first, first},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        holder.setStatic(r.value);
        EXPECT_EQ(holder.getStatic(), r.expectedAfter);
        EXPECT_EQ(fired, 0);
    }
}

} // namespace
