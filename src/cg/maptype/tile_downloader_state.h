// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileDownloaderState.pas, u_TileDownloaderStateStatic.pas (the static folds into the
//        struct below), u_TileDownloaderStateInternal.pas
//
// Enabled flag plus the reason it was disabled; the first disable wins until enable() runs.

#include "cg/core/changeable.h"
#include "cg/core/sync.h"

#include <QString>

#include <memory>

namespace cg::maptype {

struct TileDownloaderStateStatic {
    bool enabled = true;
    QString disableReason;
};
using TileDownloaderStateStaticPtr = std::shared_ptr<const TileDownloaderStateStatic>;

class ITileDownloaderStateChangeable : public core::Changeable {
public:
    [[nodiscard]] virtual TileDownloaderStateStaticPtr getStatic() const = 0;
};

// Also carries legacy ITileDownloaderStateInternal (u_TileDownloaderStateInternal.pas:32-44, declared in the
// u_ unit, one implementer): an upstream change to that interface is traced here.
class TileDownloaderStateInternal final : public ITileDownloaderStateChangeable {
public:
    TileDownloaderStateInternal();

    // Only when enabled: stores the reason and notifies (after unlock). While disabled the
    // call is ignored, the first reason is kept.
    void disable(const QString& reason);
    // Only when disabled: clears the reason and notifies (after unlock).
    void enable();

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] QString disableReason() const; // empty while enabled
    [[nodiscard]] TileDownloaderStateStaticPtr getStatic() const override;

private:
    [[nodiscard]] TileDownloaderStateStaticPtr createStatic() const; // caller holds the lock

    mutable core::Mutex mutex_;
    bool enabled_ = true;
    QString reason_;
    TileDownloaderStateStaticPtr static_;
};

} // namespace cg::maptype
