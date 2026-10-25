// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-037. Ports u_SortFunc.pas:465-509 (SortInterfaceListByCompareFunction);
// docs/specs/vector.md §2.6.3, §7.2.
#include "cg/vector/sort_func.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

using cg::vector::sortInterfaceListByCompareFunction;

namespace {

using Entry = std::pair<int, char>; // (key, tag): the comparator looks at the key only

// Legacy CompareVectorItems shape: descending by key.
int descending(const Entry& a, const Entry& b) {
    if (a.first == b.first) {
        return 0;
    }
    return a.first < b.first ? 1 : -1;
}

std::string tags(const std::vector<Entry>& list) {
    std::string result;
    for (const auto& e : list) {
        result += e.second;
    }
    return result;
}

} // namespace

// The expected strings are derived by hand from the Pascal (see docs/handoffs/T-0057.md); a stable
// sort gives "BDAC" and "ABC" resp. "AB", so these inputs separate the literal port from a library sort.
TEST(SortFunc, LiteralQuickSortExchangesAreReproduced) {
    struct Case {
        const char* name;
        std::vector<Entry> input;
        const char* expected;
    };
    const Case cases[] = {
        {"two equal keys swap", {{5, 'A'}, {5, 'B'}}, "BA"},
        {"three equal keys reverse", {{5, 'A'}, {5, 'B'}, {5, 'C'}}, "CBA"},
        {"4 elements, two recursion levels", {{3, 'A'}, {9, 'B'}, {3, 'C'}, {9, 'D'}}, "DBAC"},
        {"empty and single are untouched", {}, ""},
        {"single", {{1, 'X'}}, "X"},
    };
    for (const auto& c : cases) {
        auto list = c.input;
        sortInterfaceListByCompareFunction(list, descending);
        EXPECT_EQ(tags(list), c.expected) << c.name;
    }
}
