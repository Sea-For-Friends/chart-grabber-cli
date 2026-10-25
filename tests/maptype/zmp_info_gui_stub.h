// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Shared by the map-type GUI config and hot-key tests: the GUI half of a ZMP (IZmpInfoGui) with
// settable hot key and enabled flag, the other fields fixed to sky_chandracomp.zmp.

#include "cg/core/string_by_language.h"
#include "cg/maptype/zmp_info.h"

#include <QImage>
#include <QStringList>
#include <QtGlobal>

#include <memory>

namespace cg::maptype::test {

// The GUI half of tests/fixtures/zmp/sas.maps/_Space/sky_chandracomp.zmp/params.txt
// (pnum empty -> -1, no hotkey, no separator, enabled), languages en es fa fr ru.
class ZmpGuiStub final : public IZmpInfoGui {
public:
    int pnum = -1;
    quint16 key = 0;
    bool sep = false;
    bool on = true;
    // Name and submenu for languages 0..3 (the Russian strings below are fixed); the defaults are
    // the sky_chandracomp.zmp values.
    QString nameEn = QStringLiteral(u"chandracomp (Google)");
    QString subMenuEn = QStringLiteral(u"Cache view\\Space");
    [[nodiscard]] cg::core::IStringByLanguagePtr name() const override {
        return std::make_shared<cg::core::StringByLanguageWithStaticList>(
            QStringList{nameEn, nameEn, nameEn, nameEn, QStringLiteral(u"Звёзды Chandracomp (Google)")});
    }
    [[nodiscard]] int sortIndex() const override { return pnum; }
    [[nodiscard]] quint16 hotKey() const override { return key; }
    [[nodiscard]] bool separator() const override { return sep; }
    [[nodiscard]] cg::core::IStringByLanguagePtr parentSubMenu() const override {
        return std::make_shared<cg::core::StringByLanguageWithStaticList>(QStringList{
            subMenuEn, subMenuEn, subMenuEn, subMenuEn, QStringLiteral(u"Для просмотра кэша\\Космос")});
    }
    [[nodiscard]] bool enabled() const override { return on; }
    [[nodiscard]] cg::core::IStringByLanguagePtr infoUrl() const override {
        return std::make_shared<cg::core::StringByLanguageWithStaticList>(
            QStringList{QStringLiteral(u"http://example/info")});
    }
    [[nodiscard]] QImage bmp18() const override { return {}; }
    [[nodiscard]] QImage bmp24() const override { return {}; }
};

} // namespace cg::maptype::test
