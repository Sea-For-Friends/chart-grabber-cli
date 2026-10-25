// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/layer_draw_config.h"

namespace cg::maptype {

LayerDrawConfig::LayerDrawConfig(qint32 layerZOrderDefault)
    : layerZOrderDef_(layerZOrderDefault), layerZOrder_(layerZOrderDefault) {}

LayerDrawConfig::~LayerDrawConfig() = default;

qint32 LayerDrawConfig::layerZOrder() const {
    const auto guard = readLock();
    return layerZOrder_;
}

void LayerDrawConfig::setLayerZOrder(qint32 v) {
    const auto guard = writeLock();
    if (layerZOrder_ != v) {
        layerZOrder_ = v;
        setChanged();
    }
}

// A non-null provider always ends with setChanged(), even when the value is unchanged.
void LayerDrawConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    if (!src) {
        return;
    }
    layerZOrder_ = src->readInt(u"LayerZOrder", layerZOrder_);
    setChanged();
}

void LayerDrawConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    if (!dst) { // unreachable in legacy (u_MapTypesMainList.pas:503-504 always passes a writer)
        return;
    }
    if (layerZOrder_ != layerZOrderDef_) {
        dst->writeInt(u"LayerZOrder", layerZOrder_);
    } else {
        dst->deleteValue(u"LayerZOrder");
    }
}

} // namespace cg::maptype
