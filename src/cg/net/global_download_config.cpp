// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/net/global_download_config.h"

#include "cg/core/config_provider.h"
#include "cg/core/config_writer.h"

namespace cg::net {

bool GlobalDownloadConfig::isGoNextTileIfDownloadError() const {
    const ReadGuard guard = readLock();
    return goNextTileIfDownloadError_;
}

void GlobalDownloadConfig::setGoNextTileIfDownloadError(bool value) {
    setFlag(goNextTileIfDownloadError_, value);
}

bool GlobalDownloadConfig::isUseSessionLastSuccess() const {
    const ReadGuard guard = readLock();
    return useSessionLastSuccess_;
}

void GlobalDownloadConfig::setUseSessionLastSuccess(bool value) {
    setFlag(useSessionLastSuccess_, value);
}

bool GlobalDownloadConfig::isSaveTileNotExists() const {
    const ReadGuard guard = readLock();
    return saveTileNotExists_;
}

void GlobalDownloadConfig::setSaveTileNotExists(bool value) {
    setFlag(saveTileNotExists_, value);
}

// Legacy calls SetChanged unconditionally on a non-nil provider (u_GlobalDownloadConfig.pas:73),
// so a read notifies even when every value is the same as before.
void GlobalDownloadConfig::doReadConfig(const cg::core::IConfigProviderPtr& src) {
    if (src) {
        goNextTileIfDownloadError_ = src->readBool(u"GoNextTile", goNextTileIfDownloadError_);
        useSessionLastSuccess_ = src->readBool(u"SessionLastSuccess", useSessionLastSuccess_);
        saveTileNotExists_ = src->readBool(u"SaveTNE", saveTileNotExists_);
        setChanged();
    }
}

void GlobalDownloadConfig::doWriteConfig(const cg::core::IConfigWriterPtr& dst) const {
    dst->writeBool(u"GoNextTile", goNextTileIfDownloadError_);
    dst->writeBool(u"SessionLastSuccess", useSessionLastSuccess_);
    dst->writeBool(u"SaveTNE", saveTileNotExists_);
}

void GlobalDownloadConfig::setFlag(bool& field, bool value) {
    const WriteGuard guard = writeLock();
    if (field != value) {
        field = value;
        setChanged();
    }
}

} // namespace cg::net
