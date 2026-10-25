// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/u_SortFunc.pas:465-509 (SortInterfaceListByCompareFunction and its nested QuickSort).
//
// Legacy's own quicksort, ported literally: it is NOT stable and the order of elements that
// compare equal is observable (TVectorItemSubsetBuilder.RemoveDuplicates picks its survivor by it).
// Never replace it by std::sort / std::stable_sort. The other sorts of u_SortFunc are not ported here.
// Lives in the vector module because the builder is its only user (module map: Src root -> app,
// which vector may not depend on).

#include <utility>
#include <vector>

namespace cg::vector {

namespace detail {

template <class T, class Compare> void quickSort(std::vector<T>& list, Compare& compare, int l, int r) {
    int i = 0;
    int j = 0;
    do {
        i = l;
        j = r;
        const T p = list[static_cast<std::size_t>((l + r) >> 1)];
        do {
            while (compare(list[static_cast<std::size_t>(i)], p) < 0) {
                ++i;
            }
            while (compare(list[static_cast<std::size_t>(j)], p) > 0) {
                --j;
            }
            if (i <= j) {
                std::swap(list[static_cast<std::size_t>(i)], list[static_cast<std::size_t>(j)]);
                ++i;
                --j;
            }
        } while (i <= j);
        if (l < j) {
            quickSort(list, compare, l, j);
        }
        l = i;
    } while (i < r);
}

} // namespace detail

// compare(a, b) returns <0, 0, >0 like the legacy TInterfaceListSortCompareFunction.
template <class T, class Compare>
void sortInterfaceListByCompareFunction(std::vector<T>& list, Compare compare) {
    const int count = static_cast<int>(list.size());
    if (count > 1) {
        detail::quickSort(list, compare, 0, count - 1);
    }
}

} // namespace cg::vector
