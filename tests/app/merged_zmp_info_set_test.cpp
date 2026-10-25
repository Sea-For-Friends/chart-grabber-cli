// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/merged_zmp_info_set (NEW, T-0239, T-0291, app-cli.md 1.6.1; no legacy counterpart).
// No FEAT. Real ZmpInfoSet objects over temporary ZMP folders; expectations are the literals of the ticket.
#include "cg/app/merged_zmp_info_set.h"

#include <QDir>
#include <QTemporaryDir>
#include <QUuid>

#include <gtest/gtest.h>

#include <memory>
#include <tuple>
#include <vector>

#include "../maptype/maptype_test_util.h"
#include "cli_test_util.h"

using cg::app::MergedZmpInfoSet;
using cg::app::test::writeZmp;
using namespace Qt::StringLiterals;

namespace {

const QByteArray kGuid1 = "{AB0000A1-0000-0000-0000-0000000000A1}";
const QByteArray kGuid2 = "{AB0000B2-0000-0000-0000-0000000000B2}";
const QByteArray kGuid3 = "{AB0000D4-0000-0000-0000-0000000000D4}";

QUuid uuid(const QByteArray& braced) {
    return QUuid::fromString(QString::fromLatin1(braced));
}

using Call = std::tuple<QUuid, QString, QStringList>;

class MergedSetTest : public ::testing::Test {
protected:
    void SetUp() override {
        cg::maptype::test::ensureGuiApp();
        ASSERT_TRUE(root_.isValid());
    }
    QString dir(const QString& name) const { return root_.filePath(name); }
    // one folder, files written in the order given (the set sorts them by file name)
    void add(const QString& name, const QString& file, const QByteArray& guid, const QByteArray& label) {
        writeZmp(dir(name), {.folder = file, .guid = guid, .name = label});
    }
    // loads folder `name`; the set stays alive in the fixture
    const cg::maptype::ZmpInfoSet& load(const QString& name) {
        QStringList errors;
        sets_.push_back(env_.loadAll(dir(name), errors));
        EXPECT_TRUE(errors.isEmpty()) << errors.join(u'|').toStdString();
        return *sets_.back();
    }
    std::unique_ptr<MergedZmpInfoSet> merge(const std::vector<QString>& names, std::vector<Call>& calls) {
        std::vector<MergedZmpInfoSet::Folder> folders;
        for (const QString& name : names) {
            folders.push_back({dir(name), &load(name)});
        }
        return std::make_unique<MergedZmpInfoSet>(
            folders, [&calls](const QUuid& guid, const QString& winner, const QStringList& replaced) {
                calls.emplace_back(guid, winner, replaced);
            });
    }

    QTemporaryDir root_;
    cg::maptype::test::ZmpEnv env_;
    std::vector<std::unique_ptr<cg::maptype::ZmpInfoSet>> sets_;
};

} // namespace

// (a) one folder is that set: its order (by file name, not by GUID), its pointers, no sink call.
TEST_F(MergedSetTest, OneFolderIsThatSetAndTheSinkIsNotCalled) {
    add(u"A"_s, u"f2.zmp"_s, kGuid1, "One");
    add(u"A"_s, u"f1.zmp"_s, kGuid3, "Three");
    std::vector<Call> calls;
    const auto merged = merge({u"A"_s}, calls);
    EXPECT_TRUE(calls.empty());
    EXPECT_EQ(merged->guids(), (std::vector<QUuid>{uuid(kGuid3), uuid(kGuid1)}));
    EXPECT_EQ(merged->zmpByGuid(uuid(kGuid1)), sets_[0]->zmpByGuid(uuid(kGuid1)));
    EXPECT_EQ(merged->zmpByGuid(uuid(kGuid3)), sets_[0]->zmpByGuid(uuid(kGuid3)));
    EXPECT_NE(merged->zmpByGuid(uuid(kGuid1)), nullptr);
    EXPECT_EQ(merged->zmpByGuid(QUuid::fromString(u"{AB0000FF-0000-0000-0000-0000000000FF}"_s)), nullptr);
}

// (b) two disjoint folders: the union, folder A first then folder B, each in its own order.
TEST_F(MergedSetTest, DisjointFoldersGiveTheUnionInFolderOrder) {
    add(u"A"_s, u"f2.zmp"_s, kGuid3, "Three");
    add(u"A"_s, u"f1.zmp"_s, kGuid2, "Two");
    add(u"B"_s, u"f1.zmp"_s, kGuid1, "One");
    std::vector<Call> calls;
    const auto merged = merge({u"A"_s, u"B"_s}, calls);
    EXPECT_TRUE(calls.empty());
    EXPECT_EQ(merged->guids(), (std::vector<QUuid>{uuid(kGuid2), uuid(kGuid3), uuid(kGuid1)}));
    EXPECT_EQ(merged->zmpByGuid(uuid(kGuid2)), sets_[0]->zmpByGuid(uuid(kGuid2)));
    EXPECT_EQ(merged->zmpByGuid(uuid(kGuid1)), sets_[1]->zmpByGuid(uuid(kGuid1)));
}

// (c) a GUID in A and B: B's pointer, one call (winner B's path, replaced A's path).
TEST_F(MergedSetTest, LaterFolderWinsAndTheSinkIsToldOnce) {
    add(u"A"_s, u"a.zmp"_s, kGuid1, "From A");
    add(u"B"_s, u"b.zmp"_s, kGuid1, "From B");
    std::vector<Call> calls;
    const auto merged = merge({u"A"_s, u"B"_s}, calls);
    EXPECT_EQ(merged->zmpByGuid(uuid(kGuid1)), sets_[1]->zmpByGuid(uuid(kGuid1)));
    EXPECT_NE(merged->zmpByGuid(uuid(kGuid1)), sets_[0]->zmpByGuid(uuid(kGuid1)));
    EXPECT_EQ(merged->guids(), (std::vector<QUuid>{uuid(kGuid1)}));
    ASSERT_EQ(calls.size(), 1U);
    EXPECT_EQ(calls[0], std::make_tuple(uuid(kGuid1), dir(u"B"_s) + u"/b.zmp"_s,
                                        QStringList{dir(u"A"_s) + u"/a.zmp"_s}));
}

// (d) a GUID in A, B and C: C's pointer, one call, replaced {A, B} in folder order.
TEST_F(MergedSetTest, ThreeFoldersTheLastWinsAndBothEarlierArePassedInOrder) {
    add(u"A"_s, u"a.zmp"_s, kGuid1, "From A");
    add(u"B"_s, u"b.zmp"_s, kGuid1, "From B");
    add(u"C"_s, u"c.zmp"_s, kGuid1, "From C");
    std::vector<Call> calls;
    const auto merged = merge({u"A"_s, u"B"_s, u"C"_s}, calls);
    EXPECT_EQ(merged->zmpByGuid(uuid(kGuid1)), sets_[2]->zmpByGuid(uuid(kGuid1)));
    ASSERT_EQ(calls.size(), 1U);
    EXPECT_EQ(calls[0], std::make_tuple(uuid(kGuid1), dir(u"C"_s) + u"/c.zmp"_s,
                                        QStringList{dir(u"A"_s) + u"/a.zmp"_s, dir(u"B"_s) + u"/b.zmp"_s}));
}

// (e) order: A = {g1, g2}, B = {g3, g1} -> guids {g1, g2, g3}; g1 carries B's definition; the one call is
// for g1 only.
TEST_F(MergedSetTest, ASharedGuidStandsAtItsFirstPositionWithTheLastDefinition) {
    add(u"A"_s, u"a1.zmp"_s, kGuid1, "A one");
    add(u"A"_s, u"a2.zmp"_s, kGuid2, "A two");
    add(u"B"_s, u"b1.zmp"_s, kGuid3, "B three");
    add(u"B"_s, u"b2.zmp"_s, kGuid1, "B one");
    std::vector<Call> calls;
    const auto merged = merge({u"A"_s, u"B"_s}, calls);
    EXPECT_EQ(merged->guids(), (std::vector<QUuid>{uuid(kGuid1), uuid(kGuid2), uuid(kGuid3)}));
    EXPECT_EQ(merged->zmpByGuid(uuid(kGuid1)), sets_[1]->zmpByGuid(uuid(kGuid1)));
    EXPECT_EQ(merged->zmpByGuid(uuid(kGuid2)), sets_[0]->zmpByGuid(uuid(kGuid2)));
    ASSERT_EQ(calls.size(), 1U);
    EXPECT_EQ(std::get<0>(calls[0]), uuid(kGuid1));
}

// (f) two shared GUIDs: the calls follow guids() (A's order), not the order of the later folder.
TEST_F(MergedSetTest, SinkCallsFollowTheOrderOfGuids) {
    add(u"A"_s, u"a1.zmp"_s, kGuid1, "A one");
    add(u"A"_s, u"a2.zmp"_s, kGuid2, "A two");
    add(u"B"_s, u"b1.zmp"_s, kGuid2, "B two");
    add(u"B"_s, u"b2.zmp"_s, kGuid1, "B one");
    std::vector<Call> calls;
    const auto merged = merge({u"A"_s, u"B"_s}, calls);
    ASSERT_EQ(calls.size(), 2U);
    EXPECT_EQ(std::get<0>(calls[0]), uuid(kGuid1));
    EXPECT_EQ(std::get<1>(calls[0]), dir(u"B"_s) + u"/b2.zmp"_s);
    EXPECT_EQ(std::get<0>(calls[1]), uuid(kGuid2));
    EXPECT_EQ(std::get<1>(calls[1]), dir(u"B"_s) + u"/b1.zmp"_s);
}
