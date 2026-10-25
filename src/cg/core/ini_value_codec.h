// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing (internal helper).
//
// The Delphi TCustomIniFile / StrToIntDef / StrToFloat conversion quirks (spec core.md 2.12.2,
// Q13/Q14/Q16/Q17/Q18/Q19/Q20) shared between IniFile's own typed accessors (ini_file.cpp) and
// IConfigProvider's typed helpers (config_provider.cpp): a value converts identically whether it
// is read straight off an IniFile or through a provider tree built on top of one, so the
// conversion rules live in exactly one place.

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringView>
#include <QTime>
#include <QtGlobal>

namespace cg::core::valuecodec {

[[nodiscard]] qint32 parseInt(const QString& text, qint32 def);
[[nodiscard]] double parseFloat(const QString& text, double def);
[[nodiscard]] QDate parseDate(const QString& text, QDate def);
[[nodiscard]] QTime parseTime(const QString& text, QTime def);
[[nodiscard]] QDateTime parseDateTime(const QString& text, const QDateTime& def);

[[nodiscard]] QString formatFloat(double value);
[[nodiscard]] QString formatDate(QDate value);
[[nodiscard]] QString formatTime(QTime value);
[[nodiscard]] QString formatDateTime(const QDateTime& value);

[[nodiscard]] bool isHexDigits(QStringView text);

} // namespace cg::core::valuecodec
