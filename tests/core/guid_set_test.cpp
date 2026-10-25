// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-033, FEAT-GEO-034 (marks prerequisites, T-0055).
// Ports the DUnit suites u_GUIDSetStatic_Test (7), i_IGUIDInterfaceSet_Test (7, run by
// u_GUIDSet_Test for TGUIDInterfaceSet) and i_IGUIDObjectSet_Test (7, run for TGUIDObjectSet);
// same inputs, same expectations, same order of operations. The ID list has no DUnit: its table
// is written from u_IDInterfaceList.pas / u_IDListBase.pas.
#include "cg/core/guid_list_static.h"
#include "cg/core/guid_set.h"
#include "cg/core/id_list.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

using namespace cg::core;

namespace {

const QUuid G1(QStringLiteral("{357CDEAB-14FE-449E-B282-A5B96094BE81}"));
const QUuid G2(QStringLiteral("{357CDEAB-14FE-449E-B282-A5B96094BE82}"));
const QUuid G3(QStringLiteral("{357CDEAB-14FE-449E-B282-A5B96094BE83}"));
const QUuid G4(QStringLiteral("{357CDEAB-14FE-449E-B282-A5B96094BE84}"));
const QUuid G5(QStringLiteral("{86EA601F-EA2D-4E26-BDBE-1C4F65444CA5}"));
const QUuid G6(QStringLiteral("{F81CCA0A-D467-4962-A9F7-2A50B4BFDD46}"));
const QUuid G7(QStringLiteral("{F81CCA0A-D467-4962-A9F7-2A50B4BFDD47}"));

struct Simple {
    explicit Simple(QUuid g) : guid(g) {}
    QUuid guid;
};

using InterfacePtr = std::shared_ptr<Simple>;

InterfacePtr makeSimple(const QUuid& g) {
    return std::make_shared<Simple>(g);
}
const Simple* asSimple(const InterfacePtr& p) {
    return static_cast<const Simple*>(p.get());
}
const Simple* asSimple(const Simple* p) {
    return static_cast<const Simple*>(p);
}

// Order of the six Add calls used by most DUnit methods.
template <class AddFn> void addSixUnsorted(AddFn add) {
    for (const QUuid* g : {&G6, &G1, &G5, &G2, &G4, &G3})
        add(*g);
}

// ---------------------------------------------------------------- u_GUIDSetStatic_Test

TEST(GuidSetStatic, TestEmpty) {
    EXPECT_EQ(GuidSetStatic::createAndSort({}), nullptr);
}

TEST(GuidSetStatic, TestOneItem) {
    auto list = GuidSetStatic::createAndSort({G1});
    ASSERT_EQ(list->count(), 1);
    EXPECT_EQ(list->item(0), G1);
    EXPECT_TRUE(list->isExists(G1));
    for (const QUuid* g : {&G2, &G3, &G4, &G5, &G6, &G7})
        EXPECT_FALSE(list->isExists(*g));
}

TEST(GuidSetStatic, TestTwoEqual) {
    auto list = GuidSetStatic::createAndSort({G1, G1});
    ASSERT_EQ(list->count(), 1);
    EXPECT_EQ(list->item(0), G1);
    EXPECT_TRUE(list->isExists(G1));
    for (const QUuid* g : {&G2, &G3, &G4, &G5, &G6, &G7})
        EXPECT_FALSE(list->isExists(*g));
}

TEST(GuidSetStatic, TestThreeUniqu) {
    auto list = GuidSetStatic::createAndSort({G1, G5, G7});
    ASSERT_EQ(list->count(), 3);
    EXPECT_EQ(list->item(0), G1);
    EXPECT_EQ(list->item(1), G5);
    EXPECT_EQ(list->item(2), G7);
    EXPECT_TRUE(list->isExists(G1));
    EXPECT_FALSE(list->isExists(G2));
    EXPECT_FALSE(list->isExists(G3));
    EXPECT_FALSE(list->isExists(G4));
    EXPECT_TRUE(list->isExists(G5));
    EXPECT_FALSE(list->isExists(G6));
    EXPECT_TRUE(list->isExists(G7));
}

TEST(GuidSetStatic, TestFourWithEqual) {
    auto list = GuidSetStatic::createAndSort({G1, G5, G7, G1});
    ASSERT_EQ(list->count(), 3);
    EXPECT_EQ(list->item(0), G1);
    EXPECT_EQ(list->item(1), G5);
    EXPECT_EQ(list->item(2), G7);
    EXPECT_TRUE(list->isExists(G1));
    EXPECT_FALSE(list->isExists(G2));
    EXPECT_FALSE(list->isExists(G3));
    EXPECT_FALSE(list->isExists(G4));
    EXPECT_TRUE(list->isExists(G5));
    EXPECT_FALSE(list->isExists(G6));
    EXPECT_TRUE(list->isExists(G7));
}

TEST(GuidSetStatic, TestFiveSorted) {
    auto list = GuidSetStatic::createAndSort({G1, G2, G3, G5, G7});
    ASSERT_EQ(list->count(), 5);
    EXPECT_EQ(list->item(0), G1);
    EXPECT_EQ(list->item(1), G2);
    EXPECT_EQ(list->item(2), G3);
    EXPECT_EQ(list->item(3), G5);
    EXPECT_EQ(list->item(4), G7);
    EXPECT_TRUE(list->isExists(G1));
    EXPECT_TRUE(list->isExists(G2));
    EXPECT_TRUE(list->isExists(G3));
    EXPECT_FALSE(list->isExists(G4));
    EXPECT_TRUE(list->isExists(G5));
    EXPECT_FALSE(list->isExists(G6));
    EXPECT_TRUE(list->isExists(G7));
}

TEST(GuidSetStatic, TestSixUnsorted) {
    auto list = GuidSetStatic::createAndSort({G7, G6, G5, G4, G3, G2});
    ASSERT_EQ(list->count(), 6);
    EXPECT_EQ(list->item(0), G2);
    EXPECT_EQ(list->item(1), G3);
    EXPECT_EQ(list->item(2), G4);
    EXPECT_EQ(list->item(3), G5);
    EXPECT_EQ(list->item(4), G6);
    EXPECT_EQ(list->item(5), G7);
    EXPECT_FALSE(list->isExists(G1));
    for (const QUuid* g : {&G2, &G3, &G4, &G5, &G6, &G7})
        EXPECT_TRUE(list->isExists(*g));
}

// CreateByAdd / CreateByRemove have no DUnit method; cases read from u_GUIDListStatic.pas.
TEST(GuidSetStatic, CreateByAddAndRemove) {
    auto one = GuidSetStatic::createByAdd(nullptr, G5);
    ASSERT_EQ(one->count(), 1);
    auto two = GuidSetStatic::createByAdd(one, G1);
    ASSERT_EQ(two->count(), 2);
    EXPECT_EQ(two->item(0), G1);
    EXPECT_EQ(GuidSetStatic::createByAdd(two, G1)->count(), 2); // present GUID: equal set
    auto back = GuidSetStatic::createByRemove(two, G1);
    ASSERT_EQ(back->count(), 1);
    EXPECT_EQ(back->item(0), G5);
    EXPECT_EQ(GuidSetStatic::createByRemove(back, G5), nullptr);   // last element: null
    EXPECT_EQ(GuidSetStatic::createByRemove(two, G7)->count(), 2); // absent: copy
}

TEST(GuidSet, CompareGuidsIsFieldWiseUnsigned) {
    EXPECT_LT(compareGuids(G1, G5), 0); // Data1 0x357CDEAB < 0x86EA601F
    EXPECT_LT(compareGuids(G5, G6), 0); // 0x86EA601F < 0xF81CCA0A: unsigned, not signed
    EXPECT_LT(compareGuids(G6, G7), 0); // last Data4 byte
    EXPECT_EQ(compareGuids(G3, G3), 0);
    EXPECT_GT(compareGuids(G7, G1), 0);
    // Hand-ordered from u_GUIDTool.CompareGUIDs: equal Data1, Data2 unsigned (0x7FFF < 0x8000).
    const QUuid d2lo(QStringLiteral("{00000001-7FFF-0000-0000-000000000000}"));
    const QUuid d2hi(QStringLiteral("{00000001-8000-0000-0000-000000000000}"));
    EXPECT_LT(compareGuids(d2lo, d2hi), 0);
    // Same for Data3.
    const QUuid d3lo(QStringLiteral("{00000001-0000-7FFF-0000-000000000000}"));
    const QUuid d3hi(QStringLiteral("{00000001-0000-8000-0000-000000000000}"));
    EXPECT_LT(compareGuids(d3lo, d3hi), 0);
    // Fields are compared in order, the RFC variant bits are not looked at first (QUuid::operator<
    // would put the variant-0xC0 GUID after the variant-0 one).
    const QUuid fieldFirst(QStringLiteral("{00000001-0000-0000-C000-000000000000}"));
    const QUuid fieldSecond(QStringLiteral("{00000002-0000-0000-0000-000000000000}"));
    EXPECT_LT(compareGuids(fieldFirst, fieldSecond), 0);
    EXPECT_GT(compareGuids(fieldSecond, fieldFirst), 0);
}

// ---------------------------------------------------------------- i_IGUIDInterfaceSet_Test

TEST(GuidInterfaceSet, TestAdd) {
    GuidInterfaceSet<Simple> set;
    InterfacePtr source = makeSimple(G1);
    InterfacePtr result = set.add(G1, source);
    EXPECT_EQ(set.count(), 1);
    EXPECT_EQ(result, source);
    set.add(G2, makeSimple(G2));
    EXPECT_EQ(set.count(), 2);
    set.add(G3, makeSimple(G3));
    EXPECT_EQ(set.count(), 3);
    set.add(G4, makeSimple(G4));
    EXPECT_EQ(set.count(), 4);
    set.add(G5, makeSimple(G5));
    EXPECT_EQ(set.count(), 5);
    set.add(G6, makeSimple(G6));
    EXPECT_EQ(set.count(), 6);

    result = set.add(G1, makeSimple(G2));
    EXPECT_EQ(set.count(), 6);
    EXPECT_EQ(result, source);

    set.add(G2, makeSimple(G3));
    EXPECT_EQ(set.count(), 6);
    set.add(G3, makeSimple(G4));
    EXPECT_EQ(set.count(), 6);
    set.add(G4, makeSimple(G5));
    EXPECT_EQ(set.count(), 6);
    set.add(G5, makeSimple(G6));
    EXPECT_EQ(set.count(), 6);
    set.add(G6, makeSimple(G1));
    EXPECT_EQ(set.count(), 6);
}

TEST(GuidInterfaceSet, TestIsExists) {
    GuidInterfaceSet<Simple> set;
    addSixUnsorted([&](const QUuid& g) { set.add(g, makeSimple(g)); });
    EXPECT_TRUE(set.isExists(G1));
    EXPECT_TRUE(set.isExists(G2));
    EXPECT_TRUE(set.isExists(G3));
    EXPECT_TRUE(set.isExists(G4));
    EXPECT_TRUE(set.isExists(G5));
    EXPECT_TRUE(set.isExists(G6));
    EXPECT_FALSE(set.isExists(G7));
}

TEST(GuidInterfaceSet, TestGetByGUID) {
    GuidInterfaceSet<Simple> set;
    addSixUnsorted([&](const QUuid& g) { set.add(g, makeSimple(g)); });
    for (const QUuid* g : {&G1, &G2, &G3, &G4, &G5, &G6}) {
        InterfacePtr vi = set.getByGuid(*g);
        ASSERT_NE(vi, nullptr);
        EXPECT_EQ(asSimple(vi)->guid, *g);
    }
    EXPECT_EQ(set.getByGuid(G7), nullptr);
}

TEST(GuidInterfaceSet, TestReplace) {
    GuidInterfaceSet<Simple> set;
    addSixUnsorted([&](const QUuid& g) { set.add(g, makeSimple(g)); });
    set.replace(G1, makeSimple(G2));
    set.replace(G2, makeSimple(G3));
    set.replace(G3, makeSimple(G4));
    set.replace(G4, makeSimple(G5));
    set.replace(G5, makeSimple(G6));
    set.replace(G6, makeSimple(G1));
    const QUuid* keys[] = {&G1, &G2, &G3, &G4, &G5, &G6};
    const QUuid* want[] = {&G2, &G3, &G4, &G5, &G6, &G1};
    for (int i = 0; i < 6; ++i) {
        InterfacePtr vi = set.getByGuid(*keys[i]);
        ASSERT_NE(vi, nullptr);
        EXPECT_EQ(asSimple(vi)->guid, *want[i]);
    }
    EXPECT_EQ(set.getByGuid(G7), nullptr);
}

TEST(GuidInterfaceSet, TestRemove) {
    GuidInterfaceSet<Simple> set;
    addSixUnsorted([&](const QUuid& g) { set.add(g, makeSimple(g)); });
    set.remove(G1);
    set.remove(G3);
    set.remove(G5);
    EXPECT_EQ(set.getByGuid(G1), nullptr);
    ASSERT_NE(set.getByGuid(G2), nullptr);
    EXPECT_EQ(asSimple(set.getByGuid(G2))->guid, G2);
    EXPECT_EQ(set.getByGuid(G3), nullptr);
    ASSERT_NE(set.getByGuid(G4), nullptr);
    EXPECT_EQ(asSimple(set.getByGuid(G4))->guid, G4);
    EXPECT_EQ(set.getByGuid(G5), nullptr);
    ASSERT_NE(set.getByGuid(G6), nullptr);
    EXPECT_EQ(asSimple(set.getByGuid(G6))->guid, G6);
}

TEST(GuidInterfaceSet, TestClear) {
    GuidInterfaceSet<Simple> set;
    addSixUnsorted([&](const QUuid& g) { set.add(g, makeSimple(g)); });
    set.clear();
    EXPECT_EQ(set.count(), 0);
    EXPECT_TRUE(set.guids().empty()); // Next(1) = S_FALSE: no element
}

TEST(GuidInterfaceSet, TestGetGUIDEnum) {
    GuidInterfaceSet<Simple> set;
    addSixUnsorted([&](const QUuid& g) { set.add(g, makeSimple(g)); });
    const std::vector<QUuid> guids = set.guids();
    ASSERT_EQ(guids.size(), 6u); // the 7th Next(1) is S_FALSE
    EXPECT_EQ(guids[0], G1);
    EXPECT_EQ(guids[1], G2);
    EXPECT_EQ(guids[2], G3);
    EXPECT_EQ(guids[3], G4);
    EXPECT_EQ(guids[4], G5);
    EXPECT_EQ(guids[5], G6);
}

// Behaviour read from u_GUIDInterfaceSet.pas, not covered by DUnit.
TEST(GuidInterfaceSet, NilHandlingEnumUnknownAndItems) {
    GuidInterfaceSet<Simple> strict;
    EXPECT_THROW(strict.add(G1, nullptr), std::invalid_argument);
    EXPECT_THROW(strict.replace(G1, nullptr), std::invalid_argument);
    EXPECT_EQ(strict.count(), 0);
    GuidInterfaceSet<Simple> lax(true);
    EXPECT_EQ(lax.add(G1, nullptr), nullptr);
    EXPECT_TRUE(lax.isExists(G1));
    addSixUnsorted([&](const QUuid& g) { lax.replace(g, makeSimple(g)); });
    EXPECT_EQ(lax.count(), 6);
    const auto values = lax.values();
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(values[static_cast<std::size_t>(i)], lax.item(i));
        EXPECT_EQ(asSimple(lax.item(i))->guid, lax.guids()[static_cast<std::size_t>(i)]);
    }
}

// ---------------------------------------------------------------- i_IGUIDObjectSet_Test

struct ObjectSetFixture : ::testing::Test {
    Simple o1{G1}, o2{G2}, o3{G3}, o4{G4}, o5{G5}, o6{G6};
    GuidObjectSet<Simple> set;
    void addSix() {
        set.add(G6, &o6);
        set.add(G1, &o1);
        set.add(G5, &o5);
        set.add(G2, &o2);
        set.add(G4, &o4);
        set.add(G3, &o3);
    }
};

TEST_F(ObjectSetFixture, TestAdd) {
    Simple* result = set.add(G1, &o1);
    EXPECT_EQ(set.count(), 1);
    EXPECT_EQ(result, &o1);
    set.add(G2, &o2);
    EXPECT_EQ(set.count(), 2);
    set.add(G3, &o3);
    EXPECT_EQ(set.count(), 3);
    set.add(G4, &o4);
    EXPECT_EQ(set.count(), 4);
    set.add(G5, &o5);
    EXPECT_EQ(set.count(), 5);
    set.add(G6, &o6);
    EXPECT_EQ(set.count(), 6);

    Simple s2(G2), s3(G3), s4(G4), s5(G5), s6(G6), s1(G1);
    result = set.add(G1, &s2);
    EXPECT_EQ(set.count(), 6);
    EXPECT_EQ(result, &o1);
    set.add(G2, &s3);
    EXPECT_EQ(set.count(), 6);
    set.add(G3, &s4);
    EXPECT_EQ(set.count(), 6);
    set.add(G4, &s5);
    EXPECT_EQ(set.count(), 6);
    set.add(G5, &s6);
    EXPECT_EQ(set.count(), 6);
    set.add(G6, &s1);
    EXPECT_EQ(set.count(), 6);
}

TEST_F(ObjectSetFixture, TestIsExists) {
    addSix();
    EXPECT_TRUE(set.isExists(G1));
    EXPECT_TRUE(set.isExists(G2));
    EXPECT_TRUE(set.isExists(G3));
    EXPECT_TRUE(set.isExists(G4));
    EXPECT_TRUE(set.isExists(G5));
    EXPECT_TRUE(set.isExists(G6));
    EXPECT_FALSE(set.isExists(G7));
}

TEST_F(ObjectSetFixture, TestGetByGUID) {
    addSix();
    for (const QUuid* g : {&G1, &G2, &G3, &G4, &G5, &G6}) {
        Simple* vi = set.getByGuid(*g);
        ASSERT_NE(vi, nullptr);
        EXPECT_EQ(asSimple(vi)->guid, *g);
    }
    EXPECT_EQ(set.getByGuid(G7), nullptr);
}

TEST_F(ObjectSetFixture, TestReplace) {
    addSix();
    set.replace(G1, &o2);
    set.replace(G2, &o3);
    set.replace(G3, &o4);
    set.replace(G4, &o5);
    set.replace(G5, &o6);
    set.replace(G6, &o1);
    const QUuid* keys[] = {&G1, &G2, &G3, &G4, &G5, &G6};
    const QUuid* want[] = {&G2, &G3, &G4, &G5, &G6, &G1};
    for (int i = 0; i < 6; ++i) {
        Simple* vi = set.getByGuid(*keys[i]);
        ASSERT_NE(vi, nullptr);
        EXPECT_EQ(asSimple(vi)->guid, *want[i]);
    }
    EXPECT_EQ(set.getByGuid(G7), nullptr);
}

TEST_F(ObjectSetFixture, TestRemove) {
    addSix();
    set.remove(G1);
    set.remove(G3);
    set.remove(G5);
    EXPECT_EQ(set.getByGuid(G1), nullptr);
    ASSERT_NE(set.getByGuid(G2), nullptr);
    EXPECT_EQ(asSimple(set.getByGuid(G2))->guid, G2);
    EXPECT_EQ(set.getByGuid(G3), nullptr);
    ASSERT_NE(set.getByGuid(G4), nullptr);
    EXPECT_EQ(asSimple(set.getByGuid(G4))->guid, G4);
    EXPECT_EQ(set.getByGuid(G5), nullptr);
    ASSERT_NE(set.getByGuid(G6), nullptr);
    EXPECT_EQ(asSimple(set.getByGuid(G6))->guid, G6);
}

TEST_F(ObjectSetFixture, TestClear) {
    addSix();
    set.clear();
    EXPECT_EQ(set.count(), 0);
    EXPECT_TRUE(set.guids().empty());
}

TEST_F(ObjectSetFixture, TestGetGUIDEnum) {
    addSix();
    const std::vector<QUuid> guids = set.guids();
    ASSERT_EQ(guids.size(), 6u);
    EXPECT_EQ(guids[0], G1);
    EXPECT_EQ(guids[1], G2);
    EXPECT_EQ(guids[2], G3);
    EXPECT_EQ(guids[3], G4);
    EXPECT_EQ(guids[4], G5);
    EXPECT_EQ(guids[5], G6);
}

TEST_F(ObjectSetFixture, NilAndOwnership) {
    EXPECT_FALSE(set.isObjectOwner());
    EXPECT_THROW(set.add(G1, nullptr), std::invalid_argument);
    EXPECT_THROW(set.replace(G1, nullptr), std::invalid_argument);
    GuidObjectSet<Simple> lax(true);
    EXPECT_EQ(lax.add(G1, nullptr), nullptr);
    EXPECT_TRUE(lax.isExists(G1));
}

// ---------------------------------------------------------------- ID list (no DUnit)

TEST(IdInterfaceList, EnumerationIsAscendingBySignedId) {
    IdInterfaceList<Simple> list;
    for (qintptr id : {qintptr(5), qintptr(-3), qintptr(100), qintptr(0), qintptr(-70), qintptr(7)}) {
        list.add(id, makeSimple(G1));
    }
    const std::vector<qintptr> want{-70, -3, 0, 5, 7, 100};
    EXPECT_EQ(list.ids(), want); // insertion order is NOT kept; negative ids come first
    ASSERT_EQ(list.values().size(), 6u);
}

TEST(IdInterfaceList, AddKeepsExistingReplaceOverwrites) {
    IdInterfaceList<Simple> list;
    InterfacePtr a = makeSimple(G1), b = makeSimple(G2), c = makeSimple(G3);
    EXPECT_EQ(list.add(10, a), a);
    EXPECT_EQ(list.add(10, b), a); // duplicate: stored object returned, not replaced
    EXPECT_EQ(list.count(), 1);
    EXPECT_EQ(list.getById(10), a);
    list.replace(10, b);
    EXPECT_EQ(list.getById(10), b);
    EXPECT_EQ(list.count(), 1);
    list.replace(3, c); // absent: inserted
    EXPECT_EQ(list.count(), 2);
    EXPECT_EQ((std::vector<qintptr>{3, 10}), list.ids());
    EXPECT_EQ((std::vector<InterfacePtr>{c, b}), list.values());
}

TEST(IdInterfaceList, AbsentKeyAndRemoveAndClear) {
    IdInterfaceList<Simple> list;
    list.add(1, makeSimple(G1));
    list.add(2, makeSimple(G2));
    EXPECT_FALSE(list.isExists(3));
    EXPECT_EQ(list.getById(3), nullptr);
    list.remove(3); // absent: nothing happens
    EXPECT_EQ(list.count(), 2);
    list.remove(1);
    EXPECT_FALSE(list.isExists(1));
    EXPECT_EQ(list.ids(), (std::vector<qintptr>{2}));
    list.clear();
    EXPECT_EQ(list.count(), 0);
    EXPECT_TRUE(list.ids().empty());
}

TEST(IdInterfaceList, NilRejectedUnlessAllowed) {
    IdInterfaceList<Simple> strict;
    EXPECT_THROW(strict.add(1, nullptr), std::invalid_argument);
    EXPECT_THROW(strict.replace(1, nullptr), std::invalid_argument);
    EXPECT_EQ(strict.count(), 0);
    IdInterfaceList<Simple> lax(true);
    EXPECT_EQ(lax.add(1, nullptr), nullptr);
    EXPECT_TRUE(lax.isExists(1));
}

} // namespace
