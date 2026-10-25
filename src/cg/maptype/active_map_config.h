// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ActiveMapsConfig.pas (IActiveMapConfig), u_ActiveMapConfig.pas (TActiveMapConfig).
//
// The map GUID persisted as `Map` (braced, upper-case text). A zero GUID is applied only when the
// element allows it; otherwise the current value stays. Legacy's Assert calls (constructor and
// setter) are off in shipped builds (SASPlanet.dproj:92) and are not reproduced: a zero GUID that
// is not allowed is ignored without any error.

#include "cg/core/config_element.h"

#include <QUuid>

namespace cg::maptype {

// Interface and class share this header (developer brief 6a; as core/thread_config.h does).
class IActiveMapConfig : public core::ConfigElement {
public:
    [[nodiscard]] virtual QUuid mainMapGuid() const = 0;
    virtual void setMainMapGuid(const QUuid& mapGuid) = 0;
};

class ActiveMapConfig final : public IActiveMapConfig {
public:
    ActiveMapConfig(bool isAllowZeroGuid, const QUuid& initialMainMapGuid);

    [[nodiscard]] QUuid mainMapGuid() const override;
    void setMainMapGuid(const QUuid& mapGuid) override;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;

private:
    void setMainMapGuidLocked(const QUuid& mapGuid); // the "...Locked()" split of core.md 2.16

    bool isAllowZeroGuid_;
    QUuid mainMapGuid_;
};

} // namespace cg::maptype
