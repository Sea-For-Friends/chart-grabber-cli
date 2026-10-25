// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/guid_set.h"

namespace cg::core {

int compareGuids(const QUuid& a, const QUuid& b) noexcept {
    if (a.data1 != b.data1)
        return a.data1 > b.data1 ? 1 : -1;
    if (a.data2 != b.data2)
        return a.data2 > b.data2 ? 1 : -1;
    if (a.data3 != b.data3)
        return a.data3 > b.data3 ? 1 : -1;
    for (int i = 0; i < 8; ++i) {
        if (a.data4[i] != b.data4[i])
            return a.data4[i] > b.data4[i] ? 1 : -1;
    }
    return 0;
}

} // namespace cg::core
