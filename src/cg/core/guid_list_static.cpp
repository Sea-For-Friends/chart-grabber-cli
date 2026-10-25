// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/guid_list_static.h"

#include <algorithm>
#include <memory>

namespace cg::core {

IGuidSetStaticPtr GuidSetStatic::createAndSort(std::vector<QUuid> list) {
    if (list.empty())
        return nullptr;
    // Equal GUIDs are indistinguishable, so the unstable std::sort gives legacy's QuickSort result.
    std::sort(list.begin(), list.end(), GuidLess{});
    list.erase(std::unique(list.begin(), list.end(),
                           [](const QUuid& a, const QUuid& b) { return compareGuids(a, b) == 0; }),
               list.end());
    return std::make_shared<const GuidSetStatic>(std::move(list));
}

IGuidSetStaticPtr GuidSetStatic::createByAdd(const IGuidSetStaticPtr& source, const QUuid& guid) {
    std::vector<QUuid> list;
    if (source) {
        for (int i = 0; i < source->count(); ++i)
            list.push_back(source->item(i));
    }
    list.push_back(guid);
    return createAndSort(std::move(list));
}

IGuidSetStaticPtr GuidSetStatic::createByRemove(const IGuidSetStaticPtr& source, const QUuid& guid) {
    if (!source)
        return nullptr;
    std::vector<QUuid> list;
    for (int i = 0; i < source->count(); ++i) {
        if (compareGuids(source->item(i), guid) != 0)
            list.push_back(source->item(i));
    }
    if (list.empty() && source->count() == 1)
        return nullptr;
    return std::make_shared<const GuidSetStatic>(std::move(list));
}

bool GuidSetStatic::isExists(const QUuid& guid) const {
    return std::binary_search(list_.begin(), list_.end(), guid, GuidLess{});
}

} // namespace cg::core
