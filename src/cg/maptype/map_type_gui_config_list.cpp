// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_type_gui_config_list.h"

#include "cg/core/explorer_sort.h"
#include "cg/maptype/log_maptype.h"
#include "cg/maptype/map_type.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/zmp_info.h"

#include <algorithm>
#include <functional>
#include <numeric>
#include <stdexcept>

namespace cg::maptype {

namespace {

// Legacy #01 and #02 of the submenu key (u_MapTypeGUIConfigList.pas:172-181).
constexpr char16_t kSubMenuFolder = u'\x01';
constexpr char16_t kSubMenuFolderEnd = u'\x02';

// The key of ByMapName: the submenu as a folder path (folders sort before files), then the name.
// u_MapTypeGUIConfigList.pas:164-183.
QString nameKey(const IMapType& map) {
    const auto gui = map.guiConfig();
    QString subMenu = gui->parentSubMenu()->value();
    if (!subMenu.isEmpty()) {
        subMenu.replace(QChar(u'\\'), QChar(kSubMenuFolder));
        if (subMenu.front() != QChar(kSubMenuFolder)) {
            subMenu.append(QChar(kSubMenuFolder));
        }
        if (subMenu.back() == QChar(kSubMenuFolder)) {
            subMenu.back() = QChar(kSubMenuFolderEnd);
        } else {
            subMenu.append(QChar(kSubMenuFolderEnd));
        }
    }
    return subMenu + gui->name()->value();
}

// Ascending and stable, as the legacy StableSortInterfaceListBy{Integer,String}Measure
// (u_SortFunc.pas:557, :672). The result of a stable sort is fixed by a strict weak order, so
// std::stable_sort reproduces it (the transitivity of core::compareStringOrdinal is assumed, not proven).
template <class Key, class Less>
std::vector<IMapTypePtr> stableOrder(const std::vector<IMapTypePtr>& maps, const std::vector<Key>& keys,
                                     Less less) {
    std::vector<std::size_t> index(maps.size());
    std::iota(index.begin(), index.end(), std::size_t{0});
    std::stable_sort(index.begin(), index.end(),
                     [&keys, &less](std::size_t a, std::size_t b) { return less(keys[a], keys[b]); });
    std::vector<IMapTypePtr> ordered;
    ordered.reserve(maps.size());
    for (const std::size_t i : index) {
        ordered.push_back(maps[i]);
    }
    return ordered;
}

// u_MapTypeGUIConfigList.pas:157-163 (ByMapNumber) and :164-186 (ByMapName).
std::vector<IMapTypePtr> byMapNumber(const std::vector<IMapTypePtr>& maps) {
    std::vector<int> numbers;
    numbers.reserve(maps.size());
    for (const IMapTypePtr& map : maps) {
        numbers.push_back(map->guiConfig()->sortIndex());
    }
    return stableOrder(maps, numbers, std::less<int>{});
}

std::vector<IMapTypePtr> byMapName(const std::vector<IMapTypePtr>& maps) {
    std::vector<QString> keys;
    keys.reserve(maps.size());
    for (const IMapTypePtr& map : maps) {
        keys.push_back(nameKey(*map));
    }
    return stableOrder(
        maps, keys, [](const QString& a, const QString& b) { return core::compareStringOrdinal(a, b) < 0; });
}

} // namespace

// u_MapTypeGUIConfigList.pas:83-108
MapTypeGuiConfigList::MapTypeGuiConfigList(std::shared_ptr<core::ILanguageManager> languageManager,
                                           IMapTypeSetPtr mapsSet)
    : languageManager_(std::move(languageManager)), mapsSet_(std::move(mapsSet)) {
    if (!languageManager_) {
        throw std::logic_error("MapTypeGuiConfigList: nil language manager");
    }
    std::vector<std::shared_ptr<MapTypeGuiConfig>> guiConfigs;
    for (int i = 0; mapsSet_ && i < mapsSet_->count(); ++i) {
        const IMapTypePtr map = mapsSet_->item(i);
        if (!map) {
            throw std::logic_error("MapTypeGuiConfigList: nil map");
        }
        auto gui = map->guiConfig();
        if (!gui) {
            throw std::logic_error("MapTypeGuiConfigList: nil GUI config");
        }
        guiConfigs.push_back(std::move(gui));
    }
    // Legacy Add(VMap.GUIConfig, nil) (:99): listened to, never read or written through the list.
    for (const auto& gui : guiConfigs) {
        addChangeable(*gui);
    }
    doBeforeChangeNotify(); // legacy builds both lists in the constructor (:95-96, :106-107)

    // Legacy :101-104. The slots only open and close the batch, so they do not throw.
    languageBeforeSub_ =
        languageManager_->beforeChanged().subscribe([this] { languageBatch_.emplace(*this); });
    languageAfterSub_ = languageManager_->afterChanged().subscribe([this] { languageBatch_.reset(); });
}

// u_MapTypeGUIConfigList.pas:110-123
MapTypeGuiConfigList::~MapTypeGuiConfigList() {
    languageBeforeSub_.reset();
    languageAfterSub_.reset();
}

// u_MapTypeGUIConfigList.pas:130-202
MapGuidListPtr MapTypeGuiConfigList::buildOrderedList() const {
    if (!mapsSet_) {
        return nullptr; // legacy: `if Assigned(FMapsSet)` (:143)
    }
    std::vector<IMapTypePtr> listed;
    listed.reserve(static_cast<std::size_t>(mapsSet_->count()));
    for (int i = 0; i < mapsSet_->count(); ++i) {
        IMapTypePtr map = mapsSet_->item(i);
        // Uninitialised proxies are not listed (:149-151).
        const auto* proxy = dynamic_cast<const IMapTypeProxy*>(map.get());
        if (proxy != nullptr && !proxy->isInitialized()) {
            continue;
        }
        listed.push_back(std::move(map));
    }
    if (listed.empty()) {
        return nullptr; // legacy Result stays nil (:194)
    }

    std::vector<IMapTypePtr> ordered;
    if (listed.size() > 1) {
        switch (sortOrder_.load()) {
        case MapTypeGuiConfigListSortOrder::ByMapNumber:
            ordered = byMapNumber(listed);
            break;
        case MapTypeGuiConfigListSortOrder::ByMapName:
            ordered = byMapName(listed);
            break;
        case MapTypeGuiConfigListSortOrder::ByZmpName:
            ordered = std::move(listed); // legacy: "Do nothing (already sorted)" (:187-189)
            break;
        default:
            // Out-of-range SortOrder (LD-16, DV-11, ruling OQ-12): by name. The warning is logged
            // once per ReadConfig in doReadConfig, not here, because this runs on every rebuild.
            ordered = byMapName(listed);
            break;
        }
    } else {
        ordered = std::move(listed);
    }

    auto guids = std::make_shared<std::vector<QUuid>>();
    guids->reserve(ordered.size());
    for (const IMapTypePtr& map : ordered) {
        const auto zmp = map->zmp();
        if (!zmp) {
            throw std::logic_error("MapTypeGuiConfigList: nil ZMP");
        }
        guids->push_back(zmp->guid());
    }
    return guids;
}

// u_MapTypeGUIConfigList.pas:204-214 (legacy rebuilds under LockWrite). The lock order is the one of the
// header comment: the config element's lock, then rebuildMutex_, then a child's lock. Without rebuildMutex_
// an older rebuild could publish last on the BatchToken path, while setSortOrder rebuilds concurrently.
void MapTypeGuiConfigList::doBeforeChangeNotify() {
    const std::lock_guard<std::mutex> guard(rebuildMutex_);
    orderedMapGuids_.set(buildOrderedList());
    hotKeys_.set(std::make_shared<const MapTypeHotKeyListStatic>(mapsSet_));
}

// u_MapTypeGUIConfigList.pas:216-225
void MapTypeGuiConfigList::doReadConfig(const core::IConfigProviderPtr& src) {
    ConfigElementComplex::doReadConfig(src);
    if (!src) {
        return;
    }
    const int value = src->readInt(u"SortOrder", static_cast<int>(sortOrder_.load()));
    if (value < 0 || value > static_cast<int>(MapTypeGuiConfigListSortOrder::ByZmpName)) {
        // The legacy text (u_MapTypeGUIConfigList.pas:191); the value is kept as read.
        qCWarning(logMapType) << "Unexpected SortOrder type:" << value;
    }
    sortOrder_.store(static_cast<MapTypeGuiConfigListSortOrder>(value));
    setChanged();
}

// u_MapTypeGUIConfigList.pas:227-235
void MapTypeGuiConfigList::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    ConfigElementComplex::doWriteConfig(dst);
    if (dst) {
        dst->writeInt(u"SortOrder", static_cast<int>(sortOrder_.load()));
    }
}

// u_MapTypeGUIConfigList.pas:247-255
MapTypeGuiConfigListSortOrder MapTypeGuiConfigList::sortOrder() const {
    const auto guard = readLock();
    return sortOrder_.load();
}

// u_MapTypeGUIConfigList.pas:267-280
void MapTypeGuiConfigList::setSortOrder(MapTypeGuiConfigListSortOrder value) {
    const auto guard = writeLock();
    if (sortOrder_.load() != value) {
        sortOrder_.store(value);
        setChanged();
    }
}

// u_MapTypeGUIConfigList.pas:237-245 (legacy reads the field without the lock: LD-15)
MapGuidListPtr MapTypeGuiConfigList::orderedMapGuidList() const {
    return orderedMapGuids_.get();
}

std::shared_ptr<const MapTypeHotKeyListStatic> MapTypeGuiConfigList::hotKeyList() const {
    return hotKeys_.get();
}

} // namespace cg::maptype
