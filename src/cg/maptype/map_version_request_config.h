// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapVersionRequestConfig.pas, u_MapVersionRequestConfig.pas
//
// Per-map version request settings (spec maptype.md sections 2.7, 2.9): `Version` (default = the
// ZMP's version, written only when it differs) and `ShowOtherVersions` (default true, written
// always). The static half is rebuilt as an immutable snapshot before every change notification.

#include "cg/core/config_element.h"
#include "cg/core/snapshot.h"

#include <QString>

#include <memory>

namespace cg::maptype {

struct MapVersionRequestConfigStatic { // IMapVersionRequestConfigStatic
    QString version;
    bool showOtherVersions = true;
};
using MapVersionRequestConfigStaticPtr = std::shared_ptr<const MapVersionRequestConfigStatic>;

// FEAT-NET-002
class MapVersionRequestConfig final : public core::ConfigElement {
public:
    explicit MapVersionRequestConfig(QString version);
    ~MapVersionRequestConfig() override;

    [[nodiscard]] QString version() const;
    void setVersion(const QString& v);

    [[nodiscard]] bool showOtherVersions() const;
    void setShowOtherVersions(bool v);

    [[nodiscard]] MapVersionRequestConfigStaticPtr getStatic() const;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;
    void doBeforeChangeNotify() override; // rebuilds the snapshot

private:
    QString versionDef_;
    QString version_;
    bool showOtherVersions_ = true;
    core::Snapshot<MapVersionRequestConfigStatic> snapshot_;
};

} // namespace cg::maptype
