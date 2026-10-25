// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_type_gui_config.h"

#include "cg/core/string_config_data_element_with_language.h"

#include <climits>
#include <utility>

namespace cg::maptype {

using namespace Qt::StringLiterals;

namespace {

int sortIndexFromInt(int value) {
    return value < 0 ? INT_MAX : value;
}

} // namespace

MapTypeGuiConfig::MapTypeGuiConfig(const std::shared_ptr<core::ILanguageManager>& languageManager,
                                   std::shared_ptr<const IZmpInfoGui> defConfig)
    : defConfig_(std::move(defConfig)), name_(std::make_shared<core::StringConfigDataElementWithLanguage>(
                                            languageManager, true, u"MapName"_s, true, defConfig_->name())),
      sortIndex_(sortIndexFromInt(defConfig_->sortIndex())), hotKey_(defConfig_->hotKey()),
      separator_(defConfig_->separator()),
      parentSubMenu_(std::make_shared<core::StringConfigDataElementWithLanguage>(
          languageManager, true, u"ParentSubMenu"_s, false, defConfig_->parentSubMenu())),
      enabled_(defConfig_->enabled()),
      infoUrl_(std::make_shared<core::StringConfigDataElementWithLanguage>(
          languageManager, false, u"InfoUrl"_s, false, defConfig_->infoUrl())) {
    // Legacy Add(item, strategy) = all four child flags on, also for infoUrl (strategy nil: its
    // useStore is false, so reading and writing it is a no-op).
    const ChildFlags all{true, true, true, true};
    addChild(name_, core::SaveLoadSlot{}, all);
    addChild(parentSubMenu_, core::SaveLoadSlot{}, all);
    addChild(infoUrl_, core::SaveLoadSlot{}, all);
    doBeforeChangeNotify(); // seed the snapshot (legacy: AfterConstruction); no setChanged()
}

MapTypeGuiConfig::~MapTypeGuiConfig() = default;

int MapTypeGuiConfig::sortIndex() const {
    const auto guard = readLock();
    return sortIndex_;
}

void MapTypeGuiConfig::setSortIndex(int v) {
    const auto guard = writeLock();
    const int newIndex = sortIndexFromInt(v);
    if (sortIndex_ != newIndex) {
        sortIndex_ = newIndex;
        setChanged();
    }
}

quint16 MapTypeGuiConfig::hotKey() const {
    const auto guard = readLock();
    return hotKey_;
}

void MapTypeGuiConfig::setHotKey(quint16 v) {
    const auto guard = writeLock();
    if (hotKey_ != v) {
        hotKey_ = v;
        setChanged();
    }
}

bool MapTypeGuiConfig::separator() const {
    const auto guard = readLock();
    return separator_;
}

void MapTypeGuiConfig::setSeparator(bool v) {
    const auto guard = writeLock();
    if (separator_ != v) {
        separator_ = v;
        setChanged();
    }
}

bool MapTypeGuiConfig::enabled() const {
    const auto guard = readLock();
    return enabled_;
}

void MapTypeGuiConfig::setEnabled(bool v) {
    const auto guard = writeLock();
    if (enabled_ != v) {
        enabled_ = v;
        setChanged();
    }
}

MapTypeGuiConfigStaticPtr MapTypeGuiConfig::getStatic() const {
    return snapshot_.get();
}

// The children first (MapName, ParentSubMenu), then the own keys. A non-null provider always ends
// with setChanged(), even when nothing changed. L-153: the Integer from the file is stored in a
// Word, so only its low 16 bits survive.
void MapTypeGuiConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    ConfigElementComplex::doReadConfig(src);
    if (!src) {
        return;
    }
    hotKey_ = static_cast<quint16>(src->readInt(u"HotKey", hotKey_));
    separator_ = src->readBool(u"separator", separator_);
    enabled_ = src->readBool(u"Enabled", enabled_);
    sortIndex_ = sortIndexFromInt(src->readInt(u"pnum", sortIndex_));
    setChanged();
}

// Differs-rule against the raw ZMP values, so a sortIndex of INT_MAX counts as different from a
// ZMP pnum of -1 (L-151). Order: children, Separator, Enabled, pnum, HotKey.
void MapTypeGuiConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    ConfigElementComplex::doWriteConfig(dst);
    if (!dst) { // defensive; unreachable in legacy (u_MapTypesMainList.pas:503-504 always passes a writer)
        return;
    }
    if (separator_ != defConfig_->separator()) {
        dst->writeBool(u"Separator", separator_);
    } else {
        dst->deleteValue(u"Separator");
    }
    if (enabled_ != defConfig_->enabled()) {
        dst->writeBool(u"Enabled", enabled_);
    } else {
        dst->deleteValue(u"Enabled");
    }
    if (sortIndex_ != defConfig_->sortIndex()) {
        dst->writeInt(u"pnum", sortIndex_);
    } else {
        dst->deleteValue(u"pnum");
    }
    if (hotKey_ != defConfig_->hotKey()) {
        dst->writeInt(u"HotKey", hotKey_);
    } else {
        dst->deleteValue(u"HotKey");
    }
}

void MapTypeGuiConfig::doBeforeChangeNotify() {
    snapshot_.set(std::make_shared<const MapTypeGuiConfigStatic>(
        MapTypeGuiConfigStatic{name_->value(), sortIndex_, hotKey_, separator_, parentSubMenu_->value(),
                               enabled_, infoUrl_->value()}));
}

} // namespace cg::maptype
