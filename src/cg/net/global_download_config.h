// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_GlobalDownloadConfig.pas, u_GlobalDownloadConfig.pas:33-154. docs/specs/net.md §2.16,
// docs/specs/maptype.md §11.2 P-8 (T-0118).
//
// Three app-level flags that steer the region downloader and the tile result saver; this unit only
// holds and persists them. Persisted as GoNextTile (default true), SessionLastSuccess (true) and
// SaveTNE (false). The section they live in is chosen by the owner (u_GlobalConfig.pas:319-320).

#include "cg/core/config_element.h"

namespace cg::net {

class IGlobalDownloadConfig : public cg::core::ConfigElement {
public:
    [[nodiscard]] virtual bool isGoNextTileIfDownloadError() const = 0;
    virtual void setGoNextTileIfDownloadError(bool value) = 0;

    [[nodiscard]] virtual bool isUseSessionLastSuccess() const = 0;
    virtual void setUseSessionLastSuccess(bool value) = 0;

    [[nodiscard]] virtual bool isSaveTileNotExists() const = 0;
    virtual void setSaveTileNotExists(bool value) = 0;
};

class GlobalDownloadConfig final : public IGlobalDownloadConfig {
public:
    [[nodiscard]] bool isGoNextTileIfDownloadError() const override;
    void setGoNextTileIfDownloadError(bool value) override;

    [[nodiscard]] bool isUseSessionLastSuccess() const override;
    void setUseSessionLastSuccess(bool value) override;

    [[nodiscard]] bool isSaveTileNotExists() const override;
    void setSaveTileNotExists(bool value) override;

protected:
    void doReadConfig(const cg::core::IConfigProviderPtr& src) override;
    void doWriteConfig(const cg::core::IConfigWriterPtr& dst) const override;

private:
    void setFlag(bool& field, bool value);

    bool goNextTileIfDownloadError_ = true;
    bool useSessionLastSuccess_ = true;
    bool saveTileNotExists_ = false;
};

} // namespace cg::net
