// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ActiveMapsConfig.pas (IActiveLayersConfig), u_ActiveLayersConfig.pas (TActiveLayersConfig).
//
// The set of layer maps switched on, persisted as `Layer0` ... `Layer<n-1>` (braced GUIDs in set
// order). A key belongs to this element when its first five characters are `Layer`, any case and
// any suffix. The write path deletes every such key first, so a foreign key with that prefix is
// deleted on save (legacy behaviour, reproduced; see legacy-defects.md, the L row of T-0145).

#include "cg/core/config_element.h"
#include "cg/core/guid_list_static.h"

#include <QUuid>

namespace cg::maptype {

// Interface and class share this header (developer brief 6a; as core/thread_config.h does).
class IActiveLayersConfig : public core::ConfigElement {
public:
    [[nodiscard]] virtual core::IGuidSetStaticPtr layerGuids() const = 0; // null when empty
    virtual void setLayerGuids(const core::IGuidSetStaticPtr& value) = 0;

    virtual void invertLayerSelectionByGuid(const QUuid& mapGuid) = 0;
    virtual void selectLayerByGuid(const QUuid& mapGuid) = 0;
    virtual void unSelectLayerByGuid(const QUuid& mapGuid) = 0;
};

class ActiveLayersConfig final : public IActiveLayersConfig {
public:
    [[nodiscard]] core::IGuidSetStaticPtr layerGuids() const override;
    void setLayerGuids(const core::IGuidSetStaticPtr& value) override;

    void invertLayerSelectionByGuid(const QUuid& mapGuid) override;
    void selectLayerByGuid(const QUuid& mapGuid) override;
    void unSelectLayerByGuid(const QUuid& mapGuid) override;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;

private:
    void selectLayerByGuidLocked(const QUuid& mapGuid); // the "...Locked()" split of core.md 2.16

    core::IGuidSetStaticPtr layerGuids_;
};

} // namespace cg::maptype
