// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-037, LV-3, LV-4. Ports u_VectorItemSubset.pas, u_VectorItemSubsetBuilder.pas;
// docs/specs/vector.md §2.6, §7.2.
#include "cg/vector/vector_item_subset_builder.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace cg::vector;
using cg::core::HashValue;

namespace {

// Hash and equality key are free: isEqual is the rungs of VectorDataItem (identity, hash rung, key).
class FakeItem final : public IVectorDataItem {
public:
    FakeItem(HashValue hash, QString key) : hash_(hash), key_(std::move(key)) {}
    HashValue hash() const noexcept override { return hash_; }
    VectorDataItemMainInfoPtr mainInfo() const override { return nullptr; }
    QString name() const override { return key_; }
    QString desc() const override { return {}; }
    cg::geo::GeometryLonLatPtr geometry() const override { return nullptr; }
    AppearancePtr appearance() const override { return nullptr; }
    bool isEqual(const IVectorDataItem* item) const override {
        if (item == nullptr) {
            return false;
        }
        if (item == this) {
            return true;
        }
        if (item->hash() != 0 && hash_ != 0 && item->hash() != hash_) {
            return false;
        }
        return item->name() == key_;
    }
    QString hintText() const override { return {}; }
    QString infoUrl() const override { return {}; }
    QString infoHtml() const override { return {}; }
    QString infoCaption() const override { return {}; }

private:
    HashValue hash_;
    QString key_;
};

VectorDataItemPtr fake(HashValue hash, const char* key) {
    return std::make_shared<FakeItem>(hash, QString::fromLatin1(key));
}

// UpdateHashByHash by hand: CityHash64WithSeed over the 8 little-endian bytes of the value.
HashValue chainStep(HashValue running, HashValue value) {
    unsigned char b[8];
    for (int i = 0; i < 8; ++i) {
        b[i] = static_cast<unsigned char>(value >> (8 * i));
    }
    return cg::core::cityHash64WithSeed(b, 8, running);
}

} // namespace

TEST(VectorItemSubset, IsEqualLadder) {
    const auto a = fake(10, "a");
    const auto b = fake(20, "b");
    const auto a2 = fake(10, "a"); // equal to a, other object
    const auto c = fake(30, "c");
    const VectorItemSubset ab(7, {a, b});
    struct Case {
        const char* name;
        VectorItemSubset other;
        bool expected;
    };
    const Case cases[] = {
        {"same content, same hash", VectorItemSubset(7, {a2, b}), true},
        {"counts differ", VectorItemSubset(7, {a, b, c}), false},
        {"counts differ before the hash rung is looked at", VectorItemSubset(0, {a}), false},
        {"both hashes non-zero and different: false although items equal", VectorItemSubset(8, {a2, b}),
         false},
        {"own hash 0 skips the hash rung", VectorItemSubset(0, {a2, b}), true},
        {"element mismatch", VectorItemSubset(7, {a, c}), false},
        {"order matters", VectorItemSubset(7, {b, a}), false},
    };
    for (const auto& tc : cases) {
        EXPECT_EQ(ab.isEqual(&tc.other), tc.expected) << tc.name;
    }
    EXPECT_FALSE(ab.isEqual(nullptr));
    EXPECT_TRUE(ab.isEqual(&ab));
    const VectorItemSubset zeroAb(0, {a, b});
    EXPECT_TRUE(zeroAb.isEqual(&ab)); // the zero is on this side
}

TEST(VectorItemSubset, EmptyAndItemAccess) {
    const VectorItemSubset empty(0, {});
    EXPECT_TRUE(empty.isEmpty());
    EXPECT_EQ(empty.count(), 0);
    const auto a = fake(10, "a");
    const VectorItemSubset one(5, {a});
    EXPECT_FALSE(one.isEmpty());
    EXPECT_EQ(one.hash(), 5u);
    EXPECT_EQ(one.item(0), a);
    EXPECT_THROW((void)one.item(1), std::out_of_range); // D-61 regression
    EXPECT_THROW((void)one.item(-1), std::out_of_range);
}

TEST(VectorItemSubsetBuilder, HashChainAndStaticResults) {
    VectorItemSubsetBuilder builder;
    EXPECT_EQ(builder.makeStaticAndClear(), nullptr); // empty builder: null, not an empty subset
    EXPECT_EQ(builder.makeStaticCopy(), nullptr);
    EXPECT_EQ(builder.count(), 0);
    EXPECT_EQ(builder.capacity(), 0);

    EXPECT_EQ(builder.add(fake(100, "x")), 0);
    EXPECT_EQ(builder.makeStaticCopy()->hash(), 100u); // first add: the item's own hash
    EXPECT_EQ(builder.add(fake(200, "y")), 1);
    EXPECT_EQ(builder.add(fake(300, "z")), 2);
    const HashValue expected = chainStep(chainStep(100, 200), 300);

    const auto copy = builder.makeStaticCopy();
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->hash(), expected);
    EXPECT_EQ(builder.count(), 3); // a copy leaves the builder alone

    const auto taken = builder.makeStaticAndClear();
    ASSERT_NE(taken, nullptr);
    EXPECT_EQ(taken->hash(), expected);
    EXPECT_EQ(taken->count(), 3);
    EXPECT_TRUE(taken->isEqual(copy.get()));
    EXPECT_EQ(builder.count(), 0);
    EXPECT_EQ(builder.makeStaticAndClear(), nullptr);

    // Clear keeps the old hash (L-150) but the next first add overwrites it: restarts the chain.
    builder.add(fake(1, "a"));
    builder.add(fake(2, "b"));
    builder.clear();
    EXPECT_EQ(builder.count(), 0);
    builder.add(fake(50, "c"));
    EXPECT_EQ(builder.makeStaticAndClear()->hash(), 50u);
}

TEST(VectorItemSubsetBuilder, CapacityItemAndNullArgument) {
    VectorItemSubsetBuilder builder;
    builder.setCapacity(16);
    EXPECT_GE(builder.capacity(), 16);
    const auto a = fake(1, "a");
    builder.add(a);
    EXPECT_EQ(builder.item(0), a);
    EXPECT_THROW((void)builder.item(1),
                 std::out_of_range); // D-61 regression (legacy: nil dereference / list error)
    EXPECT_THROW((void)VectorItemSubsetBuilder().item(0), std::out_of_range);
    EXPECT_EQ(builder.add(nullptr), -1); // explicit guard in legacy, same in every build
    EXPECT_EQ(builder.count(), 1);
    EXPECT_THROW(builder.setCapacity(0), std::invalid_argument); // below count, legacy EListError
    const auto built = VectorItemSubsetBuilderFactory().build();
    EXPECT_EQ(built->add(a), 0);
    EXPECT_EQ(built->count(), 1);
}

// LV-3, LV-4. Sorted descending by hash with the literal quicksort; of equal neighbours the FIRST in
// sorted order survives. Hand trace of the 4-element input in docs/handoffs/T-0057.md.
TEST(VectorItemSubsetBuilder, RemoveDuplicatesOrderAndSurvivor) {
    const auto a = fake(3, "low");  // equal to c
    const auto b = fake(9, "high"); // equal to d
    const auto c = fake(3, "low");
    const auto d = fake(9, "high");
    VectorItemSubsetBuilder builder;
    for (const auto& item : {a, b, c, d}) {
        builder.add(item);
    }
    builder.removeDuplicates();
    // The quicksort leaves [d, b, a, c] (a stable sort would give [b, d, a, c]): survivors d and a.
    ASSERT_EQ(builder.count(), 2);
    EXPECT_EQ(builder.item(0), d);
    EXPECT_EQ(builder.item(1), a);
    EXPECT_EQ(builder.makeStaticAndClear()->hash(), chainStep(9, 3)); // first.hash, then each survivor
}

TEST(VectorItemSubsetBuilder, RemoveDuplicatesComparesNeighboursOnly) {
    // Equal hash 5 for all three: the sort turns [x1, y, x2] into [x2, y, x1]; the equal pair
    // x1/x2 is no longer adjacent to each other, so nothing is removed (LV-4).
    const auto x1 = fake(5, "x");
    const auto y = fake(5, "y");
    const auto x2 = fake(5, "x");
    VectorItemSubsetBuilder builder;
    builder.add(x1);
    builder.add(y);
    builder.add(x2);
    builder.removeDuplicates();
    ASSERT_EQ(builder.count(), 3);
    EXPECT_EQ(builder.item(0), x2);
    EXPECT_EQ(builder.item(1), y);
    EXPECT_EQ(builder.item(2), x1);
    EXPECT_EQ(builder.makeStaticAndClear()->hash(), chainStep(chainStep(5, 5), 5));

    // Two equal items: the second one added survives (the sort exchanged them).
    const auto p = fake(5, "p");
    const auto q = fake(5, "p");
    builder.add(p);
    builder.add(q);
    builder.removeDuplicates();
    ASSERT_EQ(builder.count(), 1);
    EXPECT_EQ(builder.item(0), q);
    EXPECT_EQ(builder.makeStaticAndClear()->hash(), 5u);

    // One item: untouched, hash unchanged.
    builder.add(fake(77, "solo"));
    builder.removeDuplicates();
    EXPECT_EQ(builder.makeStaticAndClear()->hash(), 77u);
}
