// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MapTypeSetChangeableSimple.pas (TMapTypeSetChangeableSimple and its
//        IMapTypeSetChangeableSimpleInternal, which collapses into setStatic)
//
// Holder of the full map set. setStatic NEVER notifies, as in legacy: the body has no
// DoChangeNotify call (u_MapTypeSetChangeableSimple.pas:76-92); see docs/parity/legacy-defects.md.

#include "cg/core/snapshot.h"
#include "cg/maptype/map_type_set_changeable.h"

#include <memory>

namespace cg::maptype {

class MapTypeSetChangeableSimple final : public IMapTypeSetChangeable {
public:
    // `current` may be null (legacy passes nil); see setStatic.
    MapTypeSetChangeableSimple(std::shared_ptr<const IMapTypeSetBuilderFactory> factory,
                               const IMapTypeSetPtr& current);
    ~MapTypeSetChangeableSimple() override;

    [[nodiscard]] IMapTypeSetPtr getStatic() const override;
    // A null value is replaced by the result of an empty no-nil builder, which is null: so a
    // null argument leaves a null set. Any other value is stored as is, without comparison and
    // without notification.
    void setStatic(const IMapTypeSetPtr& value);

private:
    std::shared_ptr<const IMapTypeSetBuilderFactory> factory_;
    core::Snapshot<IMapTypeSet> static_;
};

} // namespace cg::maptype
