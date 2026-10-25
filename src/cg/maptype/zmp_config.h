// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ZmpConfig.pas, u_ZmpConfig.pas
//
// The defaults every ZMP is loaded with (spec maptype.md §2.6.9), persisted in the main INI
// section [ZmpDefaultParams]. ZmpInfo reads it once at construction.

#include "cg/core/config_element.h"

#include <QtGlobal>

namespace cg::maptype {

// FEAT-NET-002
class ZmpConfig final : public core::ConfigElement {
public:
    ZmpConfig();
    ~ZmpConfig() override;

    [[nodiscard]] quint32 maxConnectToServerCount() const;
    void setMaxConnectToServerCount(quint32 v);

    [[nodiscard]] bool useMemCache() const;
    void setUseMemCache(bool v);

    [[nodiscard]] qint32 memCacheCapacity() const;
    void setMemCacheCapacity(qint32 v);

    [[nodiscard]] quint32 memCacheTtl() const; // ms
    void setMemCacheTtl(quint32 v);

    [[nodiscard]] qint32 memCacheClearStrategy() const;
    void setMemCacheClearStrategy(qint32 v);

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;

private:
    quint32 maxConnectToServerCount_ = 4;
    bool useMemCache_ = true;
    qint32 memCacheCapacity_ = 100;
    quint32 memCacheTtl_ = 60000;
    qint32 memCacheClearStrategy_ = 1; // csByYongest
};

} // namespace cg::maptype
