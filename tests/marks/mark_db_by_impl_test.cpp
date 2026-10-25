// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: u_MarkDbByImpl.pas (TMarkDbByImpl) with a fake IMarkSystemImpl, and the MarkOrIdList
// wrapper of decision C3 (docs/specs/marks.md section 2 preamble).
#include "cg/core/notifier.h"
#include "cg/marks/mark_db_by_impl.h"
#include "cg/marks/mark_id.h"
#include "cg/marks/mark_system_impl.h"
#include "cg/marks/mark_system_impl_changeable.h"
#include "cg/vector/category.h"
#include "cg/vector/vector_item_subset.h"

#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mark_system_fakes.h"

namespace m = cg::marks;
namespace v = cg::vector;
namespace t = cg::marks::testutil;

namespace {

QStringList g_messages;
void collect(QtMsgType, const QMessageLogContext&, const QString& msg) {
    g_messages.push_back(msg);
}

class CaptureLog {
public:
    CaptureLog() : previous_(qInstallMessageHandler(collect)) { g_messages.clear(); }
    ~CaptureLog() { qInstallMessageHandler(previous_); }
    CaptureLog(const CaptureLog&) = delete;
    CaptureLog& operator=(const CaptureLog&) = delete;

private:
    QtMessageHandler previous_;
};

class FakeId final : public m::IMarkId {
public:
    [[nodiscard]] v::CategoryPtr category() const override { return nullptr; }
    [[nodiscard]] QString name() const override { return {}; }
    [[nodiscard]] int multiGeometryCount() const override { return 1; }
    [[nodiscard]] m::MarkIdType markType() const override { return m::MarkIdType::Point; }
    [[nodiscard]] bool isSameId(const IMarkId* markId) const override { return markId == this; }
    [[nodiscard]] bool isSameMark(const v::IVectorDataItem*) const override { return false; }
};

// The shared fake system (mark_system_fakes.h) whose mark database is the t::FakeDb of this file.
std::shared_ptr<t::FakeMarkSystemImpl> backendWith(std::shared_ptr<t::FakeDb> db) {
    auto impl = std::make_shared<t::FakeMarkSystemImpl>();
    impl->markDatabase = std::move(db);
    return impl;
}

struct Fixture {
    std::shared_ptr<t::FakeMarkSystemImplChangeable> changeable =
        std::make_shared<t::FakeMarkSystemImplChangeable>();
    std::shared_ptr<t::FakeDb> db = std::make_shared<t::FakeDb>();
    std::shared_ptr<t::FakeMarkSystemImpl> impl = backendWith(db);
    cg::core::Notifier errors;
    int errorCount = 0;
    cg::core::Subscription errorSub = errors.subscribe([this] { ++errorCount; });
    std::unique_ptr<m::MarkDbByImpl> facade;
    explicit Fixture(bool open = true) {
        if (open) {
            changeable->impl = impl;
        }
        facade = std::make_unique<m::MarkDbByImpl>(changeable, nullptr, errors);
    }
};

const v::CategoryPtr kCategory = std::make_shared<v::Category>(QStringLiteral("c"));
const cg::geo::DoubleRect kRect{1, 2, 3, 4};
const cg::geo::DoublePoint kSize{5, 6};
const m::MarkIdPtr kId = std::make_shared<FakeId>();

// Runs `call` against a backend that throws and checks "caught, handler once, message logged once".
template <class F> void expectCaught(Fixture& f, const char* method, F&& call) {
    CaptureLog log;
    f.errorCount = 0;
    f.db->fail = true;
    EXPECT_NO_THROW(call()) << method;
    EXPECT_EQ(f.errorCount, 1) << method;
    EXPECT_EQ(g_messages.size(), 1) << method;
    EXPECT_TRUE(!g_messages.isEmpty() &&
                g_messages[0].contains(QStringLiteral("boom: ") + QString::fromLatin1(method)))
        << method;
    EXPECT_EQ(f.db->last, method);
    f.db->fail = false;
}

TEST(MarkDbByImpl, ThrowingBackendIsCaughtAndEmptyValueReturned) {
    Fixture f;
    auto& d = *f.facade;
    expectCaught(f, "firstMarkByName",
                 [&] { EXPECT_EQ(d.firstMarkByName(QStringLiteral("x"), kCategory), nullptr); });
    expectCaught(f, "markSubsetByCategoryList",
                 [&] { EXPECT_EQ(d.markSubsetByCategoryList(nullptr, true), nullptr); });
    expectCaught(f, "markSubsetByCategory",
                 [&] { EXPECT_EQ(d.markSubsetByCategory(kCategory, true), nullptr); });
    expectCaught(f, "markSubsetByCategoryListInRect",
                 [&] { EXPECT_EQ(d.markSubsetByCategoryListInRect(kRect, nullptr, true, kSize), nullptr); });
    expectCaught(f, "markSubsetByCategoryInRect",
                 [&] { EXPECT_EQ(d.markSubsetByCategoryInRect(kRect, kCategory, true, kSize), nullptr); });
    expectCaught(f, "findMarks",
                 [&] { EXPECT_EQ(d.findMarks(QStringLiteral("x"), 5, true, true), nullptr); });
    expectCaught(f, "updateMark", [&] { EXPECT_EQ(d.updateMark(nullptr, f.db->item), nullptr); });
    expectCaught(f, "updateMarkList", [&] { EXPECT_EQ(d.updateMarkList(nullptr, f.db->markList), nullptr); });
    expectCaught(f, "allMarkIdList", [&] { EXPECT_EQ(d.allMarkIdList(), nullptr); });
    expectCaught(f, "markIdListByCategory", [&] { EXPECT_EQ(d.markIdListByCategory(kCategory), nullptr); });
    expectCaught(f, "markById", [&] { EXPECT_EQ(d.markById(kId), nullptr); });
    expectCaught(f, "setMarkVisibleById", [&] { d.setMarkVisibleById(kId, true); });
    expectCaught(f, "setMarkVisible", [&] { d.setMarkVisible(f.db->item, true); });
    expectCaught(f, "setMarkVisibleByIdList", [&] { d.setMarkVisibleByIdList(nullptr, true); });
    expectCaught(f, "toggleMarkVisibleByIdList", [&] { d.toggleMarkVisibleByIdList(nullptr); });
    expectCaught(f, "setAllMarksInCategoryVisible", [&] { d.setAllMarksInCategoryVisible(kCategory, true); });
    // The two visibility queries fall back to True, not False (:367, :374, :383, :390).
    f.db->visible = false;
    expectCaught(f, "markVisibleById", [&] { EXPECT_TRUE(d.markVisibleById(kId)); });
    expectCaught(f, "markVisible", [&] { EXPECT_TRUE(d.markVisible(f.db->item)); });
}

TEST(MarkDbByImpl, NullMarkDbOfTheBackendIsCaughtLikeAnyBackendFailure) {
    // Legacy: VImpl.MarkDb is nil -> access violation inside the try -> CatchException.
    Fixture f;
    f.impl->markDatabase = nullptr;
    {
        CaptureLog log;
        EXPECT_EQ(f.facade->allMarkIdList(), nullptr);
        EXPECT_TRUE(f.facade->markVisibleById(kId));
        f.facade->setMarkVisibleById(kId, true);
        EXPECT_EQ(f.errorCount, 3);
        EXPECT_EQ(g_messages.size(), 3);
    }
    // OnImplChange (the 19th catching method): a switch to such a backend is caught, not subscribed.
    f.errorCount = 0;
    {
        CaptureLog log;
        f.changeable->open(f.impl);
        EXPECT_EQ(f.errorCount, 1);
        EXPECT_EQ(g_messages.size(), 1);
    }
}

TEST(MarkDbByImpl, NoOpenDatabaseGivesEmptyValuesAndNoError) {
    Fixture f(false);
    auto& d = *f.facade;
    CaptureLog log;
    EXPECT_EQ(d.firstMarkByName(QStringLiteral("x"), kCategory), nullptr);
    EXPECT_EQ(d.markSubsetByCategoryList(nullptr, true), nullptr);
    EXPECT_EQ(d.markSubsetByCategory(kCategory, true), nullptr);
    EXPECT_EQ(d.markSubsetByCategoryListInRect(kRect, nullptr, true, kSize), nullptr);
    EXPECT_EQ(d.markSubsetByCategoryInRect(kRect, kCategory, true, kSize), nullptr);
    EXPECT_EQ(d.findMarks(QStringLiteral("x"), 5, true, true), nullptr);
    EXPECT_EQ(d.updateMark(nullptr, f.db->item), nullptr);
    EXPECT_EQ(d.updateMarkList(nullptr, nullptr), nullptr);
    EXPECT_EQ(d.allMarkIdList(), nullptr);
    EXPECT_EQ(d.markIdListByCategory(kCategory), nullptr);
    EXPECT_EQ(d.markById(kId), nullptr);
    d.setMarkVisibleById(kId, true);
    d.setMarkVisible(f.db->item, true);
    d.setMarkVisibleByIdList(nullptr, true);
    d.toggleMarkVisibleByIdList(nullptr);
    d.setAllMarksInCategoryVisible(kCategory, true);
    EXPECT_TRUE(d.markVisibleById(kId));
    EXPECT_TRUE(d.markVisible(f.db->item));
    EXPECT_EQ(f.errorCount, 0);
    EXPECT_TRUE(g_messages.isEmpty());
    EXPECT_EQ(f.db->calls, 0);
}

TEST(MarkDbByImpl, ForwardsArgumentsAndReturnsTheBackendAnswer) {
    Fixture f;
    auto& d = *f.facade;
    auto& db = *f.db;
    EXPECT_EQ(d.firstMarkByName(QStringLiteral("abc"), kCategory), db.item);
    EXPECT_EQ(db.argName, QStringLiteral("abc"));
    EXPECT_EQ(db.argCategory, kCategory);
    EXPECT_EQ(d.markSubsetByCategory(kCategory, true), db.subset);
    EXPECT_TRUE(db.argBool);
    EXPECT_EQ(d.markSubsetByCategoryListInRect(kRect, nullptr, true, kSize), db.subset);
    EXPECT_EQ(db.argInt, 8); // rect.right 3 + size.x 5
    EXPECT_EQ(d.markSubsetByCategoryInRect(kRect, kCategory, false, kSize), db.subset);
    EXPECT_FALSE(db.argBool);
    EXPECT_EQ(d.findMarks(QStringLiteral("q"), 7, true, false), db.subset);
    EXPECT_EQ(db.argInt, 7);
    EXPECT_TRUE(db.argBool);
    EXPECT_FALSE(db.argBool2);
    EXPECT_EQ(d.updateMark(nullptr, db.item), db.item);
    EXPECT_EQ(db.argOld, nullptr);
    EXPECT_EQ(db.argNew, db.item);
    EXPECT_EQ(d.allMarkIdList(), db.idList);
    EXPECT_EQ(d.markById(kId), db.item);
    EXPECT_EQ(db.argId, kId);
    EXPECT_FALSE(d.markVisible(db.item)); // the backend says false: no fallback to True
    EXPECT_FALSE(d.markVisibleById(kId));
    d.setMarkVisibleById(kId, true);
    EXPECT_EQ(db.last, "setMarkVisibleById");
    EXPECT_TRUE(db.argBool);
    d.setAllMarksInCategoryVisible(kCategory, true);
    EXPECT_EQ(db.last, "setAllMarksInCategoryVisible");
    EXPECT_EQ(f.errorCount, 0);
}

TEST(MarkDbByImpl, ListArgumentsReachTheBackendUnchanged) {
    Fixture f;
    auto& db = *f.db;
    const auto ids = std::make_shared<const std::vector<m::MarkIdPtr>>(std::vector<m::MarkIdPtr>{kId});
    const auto marks =
        std::make_shared<const std::vector<v::VectorDataItemPtr>>(std::vector<v::VectorDataItemPtr>{db.item});
    EXPECT_EQ(f.facade->updateMarkList(ids, marks), db.markList);
    ASSERT_EQ(db.argOrIdList.count(), 1);
    EXPECT_EQ(std::get<m::MarkIdPtr>(db.argOrIdList[0]), kId);
    EXPECT_EQ(db.argNewList, marks);
    f.facade->setMarkVisibleByIdList(marks, true);
    ASSERT_EQ(db.argOrIdList.count(), 1);
    EXPECT_EQ(std::get<v::VectorDataItemPtr>(db.argOrIdList[0]), db.item);
    f.facade->toggleMarkVisibleByIdList(ids);
    EXPECT_EQ(db.last, "toggleMarkVisibleByIdList");
    EXPECT_EQ(db.argOrIdList.count(), 1);
}

TEST(MarkDbByImpl, ConstructorDoesNotSubscribeTheCurrentBackend) {
    // Legacy :166-167 calls OnDBImplChange (notify), not OnImplChange (subscribe): a backend that is
    // already open when the facade is built is not listened to until the next backend switch.
    cg::core::Notifier errors;
    auto changeable = std::make_shared<t::FakeMarkSystemImplChangeable>();
    auto db = std::make_shared<t::FakeDb>();
    changeable->impl = backendWith(db);
    int fired = 0;
    m::MarkDbByImpl facade(changeable, nullptr, errors);
    const auto sub = facade.changeNotifier().subscribe([&] { ++fired; });
    db->changed.notify();
    EXPECT_EQ(fired, 0);
    EXPECT_EQ(db->changed.subscriberCount(), 0u);
}

TEST(MarkDbByImpl, BackendSwitchResubscribesWithoutNotifying) {
    // Legacy OnImplChange :395-412 moves the listener to the new backend and does not notify
    // (the category facade does; this one does not).
    Fixture f;
    int fired = 0;
    const auto sub = f.facade->changeNotifier().subscribe([&] { ++fired; });
    auto secondDb = std::make_shared<t::FakeDb>();
    auto second = backendWith(secondDb);
    f.changeable->open(second);
    EXPECT_EQ(fired, 0);
    EXPECT_EQ(secondDb->changed.subscriberCount(), 1u);
    secondDb->changed.notify();
    EXPECT_EQ(fired, 1);
    auto thirdDb = std::make_shared<t::FakeDb>();
    auto third = backendWith(thirdDb);
    f.changeable->open(third);
    EXPECT_EQ(secondDb->changed.subscriberCount(), 0u);
    secondDb->changed.notify();
    EXPECT_EQ(fired, 1);
    f.changeable->open(nullptr); // reopening: no backend, listener dropped
    EXPECT_EQ(thirdDb->changed.subscriberCount(), 0u);
    EXPECT_EQ(f.errorCount, 0);
}

TEST(MarkOrIdList, NilAndConversions) {
    const m::MarkOrIdList defaulted;
    const m::MarkOrIdList literal = nullptr;
    const m::MarkOrIdList nullMarks = m::MarkList{};
    const m::MarkOrIdList nullIds = m::MarkIdList{};
    for (const auto* l : {&defaulted, &literal, &nullMarks, &nullIds}) {
        EXPECT_FALSE(static_cast<bool>(*l));
        EXPECT_EQ(l->count(), 0);
    }
    const auto item = std::make_shared<t::FakeItem>();
    const m::MarkOrIdList marks = std::make_shared<const std::vector<v::VectorDataItemPtr>>(
        std::vector<v::VectorDataItemPtr>{item, nullptr});
    const m::MarkOrIdList ids =
        std::make_shared<const std::vector<m::MarkIdPtr>>(std::vector<m::MarkIdPtr>{kId, kId, nullptr});
    EXPECT_TRUE(static_cast<bool>(marks));
    EXPECT_EQ(marks.count(), 2);
    EXPECT_EQ(ids.count(), 3);
    ASSERT_TRUE(std::holds_alternative<v::VectorDataItemPtr>(marks[0]));
    EXPECT_EQ(std::get<v::VectorDataItemPtr>(marks[0]), item);
    EXPECT_EQ(std::get<v::VectorDataItemPtr>(marks[1]), nullptr); // a nil element stays nil
    ASSERT_TRUE(std::holds_alternative<m::MarkIdPtr>(ids[1]));
    EXPECT_EQ(std::get<m::MarkIdPtr>(ids[1]), kId);
    EXPECT_EQ(std::get<m::MarkIdPtr>(ids[2]), nullptr);
    const m::MarkOrIdList emptyIds = std::make_shared<const std::vector<m::MarkIdPtr>>();
    EXPECT_TRUE(static_cast<bool>(emptyIds)); // Assigned(AList) true, Count 0
    EXPECT_EQ(emptyIds.count(), 0);
}

} // namespace
