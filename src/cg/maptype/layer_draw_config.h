// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_LayerDrawConfig.pas, u_LayerDrawConfig.pas
//
// Draw order of a layer map (spec maptype.md §2.7.2). Legacy takes the whole IZmpInfo and only
// reads its LayerZOrder; the port takes that integer, since ZmpInfo does not exist yet.

#include "cg/core/config_element.h"

#include <QtGlobal>

namespace cg::maptype {

// FEAT-NET-002
class LayerDrawConfig final : public core::ConfigElement {
public:
    explicit LayerDrawConfig(qint32 layerZOrderDefault);
    ~LayerDrawConfig() override;

    [[nodiscard]] qint32 layerZOrder() const;
    void setLayerZOrder(qint32 v);

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;

private:
    qint32 layerZOrderDef_;
    qint32 layerZOrder_;
};

} // namespace cg::maptype
