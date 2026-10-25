// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_GUIDListStatic.pas (IGUIDSetStatic), u_GUIDListStatic.pas (TGUIDSetStatic).
// IGUIDListStatic / TGUIDListStatic (ordered list) are not ported (idioms.md: std::vector<QUuid>).
//
// Immutable set of GUIDs in ascending compareGuids order (guid_set.h), no duplicates. Legacy returns
// nil for an empty set; so do the factories (null pointer).

#include "cg/core/guid_set.h"

#include <QUuid>

#include <memory>
#include <vector>

namespace cg::core {

// FEAT-GEO-033, FEAT-GEO-034
class IGuidSetStatic {
public:
    virtual ~IGuidSetStatic() = default;
    [[nodiscard]] virtual QUuid item(int index) const = 0;
    [[nodiscard]] virtual int count() const = 0;
    [[nodiscard]] virtual bool isExists(const QUuid& guid) const = 0;
};
using IGuidSetStaticPtr = std::shared_ptr<const IGuidSetStatic>;

class GuidSetStatic final : public IGuidSetStatic {
public:
    // Sorts and drops duplicates; an empty input gives null.
    static IGuidSetStaticPtr createAndSort(std::vector<QUuid> list);
    // A null source is the empty set. Adding a present GUID gives an equal set.
    static IGuidSetStaticPtr createByAdd(const IGuidSetStaticPtr& source, const QUuid& guid);
    // Source must be non-null (legacy Assert; null gives null). Removing the only element gives
    // null. Removing an absent GUID gives a copy (legacy: Assert in debug builds only).
    static IGuidSetStaticPtr createByRemove(const IGuidSetStaticPtr& source, const QUuid& guid);

    // Use the factories: the argument must already be sorted and duplicate-free.
    explicit GuidSetStatic(std::vector<QUuid> sorted) : list_(std::move(sorted)) {}

    [[nodiscard]] QUuid item(int index) const override { return list_[static_cast<std::size_t>(index)]; }
    [[nodiscard]] int count() const override { return static_cast<int>(list_.size()); }
    [[nodiscard]] bool isExists(const QUuid& guid) const override;

private:
    std::vector<QUuid> list_;
};

} // namespace cg::core
