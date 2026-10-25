// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileDownloadSubsystemState.pas (docs/specs/maptype.md 2.13.1)
//
// The state of a whole map's download subsystem: the first reason, in a fixed priority, why a map cannot
// download -- the ZMP, the map's abilities, the request builder, the result saver. Legacy derives from
// TConfigDataElementComplexWithStaticBase: it listens to its three sources only when the ZMP allows
// downloading, builds its static before every change notification and notifies on ANY change of a source
// (no comparison with the old value).
//
// Must be owned by a std::shared_ptr and subscribe() called once after construction (the same contract as
// net::DownloadQueue): the source slots hold only a weak reference, so a notification racing the destructor
// is safe -- `core::Subscription::reset` does not wait for a callback in flight (core/notifier.cpp).

#include "cg/core/sync.h"
#include "cg/maptype/map_abilities_config.h"
#include "cg/maptype/tile_downloader_state.h"

#include <memory>
#include <vector>

namespace cg::maptype {

class TileDownloadSubsystemState final : public ITileDownloaderStateChangeable,
                                         public std::enable_shared_from_this<TileDownloadSubsystemState> {
public:
    // The three sources are read only when `zmpDownloadEnabled` (:68-77); a disabled-by-ZMP state may be
    // built with all three null. An enabled one with a null source is a port precondition (legacy would
    // fail on the first CreateStatic): std::invalid_argument.
    TileDownloadSubsystemState(bool zmpDownloadEnabled,
                               std::shared_ptr<ITileDownloaderStateChangeable> requestBuilderState,
                               std::shared_ptr<ITileDownloaderStateChangeable> saverState,
                               std::shared_ptr<MapAbilitiesConfig> mapAbilitiesConfig);

    // Ports Add(FSaverState) / Add(FRequestBuilderState) / Add(FMapAbilitiesConfig) (:68-77): listens to the
    // `changed()` of the three sources. Once, after make_shared. Does nothing for a disabled-by-ZMP state.
    void subscribe();

    [[nodiscard]] TileDownloaderStateStaticPtr getStatic() const override;

protected:
    void doBeforeChangeNotify() override; // CreateStatic, before the subscribers see the change

private:
    void onSourceChange();
    [[nodiscard]] TileDownloaderStateStaticPtr createStatic() const; // :80-108

    bool zmpDownloadEnabled_;
    std::shared_ptr<ITileDownloaderStateChangeable> requestBuilderState_;
    std::shared_ptr<ITileDownloaderStateChangeable> saverState_;
    std::shared_ptr<MapAbilitiesConfig> mapAbilitiesConfig_;

    // Serialises "read the sources, publish the static": whoever publishes last has read the newest sources.
    mutable core::Mutex mutex_;
    TileDownloaderStateStaticPtr static_;

    // Declared last: torn down first (destructor-order rule of net::DownloadQueue).
    std::vector<core::Subscription> subscriptions_;
};

} // namespace cg::maptype
