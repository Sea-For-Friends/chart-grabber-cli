// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-001 (prerequisite slice, held open). Ports MapType/u_MapTypeSetBuilderFactory.pas
// (TMapTypeSet, TMapTypeSetBuilder) and Basic/u_GUIDInterfaceSetOrdered.pas; spec maptype.md §2.11.2.
#include "cg/core/config_provider_ini.h"
#include "cg/core/hash.h"
#include "cg/maptype/map_type_set.h"

#include <QDirIterator>
#include <QUuid>

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "fake_map_type.h"
#include "maptype_test_util.h"

using cg::maptype::IMapTypePtr;
using cg::maptype::IMapTypeSet;
using cg::maptype::IMapTypeSetPtr;
using cg::maptype::MapTypeSetBuilderFactory;
using cg::maptype::test::buildSet;
using cg::maptype::test::fakeMap;
using cg::maptype::test::guidN;

namespace {

// The GUIDs of the 514 stock ZMPs, in the order the directory iterator yields them.
std::vector<QUuid> stockGuids() {
    std::vector<QUuid> out;
    QDirIterator it(cg::maptype::test::fixtureDir() + QStringLiteral("/zmp/sas.maps"),
                    {QStringLiteral("params.txt")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto ini = cg::core::ConfigProviderIni::open(it.next());
        EXPECT_TRUE(ini.has_value());
        out.push_back((*ini)->subItem(u"PARAMS")->readUuid(u"GUID", QUuid()));
    }
    return out;
}

// A set that reports hash 0 ("not computed"), to reach the branches the builder never produces.
class UnhashedSet final : public IMapTypeSet {
public:
    explicit UnhashedSet(std::vector<IMapTypePtr> items) : items_(std::move(items)) {}
    cg::core::HashValue hash() const override { return cg::core::kNoHash; }
    int count() const override { return static_cast<int>(items_.size()); }
    IMapTypePtr item(int i) const override { return items_.at(static_cast<std::size_t>(i)); }
    bool isEqual(const IMapTypeSetPtr&) const override { return false; }
    bool isExists(const QUuid&) const override { return false; }
    IMapTypePtr mapTypeByGuid(const QUuid&) const override { return nullptr; }

private:
    std::vector<IMapTypePtr> items_;
};

TEST(MapTypeSet, StockCorpusKeepsInsertionOrderAndLooksEveryGuidUp) {
    CG_SKIP_IF_NO_CORPUS();
    const std::vector<QUuid> guids = stockGuids();
    ASSERT_EQ(guids.size(), 514u);
    std::vector<IMapTypePtr> maps;
    for (const QUuid& g : guids) {
        ASSERT_FALSE(g.isNull());
        maps.push_back(fakeMap(g));
    }
    const auto set = buildSet(maps);
    ASSERT_NE(set, nullptr);
    ASSERT_EQ(set->count(), 514); // all stock GUIDs are distinct
    for (int i = 0; i < 514; ++i) {
        EXPECT_EQ(set->item(i), maps[static_cast<std::size_t>(i)]) << i;
        EXPECT_EQ(set->mapTypeByGuid(guids[static_cast<std::size_t>(i)]), maps[static_cast<std::size_t>(i)])
            << i;
        EXPECT_TRUE(set->isExists(guids[static_cast<std::size_t>(i)])) << i;
    }
    EXPECT_FALSE(set->isExists(guidN(999)));
    EXPECT_EQ(set->mapTypeByGuid(guidN(999)), nullptr);
}

TEST(MapTypeSet, InsertionOrderIsNotGuidOrderAndItemIsBoundsChecked) {
    const auto a = fakeMap(guidN(9));
    const auto b = fakeMap(guidN(1));
    const auto c = fakeMap(guidN(5));
    const auto set = buildSet({a, b, c});
    ASSERT_EQ(set->count(), 3);
    EXPECT_EQ(set->item(0), a);
    EXPECT_EQ(set->item(1), b);
    EXPECT_EQ(set->item(2), c);
    EXPECT_THROW((void)set->item(3), std::out_of_range);
    EXPECT_THROW((void)set->item(-1), std::out_of_range);
}

TEST(MapTypeSetBuilder, DuplicateGuidIsIgnoredAndTheFirstItemStays) {
    const auto first = fakeMap(guidN(1));
    const auto other = fakeMap(guidN(2));
    const auto dup = fakeMap(guidN(1)); // another object, same GUID
    auto b = MapTypeSetBuilderFactory().build(false);
    b->add(first);
    b->add(other);
    b->add(dup);
    b->add(first);
    EXPECT_EQ(b->count(), 2);
    const auto set = b->makeAndClear();
    ASSERT_EQ(set->count(), 2);
    EXPECT_EQ(set->item(0), first);
    EXPECT_EQ(set->mapTypeByGuid(guidN(1)), first);
}

TEST(MapTypeSetBuilder, HashMatchesTheChainAndDependsOnContentAndOrder) {
    const auto a = fakeMap(guidN(1));
    const auto b2 = fakeMap(guidN(2));
    const auto c = fakeMap(guidN(3));
    auto builder = MapTypeSetBuilderFactory().build(false);
    for (const auto& m : {a, b2, c}) {
        builder->add(m);
    }
    const auto copy = builder->makeCopy();
    EXPECT_EQ(builder->count(), 3); // makeCopy keeps the content
    const auto moved = builder->makeAndClear();
    cg::core::HashValue want = 0x049626A97A946096ULL;
    for (int n : {1, 2, 3}) {
        cg::core::hashUpdate(want, guidN(n));
    }
    EXPECT_EQ(copy->hash(), want);
    EXPECT_EQ(moved->hash(), want);
    EXPECT_NE(buildSet({c, b2, a})->hash(), want); // a permutation
    EXPECT_NE(buildSet({a, b2})->hash(), want);
}

TEST(MapTypeSetBuilder, EmptyBuilderYieldsNullAndMakeAndClearEmptiesIt) {
    auto b = MapTypeSetBuilderFactory().build(false);
    EXPECT_EQ(b->makeCopy(), nullptr);
    EXPECT_EQ(b->makeAndClear(), nullptr);
    b->setCapacity(16);
    EXPECT_GE(b->capacity(), 16);
    EXPECT_EQ(b->makeAndClear(), nullptr); // reserved but empty: still null, reserve kept
    b->add(fakeMap(guidN(1)));
    ASSERT_NE(b->makeAndClear(), nullptr);
    EXPECT_EQ(b->count(), 0);
    EXPECT_EQ(b->capacity(), 0);
    EXPECT_EQ(b->makeAndClear(), nullptr);
    b->add(fakeMap(guidN(2)));
    b->clear();
    EXPECT_EQ(b->count(), 0);
    EXPECT_EQ(b->capacity(), 0);
    EXPECT_EQ(b->makeCopy(), nullptr);
}

TEST(MapTypeSetBuilder, CapacityBelowTheCountIsAnError) {
    auto b = MapTypeSetBuilderFactory().build(false);
    b->setCapacity(0); // nothing to forward to
    EXPECT_EQ(b->capacity(), 0);
    b->add(fakeMap(guidN(1)));
    b->add(fakeMap(guidN(2)));
    EXPECT_THROW(b->setCapacity(1), std::length_error);
    EXPECT_THROW(b->setCapacity(0), std::length_error); // forwarded to the existing list
    b->setCapacity(8);
    EXPECT_GE(b->capacity(), 8);
}

TEST(MapTypeSetBuilder, NilItems) {
    {
        auto strict = MapTypeSetBuilderFactory().build(false);
        try {
            strict->add(nullptr);
            FAIL() << "no exception";
        } catch (const std::invalid_argument& e) {
            EXPECT_STREQ(e.what(), "Interface is nil");
        }
        EXPECT_EQ(strict->count(), 0);
    }
    auto lenient = MapTypeSetBuilderFactory().build(true);
    lenient->add(nullptr);
    lenient->add(nullptr); // the second null has the zero GUID too: ignored
    lenient->add(fakeMap(guidN(1)));
    EXPECT_EQ(lenient->count(), 2);
    const auto set = lenient->makeCopy();
    ASSERT_EQ(set->count(), 2);
    EXPECT_EQ(set->item(0), nullptr);
    EXPECT_EQ(set->item(1)->guid(), guidN(1));
    EXPECT_FALSE(set->isExists(QUuid())); // exists means "lookup is not null"
    EXPECT_EQ(set->mapTypeByGuid(QUuid()), nullptr);
}

TEST(MapTypeSet, IsEqualRules) {
    const auto a = fakeMap(guidN(1));
    const auto b = fakeMap(guidN(2));
    const auto a2 = fakeMap(guidN(1)); // same GUID, another object
    const auto set = buildSet({a, b});

    struct Row {
        const char* name;
        IMapTypeSetPtr other;
        bool equal;
    };
    const Row rows[] = {
        {"null", nullptr, false},
        {"self", set, true},
        {"same items, other builder", buildSet({a, b}), true},
        {"same GUIDs, other objects", buildSet({a2, b}), false},
        {"permutation (hash differs)", buildSet({b, a}), false},
        {"fewer items", buildSet({a}), false},
        {"more items", buildSet({a, b, fakeMap(guidN(3))}), false},
        {"hash 0 skips the hash test, equal items",
         std::make_shared<UnhashedSet>(std::vector<IMapTypePtr>{a, b}), true},
        {"hash 0, permuted items", std::make_shared<UnhashedSet>(std::vector<IMapTypePtr>{b, a}), false},
        {"hash 0, other count", std::make_shared<UnhashedSet>(std::vector<IMapTypePtr>{a}), false},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        EXPECT_EQ(set->isEqual(r.other), r.equal);
    }
}

} // namespace
