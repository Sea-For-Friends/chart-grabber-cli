// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports i_TileRequestTask.pas, u_TileRequestTask.pas (no FEAT id: prerequisite of the download subsystem).
#include "cg/maptype/tile_request_task.h"

#include <QString>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

using namespace cg::maptype;
using namespace cg;
using namespace Qt::StringLiterals;

namespace {

ITileRequestPtr tileRequest() {
    return std::make_shared<const TileRequest>(geo::PointI{3, 4}, 5, nullptr);
}

ITileRequestResultPtr resultFor(const ITileRequestPtr& r, const QString& text) {
    return std::make_shared<const TileRequestResultErrorBeforBuildDownloadRequest>(r, text);
}

struct Call {
    const ITileRequestTask* task;
    ITileRequestResultPtr result;
    std::thread::id thread;
};

struct Recorder {
    std::mutex m;
    std::vector<Call> calls;
    TileRequestTaskFinishNotifierCallBack callBack() {
        return [this](const ITileRequestTask& t, const ITileRequestResultPtr& r) {
            const std::scoped_lock lock(m);
            calls.push_back({&t, r, std::this_thread::get_id()});
        };
    }
};

// n parties meet; a missing party turns a deadlock into a failure after 10 s instead of a hang.
class Rendezvous {
public:
    explicit Rendezvous(int n) : n_(n) {}
    bool arrive() {
        std::unique_lock lock(m_);
        ++count_;
        cv_.notify_all();
        return cv_.wait_for(lock, std::chrono::seconds(10), [&] { return count_ >= n_; });
    }
    bool waitFor(int n) {
        std::unique_lock lock(m_);
        return cv_.wait_for(lock, std::chrono::seconds(10), [&] { return count_ >= n; });
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    int n_;
    int count_ = 0;
};

struct Fixture {
    Recorder rec;
    std::shared_ptr<TileRequestTaskFinishNotifier> notifier =
        std::make_shared<TileRequestTaskFinishNotifier>(rec.callBack());
    ITileRequestPtr request = tileRequest();
    std::stop_source soft, hard;
    std::shared_ptr<TileRequestTask> task =
        std::make_shared<TileRequestTask>(request, soft.get_token(), hard.get_token(), notifier);
};

TEST(TileRequestTask, AccessorsReturnWhatTheConstructorGot) {
    Fixture f;
    EXPECT_EQ(f.task->tileRequest(), f.request);
    EXPECT_FALSE(f.task->softCancel().stop_requested());
    EXPECT_FALSE(f.task->cancel().stop_requested());
    f.soft.request_stop();
    EXPECT_TRUE(f.task->softCancel().stop_requested());
    EXPECT_FALSE(f.task->cancel().stop_requested()); // soft and hard are separate signals
    f.hard.request_stop();
    EXPECT_TRUE(f.task->cancel().stop_requested());
}

TEST(TileRequestTask, DefaultTokensNeverCancel) {
    Fixture f;
    const TileRequestTask t(f.request, {}, {}, f.notifier);
    EXPECT_FALSE(t.softCancel().stop_possible());
    EXPECT_FALSE(t.cancel().stop_possible());
}

TEST(TileRequestTask, SetFinishedNotifiesOnceWithTheTaskAndTheResult) {
    Fixture f;
    const auto r = resultFor(f.request, u"Город"_s);
    f.task->setFinished(r);
    ASSERT_EQ(f.rec.calls.size(), 1U);
    EXPECT_EQ(f.rec.calls[0].task, f.task.get());
    EXPECT_EQ(f.rec.calls[0].result, r);
    EXPECT_EQ(f.rec.calls[0].thread, std::this_thread::get_id()); // on the calling thread
}

// Legacy keeps no "finished" state: every call notifies (u_TileRequestTask.pas:122-125).
TEST(TileRequestTask, SetFinishedTwiceNotifiesTwiceWithEachResult) {
    Fixture f;
    const auto a = resultFor(f.request, u"a"_s);
    const auto b = resultFor(f.request, u"b"_s);
    f.task->setFinished(a);
    f.task->setFinished(b);
    ASSERT_EQ(f.rec.calls.size(), 2U);
    EXPECT_EQ(f.rec.calls[0].result, a);
    EXPECT_EQ(f.rec.calls[1].result, b);
}

TEST(TileRequestTask, NullResultIsForwardedAsNull) {
    Fixture f;
    f.task->setFinished(nullptr);
    ASSERT_EQ(f.rec.calls.size(), 1U);
    EXPECT_EQ(f.rec.calls[0].result, nullptr);
}

TEST(TileRequestTask, CancelBeforeAndAfterFinishDoesNotChangeTheNotification) {
    Fixture f;
    f.soft.request_stop();
    f.hard.request_stop();
    const auto r = resultFor(f.request, u"c"_s);
    f.task->setFinished(r); // cancelled before: still notifies
    EXPECT_EQ(f.rec.calls.size(), 1U);
    Fixture g;
    g.task->setFinished(r);
    g.soft.request_stop();
    g.hard.request_stop(); // cancelled after: nothing more happens
    EXPECT_EQ(g.rec.calls.size(), 1U);
}

TEST(TileRequestTask, DisabledNotifierSwallowsTheFinishAndEnablingRestoresIt) {
    Fixture f;
    EXPECT_TRUE(f.notifier->enabled());
    f.notifier->setEnabled(false);
    EXPECT_FALSE(f.notifier->enabled());
    f.task->setFinished(resultFor(f.request, u"x"_s));
    EXPECT_EQ(f.rec.calls.size(), 0U);
    f.notifier->setEnabled(true);
    EXPECT_TRUE(f.notifier->enabled());
    f.task->setFinished(resultFor(f.request, u"y"_s));
    EXPECT_EQ(f.rec.calls.size(), 1U);
}

TEST(TileRequestTask, CallbackExceptionPropagatesOutOfSetFinished) {
    Fixture f;
    const auto notifier = std::make_shared<TileRequestTaskFinishNotifier>(
        [](const ITileRequestTask&, const ITileRequestResultPtr&) { throw std::runtime_error("cb"); });
    TileRequestTask t(f.request, {}, {}, notifier);
    EXPECT_THROW(t.setFinished(nullptr), std::runtime_error);
}

// Reproductions: legacy raises EAccessViolation for a nil finish notifier / nil callback (Assert is off in
// the shipped build).
TEST(TileRequestTask, NullFinishNotifierThrowsInsteadOfCrashing) {
    TileRequestTask t(tileRequest(), {}, {}, nullptr);
    EXPECT_THROW(t.setFinished(nullptr), std::logic_error);
}

TEST(TileRequestTask, EmptyCallbackThrowsWhenEnabledAndIsHarmlessWhenDisabled) {
    TileRequestTaskFinishNotifier n(TileRequestTaskFinishNotifierCallBack{});
    EXPECT_THROW(n.notify(TileRequestTask(nullptr, {}, {}, nullptr), nullptr), std::bad_function_call);
    n.setEnabled(false);
    EXPECT_NO_THROW(n.notify(TileRequestTask(nullptr, {}, {}, nullptr), nullptr));
}

// The finish comes from another thread while this one waits: the callback runs on the finishing thread.
TEST(TileRequestTask, FinishFromAnotherThreadWhileAWaiterWaits) {
    Fixture f;
    Rendezvous done(1);
    const auto notifier = std::make_shared<TileRequestTaskFinishNotifier>(
        [&](const ITileRequestTask& t, const ITileRequestResultPtr& r) {
            f.rec.callBack()(t, r);
            done.arrive();
        });
    auto task =
        std::make_shared<TileRequestTask>(f.request, f.soft.get_token(), f.hard.get_token(), notifier);
    const auto r = resultFor(f.request, u"w"_s);
    std::thread::id workerId;
    std::thread worker([&] {
        workerId = std::this_thread::get_id();
        task->setFinished(r);
    });
    EXPECT_TRUE(done.waitFor(1));
    worker.join();
    ASSERT_EQ(f.rec.calls.size(), 1U);
    EXPECT_NE(f.rec.calls[0].thread, std::this_thread::get_id());
    EXPECT_EQ(f.rec.calls[0].thread, workerId);
    EXPECT_EQ(f.rec.calls[0].result, r);
}

// No lock around the callback: two finishes of one task are inside it at the same moment.
TEST(TileRequestTask, TwoFinishesOverlapInsideTheCallback) {
    Rendezvous both(2);
    int inside = 0;
    std::mutex m;
    int maxInside = 0;
    const auto notifier = std::make_shared<TileRequestTaskFinishNotifier>(
        [&](const ITileRequestTask&, const ITileRequestResultPtr&) {
            {
                const std::scoped_lock lock(m);
                maxInside = std::max(maxInside, ++inside);
            }
            EXPECT_TRUE(both.arrive());
            const std::scoped_lock lock(m);
            --inside;
        });
    TileRequestTask task(tileRequest(), {}, {}, notifier);
    std::thread a([&] { task.setFinished(nullptr); });
    std::thread b([&] { task.setFinished(nullptr); });
    a.join();
    b.join();
    EXPECT_EQ(maxInside, 2);
}

// Disabling does not wait for a callback that is running (legacy: an Interlocked flag, nothing else).
// Here the callback only returns after setEnabled(false) returned on the other thread.
TEST(TileRequestTask, DisableDoesNotWaitForARunningCallback) {
    Rendezvous entered(1);
    Rendezvous disabled(1);
    std::shared_ptr<TileRequestTaskFinishNotifier> notifier;
    notifier = std::make_shared<TileRequestTaskFinishNotifier>(
        [&](const ITileRequestTask&, const ITileRequestResultPtr&) {
            entered.arrive();
            EXPECT_TRUE(disabled.waitFor(1));
        });
    TileRequestTask task(tileRequest(), {}, {}, notifier);
    std::thread w([&] { task.setFinished(nullptr); });
    EXPECT_TRUE(entered.waitFor(1));
    notifier->setEnabled(false);
    EXPECT_FALSE(notifier->enabled());
    disabled.arrive();
    w.join();
}

} // namespace
