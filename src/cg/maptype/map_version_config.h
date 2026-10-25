// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapVersionConfig.pas, u_MapVersionConfig.pas
//
// A config element holding one version string, key `Version` (spec maptype.md section 2.9).
// Legacy never instantiates it (only TMapVersionChangeable refers to it, and that is unused too);
// the live element is MapVersionRequestConfig.

#include "cg/core/config_element.h"

#include <QString>

namespace cg::maptype {

// FEAT-NET-002
class MapVersionConfig final : public core::ConfigElement {
public:
    explicit MapVersionConfig(QString version);
    ~MapVersionConfig() override;

    [[nodiscard]] QString version() const;
    void setVersion(const QString& v);

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;

private:
    void setVersionLocked(const QString& v); // doReadConfig runs inside the write lock

    QString version_;
    QString versionDef_;
};

} // namespace cg::maptype
