// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-030 (prerequisite slice, held open). Ports MapType/u_MapTypeListBuilderFactory.pas
// (TMapTypeListStatic, TMapTypeListBuilder), i_MapTypeListStatic.pas, i_MapTypeListBuilder.pas;
// spec maptype.md section 2.11.3. Legacy-defect LD-17 (nil item) is pinned here (deviation row).
#include "cg/core/hash.h"
#include "cg/maptype/map_type_list.h"

#include <QUuid>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <vector>

#include "fake_map_type.h"
#include "maptype_test_util.h"

using cg::maptype::IMapTypeListBuilder;
using cg::maptype::IMapTypeListStatic;
using cg::maptype::IMapTypeListStaticPtr;
using cg::maptype::IMapTypePtr;
using cg::maptype::MapTypeListBuilderFactory;
using cg::maptype::test::fakeMap;
using cg::maptype::test::guidN;

namespace {

// u_MapTypeListBuilderFactory.pas:52 (CInitialHash), copied from the Pascal source.
constexpr cg::core::HashValue kLegacyInitialHash = 0xc74bb7e2dee15036ULL;

std::unique_ptr<IMapTypeListBuilder> builderWith(const std::vector<IMapTypePtr>& maps) {
    auto b = MapTypeListBuilderFactory().build();
    for (const auto& m : maps) {
        b->add(m);
    }
    return b;
}

// A list that reports hash 0 ("not computed"), to reach the branch of the other list's isEqual that
// ignores the hash. The builder never produces one. Its own isEqual is never called by these tests.
class UnhashedList final : public IMapTypeListStatic {
public:
    explicit UnhashedList(std::vector<IMapTypePtr> items) : items_(std::move(items)) {}
    cg::core::HashValue hash() const override { return cg::core::kNoHash; }
    int count() const override { return static_cast<int>(items_.size()); }
    IMapTypePtr item(int i) const override { return items_.at(static_cast<std::size_t>(i)); }
    bool isEqual(const IMapTypeListStaticPtr&) const override { return false; }

private:
    std::vector<IMapTypePtr> items_;
};

TEST(MapTypeList, HashIsTheChainOfItemGuidsFromTheLegacyInitialValue) {
    const auto a = fakeMap(guidN(1));
    const auto b = fakeMap(guidN(2));
    const auto c = fakeMap(guidN(3));
    const auto list = builderWith({a, b, c})->makeCopy();
    ASSERT_NE(list, nullptr);
    cg::core::HashValue want = kLegacyInitialHash;
    for (const auto& m : {a, b, c}) {
        cg::core::hashUpdate(want, m->guid());
    }
    EXPECT_EQ(list->hash(), want);
    EXPECT_EQ(builderWith({a, b, c})->makeAndClear()->hash(), want);

    // Duplicates are hashed too (the list keeps them), and a permutation hashes differently.
    cg::core::HashValue dup = kLegacyInitialHash;
    for (const auto& m : {a, a}) {
        cg::core::hashUpdate(dup, m->guid());
    }
    EXPECT_EQ(builderWith({a, a})->makeCopy()->hash(), dup);
    EXPECT_NE(builderWith({c, b, a})->makeCopy()->hash(), want);
    EXPECT_NE(builderWith({a, b})->makeCopy()->hash(), want);
}

TEST(MapTypeList, EmptyBuilderGivesNullAndMakeAndClearEmptiesIt) {
    auto b = MapTypeListBuilderFactory().build();
    EXPECT_EQ(b->makeCopy(), nullptr);
    EXPECT_EQ(b->makeAndClear(), nullptr);
    b->add(fakeMap(guidN(1)));
    const IMapTypeListStaticPtr copy = b->makeCopy();
    b->add(fakeMap(guidN(2)));
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->count(), 1); // a copy does not follow later changes
    const IMapTypeListStaticPtr list = b->makeAndClear();
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->count(), 2);
    EXPECT_EQ(b->count(), 0);
    EXPECT_EQ(b->makeCopy(), nullptr);
}

TEST(MapTypeListBuilder, DuplicatesAndOrderAreKeptAndExchangeRemoveSetItemWork) {
    const auto a = fakeMap(guidN(1));
    const auto b = fakeMap(guidN(2));
    const auto c = fakeMap(guidN(3));
    auto builder = builderWith({a, b, a, c});
    ASSERT_EQ(builder->count(), 4);
    EXPECT_EQ(builder->item(0), a);
    EXPECT_EQ(builder->item(2), a);

    builder->exchange(0, 3); // [c, b, a, a]
    EXPECT_EQ(builder->item(0), c);
    EXPECT_EQ(builder->item(3), a);
    builder->remove(1); // [c, a, a]
    ASSERT_EQ(builder->count(), 3);
    builder->setItem(1, b); // [c, b, a]
    const auto list = builder->makeCopy();
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 3);
    EXPECT_EQ(list->item(0), c);
    EXPECT_EQ(list->item(1), b);
    EXPECT_EQ(list->item(2), a);
}

TEST(MapTypeListBuilder, IndexErrorsThrowOutOfRange) {
    auto builder = builderWith({fakeMap(guidN(1)), fakeMap(guidN(2))});
    EXPECT_THROW(builder->item(2), std::out_of_range);
    EXPECT_THROW(builder->item(-1), std::out_of_range);
    EXPECT_THROW(builder->setItem(2, nullptr), std::out_of_range);
    EXPECT_THROW(builder->remove(2), std::out_of_range);
    EXPECT_THROW(builder->exchange(0, 2), std::out_of_range);
    EXPECT_THROW(builder->exchange(-1, 0), std::out_of_range);
    EXPECT_EQ(builder->count(), 2); // nothing changed
}

TEST(MapTypeListStatic, IsEqualRules) {
    const auto x = fakeMap(guidN(1));
    const auto twinOfX = fakeMap(guidN(1)); // same GUID, another object
    const auto z = fakeMap(guidN(2));
    const IMapTypeListStaticPtr set = builderWith({x, z})->makeCopy();
    const IMapTypeListStaticPtr onlyX = builderWith({x})->makeCopy();
    EXPECT_EQ(onlyX->hash(), builderWith({twinOfX})->makeCopy()->hash()); // the hash sees GUIDs only
    EXPECT_FALSE(onlyX->isEqual(builderWith({twinOfX})->makeCopy()));     // the comparison sees objects

    struct Row {
        const char* name;
        IMapTypeListStaticPtr subject;
        IMapTypeListStaticPtr other;
        bool equal;
    };
    const std::vector<Row> rows = {
        {"null", set, nullptr, false},
        {"self", set, set, true},
        {"same items, other builder", set, builderWith({x, z})->makeCopy(), true},
        {"same GUIDs, other objects", set, builderWith({twinOfX, z})->makeCopy(), false},
        {"permutation (hash differs)", set, builderWith({z, x})->makeCopy(), false},
        {"fewer items", set, onlyX, false},
        {"other list hash 0, equal items", set,
         std::make_shared<UnhashedList>(std::vector<IMapTypePtr>{x, z}), true},
        {"other list hash 0, permuted items", set,
         std::make_shared<UnhashedList>(std::vector<IMapTypePtr>{z, x}), false},
        {"other list hash 0, other count", set, std::make_shared<UnhashedList>(std::vector<IMapTypePtr>{x}),
         false},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        EXPECT_EQ(r.subject->isEqual(r.other), r.equal);
    }
}

// LD-17 (spec maptype.md section 8.5): legacy dereferences a nil item in the hash loop (access
// violation). The port throws std::logic_error before the builder changes; add(nullptr) is accepted.
TEST(MapTypeListBuilder, NilItemIsAcceptedByAddButNotByMakeCopyOrMakeAndClear) {
    const auto a = fakeMap(guidN(1));
    auto builder = builderWith({a, nullptr});
    ASSERT_EQ(builder->count(), 2);
    EXPECT_EQ(builder->item(1), nullptr);
    EXPECT_THROW(builder->makeCopy(), std::logic_error);
    EXPECT_THROW(builder->makeAndClear(), std::logic_error);
    EXPECT_EQ(builder->count(), 2);
    EXPECT_EQ(builder->item(0), a);

    builder->setItem(1, fakeMap(guidN(2)));
    EXPECT_EQ(builder->makeAndClear()->count(), 2);
}

// Legacy: a negative capacity is a no-op while FList is nil and raises once the list exists
// (u_InterfaceListSimple.pas:274-284). The list exists after Add or a positive capacity, survives
// Clear, and is handed over (nil again) by MakeAndClear.
TEST(MapTypeListBuilder, CapacityBelowTheCountAndNegativeValuesFollowTheLegacyList) {
    auto fresh = MapTypeListBuilderFactory().build();
    EXPECT_NO_THROW(fresh->setCapacity(-1));

    auto builder = builderWith({fakeMap(guidN(1)), fakeMap(guidN(2)), fakeMap(guidN(3))});
    EXPECT_THROW(builder->setCapacity(2), std::length_error);
    builder->setCapacity(10);
    EXPECT_GE(builder->capacity(), 10);
    builder->clear();
    EXPECT_EQ(builder->count(), 0);
    EXPECT_EQ(builder->capacity(), 0);
    EXPECT_THROW(builder->setCapacity(-1), std::length_error); // the list exists after Clear
    EXPECT_NO_THROW(builder->setCapacity(0));

    auto handedOver = builderWith({fakeMap(guidN(1))});
    EXPECT_NE(handedOver->makeAndClear(), nullptr);
    EXPECT_NO_THROW(handedOver->setCapacity(-1)); // FList is nil again
}

} // namespace
