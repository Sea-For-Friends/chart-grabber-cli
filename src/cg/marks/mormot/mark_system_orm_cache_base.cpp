// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/mark_system_orm_cache_base.h"

namespace cg::marks {

// TIDDynArrayObject.Find (:313-316)
bool IdDynArrayObject::find(qint64 id) const {
    return array_.find(id) >= 0;
}

// TIDDynArrayObject.Add (:318-321)
void IdDynArrayObject::add(qint64 id) {
    array_.fastLocateOrAddSorted(id);
}

// TIDDynArrayObject.AddArray (:323-373). Ids already present are skipped, but the ids of the
// source are not de-duplicated among themselves (L-157).
void IdDynArrayObject::addArray(const std::vector<qint64>& array, int startIndex, int count) {
    // D-66: a negative start is rejected first (no arithmetic on it); legacy reads before the array.
    if (startIndex < 0) {
        return;
    }
    int vsize = count < 0 ? static_cast<int>(array.size()) - startIndex : count;
    if (vsize <= 0) {
        return;
    }
    // D-66: legacy reads AArray[AStartIndex+I] past the array when count runs over its end ; the port limits
    // the range to the array, as TDynArray.AddArray does itself.
    const int available = static_cast<int>(array.size()) - startIndex;
    vsize = std::min(vsize, available);
    if (vsize <= 0) {
        return;
    }
    if (array_.count() == 0) {
        array_.addArray(array, startIndex, vsize);
        array_.sort();
        return;
    }
    std::vector<bool> bits(static_cast<size_t>(vsize), false);
    int vcount = 0;
    for (int i = 0; i < vsize; ++i) {
        const int src = startIndex + i;
        if (array_.find(array[static_cast<size_t>(src)]) < 0) {
            bits[static_cast<size_t>(i)] = true;
            ++vcount;
        }
    }
    if (vcount == vsize) {
        array_.addArray(array, startIndex, vsize);
        array_.sort();
    } else if (vcount > 0) {
        // legacy: Capacity := Count + VCount (only a reservation; capacity() is never read)
        const int newCount = array_.count() + vcount;
        array_.items().reserve(static_cast<size_t>(newCount));
        for (int i = 0; i < vsize; ++i) {
            if (bits[static_cast<size_t>(i)]) {
                const int j = array_.newRow();
                const int src = startIndex + i;
                array_.items()[static_cast<size_t>(j)] = array[static_cast<size_t>(src)];
            }
        }
        array_.sort();
    }
}

// TIDDynArrayObject.Delete (:375-384)
void IdDynArrayObject::remove(qint64 id) {
    const int i = array_.find(id);
    if (i >= 0) {
        array_.fastDeleteSorted(i);
    }
}

// TIDDynArrayObject.Reset (:386-391)
void IdDynArrayObject::reset() {
    array_.clear();
}

} // namespace cg::marks
