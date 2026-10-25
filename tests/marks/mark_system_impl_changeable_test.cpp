// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkSystemImplChangeable.pas (spec marks.md section 2.6, ruling A). No FEAT id: ticket T-0102 has
// Features: —. The factory list, the factory and its list element are fakes written here; the config list
// is the landed MarkSystemConfig, the base path the landed PathConfig, the error notifier a plain
// core::Notifier, and the backend the shared testutil::FakeMarkSystemImpl. Synchronisation is by
// semaphores and latches only: no sleeps.

#include "cg/core/notifier.h"
#include "cg/core/path_config.h"
#include "cg/core/read_write_state_internal.h"
#include "cg/marks/c_mark_system.h"
#include "cg/marks/mark_system_config.h"
#include "cg/marks/mark_system_impl_changeable.h"
#include "cg/marks/sml/mark_system_impl_config_sml.h"

#include <QString>
#include <QUuid>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <semaphore>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

#include "mark_system_fakes.h"

namespace m = cg::marks;
namespace t = cg::marks::testutil;
namespace core = cg::core;

namespace {

using Gate = std::binary_semaphore;

// Answers build() with onBuild(stop, basePath); records every call and the path it was given.
class FakeFactory final : public m::IMarkSystemImplFactory {
public:
    std::function<std::shared_ptr<m::IMarkSystemImpl>(std::stop_token, const QString&)> onBuild;
    std::atomic<int> calls{0};

    // Written by the task's thread, read by the test: guarded by a mutex (ThreadSanitizer sees no ordering
    // from the semaphore the test waits on), copied out by pathList().
    [[nodiscard]] std::vector<QString> pathList() const {
        const std::lock_guard lock(pathsMutex_);
        return paths_;
    }

    [[nodiscard]] bool isInitializationRequired() const override { return false; }
    std::shared_ptr<m::IMarkSystemImpl> build(std::stop_token stop, const QString& basePath,
                                              const m::MarkSystemImplConfigStaticPtr&) override {
        ++calls;
        {
            const std::lock_guard lock(pathsMutex_);
            paths_.push_back(basePath);
        }
        return onBuild(std::move(stop), basePath);
    }

private:
    mutable std::mutex pathsMutex_;
    std::vector<QString> paths_;
};

class FakeElement final : public m::IMarkSystemImplFactoryListElement {
public:
    FakeElement(QUuid guid, std::shared_ptr<m::IMarkSystemImplFactory> factory)
        : guid_(guid), factory_(std::move(factory)) {}
    [[nodiscard]] QUuid guid() const override { return guid_; }
    [[nodiscard]] QString caption() const override { return {}; }
    [[nodiscard]] std::shared_ptr<m::IMarkSystemImplFactory> factory() const override { return factory_; }

private:
    QUuid guid_;
    std::shared_ptr<m::IMarkSystemImplFactory> factory_;
};

// Answers get() from its elements. A lookup of a GUID that is not registered releases `misses`.
class FakeFactoryList final : public m::IMarkSystemImplFactoryListStatic {
public:
    std::vector<m::MarkSystemImplFactoryListElementPtr> elements;
    mutable std::counting_semaphore<> misses{0};

    void add(const QUuid& guid, std::shared_ptr<m::IMarkSystemImplFactory> factory) {
        elements.push_back(std::make_shared<FakeElement>(guid, std::move(factory)));
    }
    [[nodiscard]] std::vector<QUuid> guidEnum() const override {
        std::vector<QUuid> guids;
        for (const auto& e : elements) {
            guids.push_back(e->guid());
        }
        return guids;
    }
    [[nodiscard]] m::MarkSystemImplFactoryListElementPtr get(const QUuid& guid) const override {
        for (const auto& e : elements) {
            if (e->guid() == guid) {
                return e;
            }
        }
        misses.release();
        return nullptr;
    }
};

// What one change notification of the object under test found published.
struct Snapshot {
    bool hasStatic;     // getStatic() != nullptr
    bool stateReadable; // state()->getStatic()->readAccess
};

// Records each change notification, in order, with the snapshot taken at that moment. The test waits for
// notifications with await(); the records of those notifications are then visible to it.
class ChangeRecorder {
public:
    explicit ChangeRecorder(m::IMarkSystemImplChangeable& target)
        : target_(target), subscription_(target.changed().subscribe([this] { record(); })) {}

    void await(std::ptrdiff_t n) {
        for (; n > 0; --n) {
            seen_.acquire();
        }
    }
    // Waits until a notification finds a published backend. Notifications that find none are skipped,
    // so this does not depend on whether the first (null) notification was recorded.
    void awaitPublished() {
        for (;;) {
            seen_.acquire();
            if (last().hasStatic) {
                return;
            }
        }
    }
    [[nodiscard]] std::vector<Snapshot> snapshots() const {
        const std::lock_guard lock(mutex_); // the task's thread may still be notifying
        return snapshots_;
    }

private:
    void record() {
        const Snapshot snap{target_.getStatic() != nullptr, target_.state()->getStatic()->readAccess};
        {
            const std::lock_guard lock(mutex_);
            snapshots_.push_back(snap);
        }
        seen_.release();
    }
    [[nodiscard]] Snapshot last() const {
        const std::lock_guard lock(mutex_);
        return snapshots_.back();
    }

    m::IMarkSystemImplChangeable& target_;
    std::counting_semaphore<> seen_{0};
    mutable std::mutex mutex_;
    std::vector<Snapshot> snapshots_;
    core::Subscription subscription_; // declared last: unsubscribed first
};

m::MarkSystemImplConfigStaticPtr implConfig() {
    return std::make_shared<m::MarkSystemImplConfigSML>(QStringLiteral("Marks.sml"), false);
}

} // namespace

class MarkSystemImplChangeableTest : public ::testing::Test {
protected:
    void SetUp() override {
        product = std::make_shared<t::FakeMarkSystemImpl>();
        product->stateObject = std::make_shared<core::ReadWriteStateInternal>(true, false);
        sqlite = std::make_shared<FakeFactory>();
        sqlite->onBuild = [this](std::stop_token, const QString&) -> std::shared_ptr<m::IMarkSystemImpl> {
            return product;
        };
        list = std::make_shared<FakeFactoryList>();
        list->add(m::cORMSQLiteMarksDbGUID, sqlite);
        basePath = core::PathConfig::create(QString(), QStringLiteral("/data/marks"), nullptr);
        config = std::make_shared<m::MarkSystemConfig>();
        errorSubscription = errorNotifier.subscribe([this] { ++errors; });
    }

    // Adds a config item (SML impl) for `db`; returns its id.
    int addItem(const QUuid& db, bool active) {
        return config->add(db, QStringLiteral("My Marks"), implConfig(), active);
    }

    // The object under test, with the fixture's parts. appStarted is the token given by the caller.
    std::unique_ptr<m::MarkSystemImplChangeable> make(std::stop_token started) {
        return std::make_unique<m::MarkSystemImplChangeable>(basePath, config, list, errorNotifier,
                                                             std::move(started), appClosing.get_token());
    }

    std::shared_ptr<t::FakeMarkSystemImpl> product; // what a successful build answers
    std::shared_ptr<FakeFactory> sqlite;            // registered under cORMSQLiteMarksDbGUID
    std::shared_ptr<FakeFactoryList> list;
    core::PathConfigPtr basePath;
    std::shared_ptr<m::MarkSystemConfig> config;
    core::Notifier errorNotifier;
    std::atomic<int> errors{0};
    core::Subscription errorSubscription;
    std::stop_source appStarted;
    std::stop_source appClosing;
    Gate gate{0};
};

// Fixture variants for the parameterised tests.
class StoppedBuildTest : public MarkSystemImplChangeableTest, public ::testing::WithParamInterface<bool> {};
class FullPathTest : public MarkSystemImplChangeableTest,
                     public ::testing::WithParamInterface<std::string> {};
enum class Missing { BasePath, ConfigList };
class NullArgumentTest : public MarkSystemImplChangeableTest,
                         public ::testing::WithParamInterface<Missing> {};

// Nothing runs before the application has started: the task has no thread until then, so a config change
// only leaves a request behind.
TEST_F(MarkSystemImplChangeableTest, NothingRunsBeforeAppStarted) {
    const int id = addItem(m::cORMSQLiteMarksDbGUID, true);
    auto obj = make(appStarted.get_token()); // not requested
    ChangeRecorder rec(*obj);

    config->update(id, m::cORMSQLiteMarksDbGUID, QStringLiteral("Renamed"), implConfig());

    EXPECT_EQ(sqlite->calls.load(), 0);
    EXPECT_TRUE(rec.snapshots().empty());
    EXPECT_FALSE(obj->getStatic());

    // The pending request from the config change is not lost by the start. Legacy OnAppStarted makes two
    // requests (Start, then StartExecute: u_MarkSystemImplChangeable.pas:223-225), so the start may run one
    // build or two, depending on whether the task thread takes the first request before the second arrives.
    // Pinned: at least one build, and each build notifies twice.
    appStarted.request_stop();
    rec.await(2);
    obj.reset();
    const int builds = sqlite->calls.load();
    EXPECT_GE(builds, 1);
    EXPECT_EQ(rec.snapshots().size(), 2u * static_cast<std::size_t>(builds));
}

// The started flag is set before construction: the task starts in the constructor. The first notification
// (static dropped) may come before the recorder subscribes, so the test waits for the published one only.
TEST_F(MarkSystemImplChangeableTest, BuildsOnceWhenAppStartedBeforeConstruction) {
    addItem(m::cORMSQLiteMarksDbGUID, true);
    sqlite->onBuild = [this](std::stop_token, const QString&) -> std::shared_ptr<m::IMarkSystemImpl> {
        gate.acquire(); // holds the build until the recorder is subscribed
        return product;
    };
    appStarted.request_stop();
    auto obj = make(appStarted.get_token());
    ChangeRecorder rec(*obj);
    gate.release();

    rec.awaitPublished();

    EXPECT_EQ(sqlite->calls.load(), 1);
    EXPECT_EQ(obj->getStatic().get(), product.get());
    EXPECT_TRUE(obj->state()->getStatic()->readAccess);
    obj.reset();
    EXPECT_EQ(sqlite->calls.load(), 1);
}

// Started after construction: the start itself runs the build. Two notifications, the first with the static
// null and the state proxy detached, the second with the backend published and the proxy following it.
TEST_F(MarkSystemImplChangeableTest, PublishesNullThenTheBackendAroundOneBuild) {
    addItem(m::cORMSQLiteMarksDbGUID, true);
    auto obj = make(appStarted.get_token());
    ChangeRecorder rec(*obj);

    appStarted.request_stop();
    rec.await(2);

    EXPECT_EQ(sqlite->calls.load(), 1);
    EXPECT_EQ(obj->getStatic().get(), product.get());
    EXPECT_EQ(errors.load(), 0);
    obj.reset();
    const std::vector<Snapshot> s = rec.snapshots();
    ASSERT_EQ(s.size(), 2u);
    EXPECT_FALSE(s[0].hasStatic);
    EXPECT_FALSE(s[0].stateReadable);
    EXPECT_TRUE(s[1].hasStatic);
    EXPECT_TRUE(s[1].stateReadable);
}

// A throwing build: the error notifier fires once, inside the catch and before the publish notification; the
// static stays null; the process goes on.
TEST_F(MarkSystemImplChangeableTest, ThrowingBuildKeepsStaticNullFiresErrorOnceAndNotifiesTwice) {
    addItem(m::cORMSQLiteMarksDbGUID, true);
    sqlite->onBuild = [](std::stop_token, const QString&) -> std::shared_ptr<m::IMarkSystemImpl> {
        throw std::runtime_error("boom");
    };
    auto obj = make(appStarted.get_token());
    ChangeRecorder rec(*obj);

    appStarted.request_stop();
    rec.await(2);

    EXPECT_EQ(sqlite->calls.load(), 1);
    EXPECT_EQ(errors.load(), 1);
    EXPECT_FALSE(obj->getStatic());
    obj.reset();
    const std::vector<Snapshot> s = rec.snapshots();
    ASSERT_EQ(s.size(), 2u);
    EXPECT_FALSE(s[0].hasStatic);
    EXPECT_FALSE(s[1].hasStatic);
    EXPECT_FALSE(s[1].stateReadable);
}

// The active item names a GUID that is not registered. The lookup returns null, the task's procedure throws
// (the task logs it and carries on): one notification only, no error notification, no build. A later config
// change to a registered item builds again.
TEST_F(MarkSystemImplChangeableTest, FactoryMissStopsAtOneNotificationAndBuildsAgainOnConfigChange) {
    const int sqliteId = addItem(m::cORMSQLiteMarksDbGUID, false);
    addItem(m::cORMMongoDbMarksDbGUID, true); // active, not in the list
    auto obj = make(appStarted.get_token());
    ChangeRecorder rec(*obj);

    appStarted.request_stop();
    rec.await(1);           // the first notification, before the lookup
    list->misses.acquire(); // the lookup of the unregistered GUID has returned null
    config->setActiveConfigId(sqliteId);
    rec.await(2); // the second run: its null notification, then the published backend

    EXPECT_EQ(sqlite->calls.load(), 1);
    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(obj->getStatic().get(), product.get());
    obj.reset();
    const std::vector<Snapshot> s = rec.snapshots();
    ASSERT_EQ(s.size(), 3u);
    EXPECT_FALSE(s[0].hasStatic);
    EXPECT_FALSE(s[1].hasStatic);
    EXPECT_TRUE(s[2].hasStatic);
    EXPECT_TRUE(s[2].stateReadable);
}

// No active item: the static is published as null, twice, and nothing is built.
TEST_F(MarkSystemImplChangeableTest, NoActiveConfigPublishesNullTwiceAndBuildsNothing) {
    auto obj = make(appStarted.get_token());
    ChangeRecorder rec(*obj);

    appStarted.request_stop();
    rec.await(2);

    EXPECT_EQ(sqlite->calls.load(), 0);
    EXPECT_EQ(errors.load(), 0);
    obj.reset();
    const std::vector<Snapshot> s = rec.snapshots();
    ASSERT_EQ(s.size(), 2u);
    EXPECT_FALSE(s[0].hasStatic);
    EXPECT_FALSE(s[1].hasStatic);
}

// A registered element whose factory is nil: the build is skipped without an error.
TEST_F(MarkSystemImplChangeableTest, ElementWithoutFactorySkipsTheBuildSilently) {
    list->elements.clear();
    list->add(m::cORMSQLiteMarksDbGUID, nullptr);
    addItem(m::cORMSQLiteMarksDbGUID, true);
    auto obj = make(appStarted.get_token());
    ChangeRecorder rec(*obj);

    appStarted.request_stop();
    rec.await(2);

    EXPECT_EQ(sqlite->calls.load(), 0);
    EXPECT_EQ(errors.load(), 0);
    obj.reset();
    const std::vector<Snapshot> s = rec.snapshots();
    ASSERT_EQ(s.size(), 2u);
    EXPECT_FALSE(s[0].hasStatic);
    EXPECT_FALSE(s[1].hasStatic);
}

// A config change while a build is running (true: the base path changed; false: the active item was
// updated) stops that build, which then publishes what it returned, and the task runs once more.
TEST_P(StoppedBuildTest, ChangeStopsTheRunningBuildAndRunsItAgain) {
    const int id = addItem(m::cORMSQLiteMarksDbGUID, true);
    auto second = std::make_shared<t::FakeMarkSystemImpl>();
    std::binary_semaphore entered{0};
    std::binary_semaphore stopped{0};
    sqlite->onBuild = [&](std::stop_token stop, const QString&) -> std::shared_ptr<m::IMarkSystemImpl> {
        if (sqlite->calls.load() == 1) {
            const std::stop_callback cancelled(stop, [&] { stopped.release(); });
            entered.release();
            stopped.acquire(); // released only when the change requests the stop
            return product;
        }
        return second;
    };
    auto obj = make(appStarted.get_token());
    ChangeRecorder rec(*obj);

    appStarted.request_stop();
    entered.acquire();
    if (GetParam()) {
        basePath->setPath(QStringLiteral("/data/other"));
    } else {
        config->update(id, m::cORMSQLiteMarksDbGUID, QStringLiteral("Renamed"), implConfig());
    }
    rec.await(4); // 1 null, 2 first build published, 3 null, 4 second build published

    EXPECT_EQ(sqlite->calls.load(), 2);
    EXPECT_EQ(obj->getStatic().get(), second.get());
    obj.reset();
    const std::vector<Snapshot> s = rec.snapshots();
    ASSERT_EQ(s.size(), 4u);
    EXPECT_TRUE(s[1].hasStatic);
    EXPECT_FALSE(s[2].hasStatic);
    EXPECT_FALSE(s[2].stateReadable); // the state proxy is detached while the second build runs
    EXPECT_TRUE(s[3].hasStatic);
}

INSTANTIATE_TEST_SUITE_P(Triggers, StoppedBuildTest, ::testing::Bool());

// A config change rebuilds the backend once: two notifications for that build, and no more.
TEST_F(MarkSystemImplChangeableTest, ConfigChangeRebuildsOnceAndNotifiesTwice) {
    const int id = addItem(m::cORMSQLiteMarksDbGUID, true);
    auto obj = make(appStarted.get_token());
    ChangeRecorder rec(*obj);
    appStarted.request_stop();
    rec.await(2);

    config->update(id, m::cORMSQLiteMarksDbGUID, QStringLiteral("Renamed"), implConfig());
    rec.await(2);

    EXPECT_EQ(sqlite->calls.load(), 2);
    EXPECT_EQ(obj->getStatic().get(), product.get());
    obj.reset();
    const std::vector<Snapshot> s = rec.snapshots();
    ASSERT_EQ(s.size(), 4u);
    EXPECT_FALSE(s[2].hasStatic);
    EXPECT_TRUE(s[3].hasStatic);
}

// The full path is handed to the backend verbatim: with and without a trailing delimiter.
TEST_P(FullPathTest, BuildGetsTheFullPathVerbatim) {
    const QString path = QString::fromStdString(GetParam());
    basePath->setPath(path);
    addItem(m::cORMSQLiteMarksDbGUID, true);
    auto obj = make(appStarted.get_token());
    ChangeRecorder rec(*obj);

    appStarted.request_stop();
    rec.await(2);

    const std::vector<QString> paths = sqlite->pathList();
    ASSERT_EQ(paths.size(), 1u);
    EXPECT_EQ(paths[0], path);
    obj.reset();
}

INSTANTIATE_TEST_SUITE_P(Paths, FullPathTest,
                         ::testing::Values(std::string("/data/marks/"), std::string("/data/marks")));

// The application-closing token, fired from inside the running build, ends the task: the build is not
// repeated although a config change in the same build requested another run. Both the token and the change
// are made by the build itself, before it returns, so the closing is in effect when the task's loop checks
// for a new run.
TEST_F(MarkSystemImplChangeableTest, AppClosingEndsTheTask) {
    const int id = addItem(m::cORMSQLiteMarksDbGUID, true);
    sqlite->onBuild = [&](std::stop_token, const QString&) -> std::shared_ptr<m::IMarkSystemImpl> {
        appClosing.request_stop(); // ends the task (its callback runs on this thread)
        config->update(id, m::cORMSQLiteMarksDbGUID, QStringLiteral("Renamed"), implConfig());
        return product;
    };
    auto obj = make(appStarted.get_token());
    ChangeRecorder rec(*obj);
    appStarted.request_stop();
    rec.await(2);

    obj.reset(); // joins the task: a second build, if one had started, has finished by now

    EXPECT_EQ(sqlite->calls.load(), 1);
    EXPECT_EQ(rec.snapshots().size(), 2u);
}

// Null constructor arguments are refused: legacy has no check (Assert off) and a nil base path or config list
// is an access violation in the constructor (:124-125).
TEST_P(NullArgumentTest, NilArgumentIsRefusedAtConstruction) {
    core::PathConfigPtr bp = GetParam() == Missing::BasePath ? nullptr : basePath;
    std::shared_ptr<m::IMarkSystemConfigListChangeable> cfg =
        GetParam() == Missing::ConfigList ? nullptr : config;
    EXPECT_THROW(std::make_unique<m::MarkSystemImplChangeable>(
                     bp, cfg, list, errorNotifier, appStarted.get_token(), appClosing.get_token()),
                 std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(Nil, NullArgumentTest, ::testing::Values(Missing::BasePath, Missing::ConfigList));

// A nil factory list is accepted at construction. Legacy dereferences the lookup result at :175 inside the
// task, an access violation caught by the thread loop: the procedure throws there, after the first
// notification and before any build, so one notification, no error notification, and nothing published.
TEST_F(MarkSystemImplChangeableTest, NilFactoryListFailsInsideTheTaskAndNotifiesOnce) {
    addItem(m::cORMSQLiteMarksDbGUID, true);
    auto obj = std::make_unique<m::MarkSystemImplChangeable>(basePath, config, nullptr, errorNotifier,
                                                             appStarted.get_token(), appClosing.get_token());
    ChangeRecorder rec(*obj);
    appStarted.request_stop();
    rec.await(1);

    obj.reset(); // joins the task

    EXPECT_EQ(sqlite->calls.load(), 0);
    EXPECT_EQ(errors.load(), 0);
    const std::vector<Snapshot> s = rec.snapshots();
    ASSERT_EQ(s.size(), 1u);
    EXPECT_FALSE(s[0].hasStatic);
    EXPECT_FALSE(s[0].stateReadable);
}

// Thread test. The build is held until the destructor cancels it: the destruction starts while the build is
// in flight, and must not return before the build has returned. Run >= 1000 times on the debug binary
// (--gtest_repeat) and under --tsan.
TEST_F(MarkSystemImplChangeableTest, DestructionWaitsForTheBuildInFlight) {
    addItem(m::cORMSQLiteMarksDbGUID, true);
    std::binary_semaphore entered{0};
    std::binary_semaphore cancelled{0};
    std::atomic<bool> returned{false};
    sqlite->onBuild = [&](std::stop_token stop, const QString&) -> std::shared_ptr<m::IMarkSystemImpl> {
        const std::stop_callback onStop(stop, [&] { cancelled.release(); });
        entered.release();
        cancelled.acquire(); // the destructor's cancel is the only thing that lets the build go on
        returned = true;
        return product;
    };
    appStarted.request_stop();
    auto obj = make(appStarted.get_token());
    entered.acquire();
    EXPECT_FALSE(obj->getStatic()); // a build in flight holds no lock: the static is already dropped

    obj.reset();

    EXPECT_TRUE(returned.load());
}
