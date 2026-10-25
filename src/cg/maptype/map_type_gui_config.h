// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapTypeGUIConfig.pas (IMapTypeGUIConfigStatic, IMapTypeGUIConfig; the proxy interface
//        IMapTypeGuiConfigProxy belongs to map_type_gui_config_proxy.h), u_MapTypeGUIConfig.pas,
//        u_MapTypeGUIConfigStatic.pas
//
// The per-map menu/GUI settings (spec maptype.md section 2.10). Three localized string children
// (MapName, ParentSubMenu, InfoUrl) plus own fields sortIndex (key pnum), hotKey, separator and
// enabled, read from and written to the map's Maps.ini section against the ZMP defaults.
// The legacy static carries no bitmaps (the spec's "check the unit"): the icons stay in
// IZmpInfoGui.

#include "cg/core/config_element_complex.h"
#include "cg/core/language_manager.h"
#include "cg/core/snapshot.h"
#include "cg/core/string_config_data_element.h"
#include "cg/maptype/zmp_info.h"

#include <QString>
#include <QtGlobal>

#include <memory>

namespace cg::maptype {

struct MapTypeGuiConfigStatic { // u_MapTypeGUIConfigStatic.pas
    QString name;               // in the current language
    int sortIndex = 0;
    quint16 hotKey = 0; // Delphi TShortCut (Word)
    bool separator = false;
    QString parentSubMenu;
    bool enabled = true;
    QString infoUrl;
};
using MapTypeGuiConfigStaticPtr = std::shared_ptr<const MapTypeGuiConfigStatic>;

// FEAT-NET-030
class MapTypeGuiConfig final : public core::ConfigElementComplex {
public:
    MapTypeGuiConfig(const std::shared_ptr<core::ILanguageManager>& languageManager,
                     std::shared_ptr<const IZmpInfoGui> defConfig);
    ~MapTypeGuiConfig() override;

    [[nodiscard]] std::shared_ptr<core::IStringConfigDataElement> name() const { return name_; }
    [[nodiscard]] std::shared_ptr<core::IStringConfigDataElement> parentSubMenu() const {
        return parentSubMenu_;
    }
    [[nodiscard]] std::shared_ptr<core::IStringConfigDataElement> infoUrl() const { return infoUrl_; }

    // A negative index means "no index" and is stored as INT_MAX (SortIndexFromInt).
    [[nodiscard]] int sortIndex() const;
    void setSortIndex(int v);
    [[nodiscard]] quint16 hotKey() const;
    void setHotKey(quint16 v);
    [[nodiscard]] bool separator() const;
    void setSeparator(bool v);
    [[nodiscard]] bool enabled() const;
    void setEnabled(bool v);

    [[nodiscard]] MapTypeGuiConfigStaticPtr getStatic() const;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;
    void doBeforeChangeNotify() override; // rebuilds the snapshot

private:
    std::shared_ptr<const IZmpInfoGui> defConfig_;
    std::shared_ptr<core::IStringConfigDataElement> name_;
    int sortIndex_;
    quint16 hotKey_;
    bool separator_;
    std::shared_ptr<core::IStringConfigDataElement> parentSubMenu_;
    bool enabled_;
    std::shared_ptr<core::IStringConfigDataElement> infoUrl_;
    core::Snapshot<MapTypeGuiConfigStatic> snapshot_;
};

} // namespace cg::maptype
