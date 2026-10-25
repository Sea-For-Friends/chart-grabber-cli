// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_TileDownloadSubsystemState.pas; docs/specs/maptype.md 2.13.1 (the priority table), 3.1 (threads).
// Expected texts are the gettext_NoOp literals of the Pascal unit (:88, :92) and the sources' own reasons.
#include "cg/maptype/map_abilities_config.h"
#include "cg/maptype/tile_download_subsystem_state.h"
#include "cg/maptype/tile_downloader_state.h"

#include <QString>

#include <gtest/gtest.h>

#include <atomic>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace cg::maptype;
using namespace Qt::StringLiterals;

namespace {

struct Sources {
    std::shared_ptr<TileDownloaderStateInternal> builder = std::make_shared<TileDownloaderStateInternal>();
    std::shared_ptr<TileDownloaderStateInternal> saver = std::make_shared<TileDownloaderStateInternal>();
    std::shared_ptr<MapAbilitiesConfig> abilities =
        std::make_shared<MapAbilitiesConfig>(std::make_shared<const MapAbilitiesConfigStatic>());

    [[nodiscard]] std::shared_ptr<TileDownloadSubsystemState> state(bool zmpEnabled = true) const {
        auto s = std::make_shared<TileDownloadSubsystemState>(zmpEnabled, builder, saver, abilities);
        s->subscribe();
        return s;
    }
};

} // namespace

// :80-108, in this priority: ZMP, map abilities, request builder, saver. Every row switches off MORE than the
// one reason it expects, so a swapped priority shows.
TEST(TileDownloadSubsystemState, FirstDisablingReasonWinsInTheFixedPriority) {
    struct Row {
        const char* name;
        bool zmpEnabled;
        bool useDownload;
        const char16_t* builderReason; // null = enabled
        const char16_t* saverReason;
        bool expectEnabled;
        const char16_t* expectReason;
    };
    const Row rows[] = {
        {"zmp off beats everything", false, false, u"Город", u"город", false, u"Disabled by Zmp"},
        {"abilities beat builder and saver", true, false, u"Город", u"город", false,
         u"Disabled by map params"},
        {"builder beats saver", true, true, u"Город", u"город", false, u"Город"},
        {"saver alone", true, true, nullptr, u"город", false, u"город"},
        {"all enabled", true, true, nullptr, nullptr, true, u""},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        Sources src;
        if (row.builderReason != nullptr) {
            src.builder->disable(QString::fromUtf16(row.builderReason));
        }
        if (row.saverReason != nullptr) {
            src.saver->disable(QString::fromUtf16(row.saverReason));
        }
        src.abilities->setUseDownload(row.useDownload);
        const auto state = src.state(row.zmpEnabled);
        EXPECT_EQ(state->getStatic()->enabled, row.expectEnabled);
        EXPECT_EQ(state->getStatic()->disableReason, QString::fromUtf16(row.expectReason));
    }
}

// Legacy listens to the three sources only when the ZMP allows downloading (:68-77).
TEST(TileDownloadSubsystemState, ListensToItsThreeSourcesOnlyWhenTheZmpAllowsDownloading) {
    Sources src;
    const auto off = src.state(false);
    EXPECT_EQ(src.builder->changed().subscriberCount(), 0u);
    EXPECT_EQ(src.saver->changed().subscriberCount(), 0u);
    EXPECT_EQ(src.abilities->changed().subscriberCount(), 0u);
    // A source changing is not seen by a disabled-by-ZMP state: still the ZMP's reason, no notification.
    int notified = 0;
    const auto sub = off->changed().subscribe([&] { ++notified; });
    src.saver->disable(u"x"_s);
    EXPECT_EQ(notified, 0);
    EXPECT_EQ(off->getStatic()->disableReason, u"Disabled by Zmp"_s);

    {
        Sources other;
        const auto on = other.state(true);
        EXPECT_EQ(other.builder->changed().subscriberCount(), 1u);
        EXPECT_EQ(other.saver->changed().subscriberCount(), 1u);
        EXPECT_EQ(other.abilities->changed().subscriberCount(), 1u);
    }
}

// The static is rebuilt BEFORE the subscribers are told (DoBeforeChangeNotify), and any change of a source
// notifies, whether or not the outcome changes (no comparison in legacy).
TEST(TileDownloadSubsystemState, StaticIsRebuiltBeforeEachNotificationAndEveryChangeNotifies) {
    Sources src;
    const auto state = src.state();
    std::vector<QString> seen;
    const auto sub = state->changed().subscribe([&] { seen.push_back(state->getStatic()->disableReason); });

    src.saver->disable(u"Город"_s);         // outcome changes
    src.builder->disable(u"город"_s);       // outcome changes (builder outranks saver)
    src.abilities->setIsShowOnSmMap(false); // a change that cannot alter the outcome
    src.abilities->setUseDownload(false);
    src.abilities->setUseDownload(true);
    src.builder->enable();
    src.saver->enable();

    const std::vector<QString> expected = {u"Город"_s, u"город"_s, u"город"_s, u"Disabled by map params"_s,
                                           u"город"_s, u"Город"_s, u""_s};
    EXPECT_EQ(seen, expected);
    EXPECT_TRUE(state->getStatic()->enabled);
}

TEST(TileDownloadSubsystemState, DestroyedStateUnsubscribesAndALateSourceChangeIsHarmless) {
    Sources src;
    {
        const auto state = src.state();
        EXPECT_EQ(src.saver->changed().subscriberCount(), 1u);
    }
    EXPECT_EQ(src.builder->changed().subscriberCount(), 0u);
    EXPECT_EQ(src.saver->changed().subscriberCount(), 0u);
    EXPECT_EQ(src.abilities->changed().subscriberCount(), 0u);
    src.saver->disable(u"late"_s);
}

TEST(TileDownloadSubsystemState, EnabledStateWithANullSourceIsRejected) {
    Sources src;
    EXPECT_THROW(TileDownloadSubsystemState(true, nullptr, src.saver, src.abilities), std::invalid_argument);
    EXPECT_THROW(TileDownloadSubsystemState(true, src.builder, nullptr, src.abilities),
                 std::invalid_argument);
    EXPECT_THROW(TileDownloadSubsystemState(true, src.builder, src.saver, nullptr), std::invalid_argument);
    const TileDownloadSubsystemState off(false, nullptr, nullptr, nullptr);
    EXPECT_EQ(off.getStatic()->disableReason, u"Disabled by Zmp"_s);
}

// Two sources change at once on two threads: whichever publishes last has read both, so the final state is
// the builder's reason (it outranks the saver's) in every interleaving. TSan also watches the exchange.
TEST(TileDownloadSubsystemState, ConcurrentChangesOfTwoSourcesEndInTheNewestState) {
    for (int round = 0; round < 50; ++round) {
        Sources src;
        const auto state = src.state();
        std::latch go(2);
        std::atomic<int> notifications{0};
        const auto sub = state->changed().subscribe([&] { ++notifications; });
        std::thread a([&] {
            go.arrive_and_wait();
            src.saver->disable(u"saver"_s);
        });
        std::thread b([&] {
            go.arrive_and_wait();
            src.builder->disable(u"builder"_s);
        });
        a.join();
        b.join();
        EXPECT_EQ(notifications.load(), 2);
        EXPECT_FALSE(state->getStatic()->enabled);
        EXPECT_EQ(state->getStatic()->disableReason, u"builder"_s);
    }
}
