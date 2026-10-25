// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapTypeGUIConfigList.pas (IMapTypeGUIConfigList, TMapTypeGUIConfigListSortOrder) and
//        u_MapTypeGUIConfigList.pas (TMapTypeGUIConfigList). The interface is merged into the class
//        (the precedent of map_type_gui_config.h).
//
// The menu order of the maps of one map set: by pnum, by name (submenu-aware) or in set order,
// chosen by the [MapsList] SortOrder key. Both results are published as immutable snapshots and
// rebuilt when a map's GUI config changes, when the language changes (one list notification per
// switch) and when the sort order changes. The GUI configs are listened to, never read or written
// through this list (legacy Add(item, nil)).

#include "cg/core/config_element_complex.h"
#include "cg/core/language_manager.h"
#include "cg/core/snapshot.h"
#include "cg/maptype/map_type_hot_key_list_static.h"
#include "cg/maptype/map_type_set.h"

#include <QUuid>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace cg::maptype {

// i_MapTypeGUIConfigList.pas:32 (the stored value may be any int read from the INI; see doReadConfig)
// NOLINTNEXTLINE(performance-enum-size): the INI can hold any integer (DV-11), so the base type stays int
enum class MapTypeGuiConfigListSortOrder : int { ByMapNumber = 0, ByMapName = 1, ByZmpName = 2 };

// Legacy IGUIDListStatic (u_GUIDListStatic.pas): an ordered, not sorted, list of ZMP GUIDs as an
// immutable vector. Null when no map is listed (legacy nil).
using MapGuidListPtr = std::shared_ptr<const std::vector<QUuid>>;

class MapTypeGuiConfigList final : public core::ConfigElementComplex {
public:
    // A null map set gives an empty list (legacy: access violation at the constructor's Count
    // read). A null map, GUI config or language manager throws std::logic_error. A listed map
    // with a null ZMP throws std::logic_error from the constructor (legacy: access violation at
    // the GUID read).
    MapTypeGuiConfigList(std::shared_ptr<core::ILanguageManager> languageManager, IMapTypeSetPtr mapsSet);
    ~MapTypeGuiConfigList() override;

    [[nodiscard]] MapTypeGuiConfigListSortOrder sortOrder() const;
    void setSortOrder(MapTypeGuiConfigListSortOrder value);

    [[nodiscard]] MapGuidListPtr orderedMapGuidList() const;
    [[nodiscard]] std::shared_ptr<const MapTypeHotKeyListStatic> hotKeyList() const;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;
    // Rebuilds both snapshots under rebuildMutex_, so that the last publish reflects the last change.
    // Runs under the write lock (writer thread) or without it (the last BatchToken of a language switch
    // or a child change); the lock order is in the .cpp. It throws std::logic_error only if a listed
    // map has a null ZMP, which the constructor rules out for the maps of the set.
    void doBeforeChangeNotify() override;

private:
    [[nodiscard]] MapGuidListPtr buildOrderedList() const;

    std::shared_ptr<core::ILanguageManager> languageManager_;
    IMapTypeSetPtr mapsSet_;
    std::atomic<MapTypeGuiConfigListSortOrder> sortOrder_{MapTypeGuiConfigListSortOrder::ByMapName};
    core::Snapshot<std::vector<QUuid>> orderedMapGuids_;
    core::Snapshot<MapTypeHotKeyListStatic> hotKeys_;
    core::Subscription languageBeforeSub_;
    core::Subscription languageAfterSub_;
    // Open between the language manager's before and after notifications: holding it keeps this
    // list's own notification suppressed, so the per-map name changes of one switch collapse into
    // one list notification (legacy StopNotify / StartNotify).
    std::optional<core::ConfigElement::BatchToken> languageBatch_;
    // Serialises every rebuild (build + publish). Lock order on every path: the config element's own
    // lock (writeLock() in setSortOrder and readConfig; none on the BatchToken path) -> rebuildMutex_ ->
    // a child's read lock taken by the rebuild. No path takes them in another order.
    std::mutex rebuildMutex_;
};

} // namespace cg::maptype
