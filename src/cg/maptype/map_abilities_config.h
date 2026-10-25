// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapAbilitiesConfig.pas, u_MapAbilitiesConfig.pas, u_MapAbilitiesConfigStatic.pas
//
// Per-map "show on the mini-map" / "downloads allowed" flags (spec maptype.md §2.7.1). The
// changeable element starts from the ZMP's static value, reads its two keys from the map's
// Maps.ini section and writes back only what differs from the ZMP. The static half is rebuilt
// as an immutable snapshot before every change notification (core::Snapshot, idioms §4).

#include "cg/core/config_element.h"
#include "cg/core/snapshot.h"

#include <memory>

namespace cg::maptype {

struct MapAbilitiesConfigStatic { // u_MapAbilitiesConfigStatic.pas
    bool isShowOnSmMap = true;
    bool useDownload = true;
    [[nodiscard]] bool operator==(const MapAbilitiesConfigStatic&) const = default;
};
using MapAbilitiesConfigStaticPtr = std::shared_ptr<const MapAbilitiesConfigStatic>;

// FEAT-NET-002
class MapAbilitiesConfig final : public core::ConfigElement {
public:
    explicit MapAbilitiesConfig(MapAbilitiesConfigStaticPtr defaults);
    ~MapAbilitiesConfig() override;

    [[nodiscard]] bool isShowOnSmMap() const;
    void setIsShowOnSmMap(bool v);

    [[nodiscard]] bool useDownload() const;
    // Stores `defaults.useDownload && v`: a map the ZMP forbids to download can never be switched on.
    void setUseDownload(bool v);

    [[nodiscard]] MapAbilitiesConfigStaticPtr getStatic() const;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;
    void doBeforeChangeNotify() override; // rebuilds the snapshot

private:
    // The "...Locked()" split (core.md 2.16): doReadConfig runs inside the write lock.
    void setIsShowOnSmMapLocked(bool v);
    void setUseDownloadLocked(bool v);

    MapAbilitiesConfigStaticPtr defaults_;
    bool isShowOnSmMap_;
    bool useDownload_;
    core::Snapshot<MapAbilitiesConfigStatic> snapshot_;
};

} // namespace cg::maptype
