// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/region/export_task_abstract (Src/RegionProcess/Export/u_ExportTaskAbstract.pas :53-102).
// T-0172 (E4 of docs/specs/region-export.md). No FEAT tag: shared base unit.
//
// The zoom filter is observed through a derived class that exposes zooms_ (protected, as FZooms :37).
// Expected values are literals derived by hand from the loop of :74-93.
#include "cg/core/result.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_projected.h"
#include "cg/geo/tile_count.h"
#include "cg/region/export_task_abstract.h"
#include "cg/region/region_process_progress_info.h"

#include <QString>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>
#include <vector>

using cg::core::ErrorCode;
using cg::region::ExportTaskAbstract;
using cg::region::RegionProcessProgressInfo;

namespace {

class ZoomProbe final : public ExportTaskAbstract {
public:
    explicit ZoomProbe(const std::vector<quint8>& zooms)
        : ExportTaskAbstract(std::make_shared<RegionProcessProgressInfo>(std::stop_token{}), nullptr, zooms,
                             std::make_shared<const cg::geo::TileIteratorFactory>(
                                 std::make_shared<const cg::geo::GeometryProjectedFactory>())) {}
    [[nodiscard]] const std::vector<quint8>& zooms() const { return zooms_; }
};

std::vector<quint8> ascending(int from, int to) { // from..to inclusive
    std::vector<quint8> v;
    for (int i = from; i <= to; ++i)
        v.push_back(static_cast<quint8>(i));
    return v;
}

} // namespace

// :74-93: a filter, not a sort. An entry is kept when it is < 24 and (nothing kept yet or strictly
// greater than the last kept one); only the first 24 entries are looked at.
TEST(ExportTaskAbstract, ZoomFilterKeepsAscendingEntriesBelow24OfTheFirst24) {
    std::vector<quint8> longList = ascending(0, 22); // 23 entries, all kept
    longList.push_back(5);                           // index 23: not greater than 22, dropped
    longList.push_back(23);                          // index 24: valid and greater, but beyond the first 24
    longList.push_back(24);                          // index 25
    ASSERT_EQ(longList.size(), 26U);

    struct Case {
        const char* label;
        std::vector<quint8> in;
        std::vector<quint8> out;
    };
    const std::vector<Case> cases = {
        {"filter not sort, 7 repeated, 30 out of range", {5, 3, 7, 7, 30}, {5, 7}},
        {"unsorted input is not reordered", {5, 3, 7}, {5, 7}},
        {"zoom 0 is kept as the first entry", {0, 2}, {0, 2}},
        {"23 is the last valid zoom", {22, 23}, {22, 23}},
        {"24 is dropped, 23 before it is kept", {23, 24}, {23}},
        {"24 dropped without anything kept does not block a later entry", {24, 5}, {5}},
        {"a smaller entry after the last kept one is dropped", {9, 4, 11}, {9, 11}},
        {"equal entry is dropped", {6, 6, 6}, {6}},
        {"255 is dropped", {3, 255, 4}, {3, 4}},
        {"only the first 24 of 26 entries are looked at", longList, ascending(0, 22)},
    };
    for (const Case& c : cases) {
        const ZoomProbe probe(c.in);
        EXPECT_EQ(probe.zooms(), c.out) << c.label;
    }
}

// :99-101: nothing left -> the message of the raise, verbatim (not translated in legacy).
TEST(ExportTaskAbstract, ProcessRegionReturnsParseErrorWhenNoZoomIsLeft) {
    const std::vector<std::vector<quint8>> inputs = {{}, {24}, {30, 40}};
    for (const auto& in : inputs) {
        ZoomProbe probe(in);
        const auto r = probe.processRegion();
        ASSERT_FALSE(r.has_value());
        EXPECT_EQ(r.error().code(), ErrorCode::ParseError);
        EXPECT_EQ(r.error().what(), QStringLiteral("Please select at least one zoom"));
    }
}

TEST(ExportTaskAbstract, ProcessRegionSucceedsWithOneKeptZoom) {
    ZoomProbe probe({24, 5});
    EXPECT_TRUE(probe.processRegion().has_value());
}
