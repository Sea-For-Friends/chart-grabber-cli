// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ConfigDataProviderByFolder.pas / u_ConfigDataProviderByZip.pas (the repeated extension tests)
//
// Shared by ConfigProviderFolder and ConfigProviderZip: the extension tests legacy repeats in
// u_ConfigDataProviderByFolder.pas and u_ConfigDataProviderByZip.pas (UpperCase(ExtractFileExt(x)) =
// '.INI' / '.TXT' / '.HTML'). The argument is a QFileInfo::suffix() (no dot).

#include <QString>

namespace cg::core {

[[nodiscard]] inline bool isIniOrTxtExt(const QString& ext) {
    return ext.compare(QStringLiteral("ini"), Qt::CaseInsensitive) == 0 ||
           ext.compare(QStringLiteral("txt"), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] inline bool isIniTxtOrHtmlExt(const QString& ext) {
    return isIniOrTxtExt(ext) || ext.compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0;
}

} // namespace cg::core
