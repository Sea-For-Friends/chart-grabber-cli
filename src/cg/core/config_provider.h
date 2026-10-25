// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ConfigDataProvider.pas
//
// The read-only settings tree every domain composes: sections of an ini file, folders of maps,
// Qt resources and layered defaults all answer the same five-method contract. Typed reads are
// non-virtual helpers over value()/binary() so the thirteen legacy providers do not each
// reimplement ReadInteger/ReadBool/ReadFloat/... themselves (idioms.md 7).

#include "cg/core/result.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QTime>
#include <QUuid>
#include <QVariant>
#include <QtGlobal>

#include <memory>

namespace cg::core {

class IConfigProvider;
using IConfigProviderPtr = std::shared_ptr<const IConfigProvider>;

// FEAT-UI-199
class IConfigProvider {
public:
    virtual ~IConfigProvider() = default;

    // nullptr when the sub-item does not exist (ports GetSubItem returning nil).
    [[nodiscard]] virtual IConfigProviderPtr subItem(QStringView id) const = 0;

    // An invalid QVariant means "absent". A present-but-empty value is a valid QString("").
    [[nodiscard]] virtual QVariant value(QStringView id) const = 0;
    [[nodiscard]] virtual QByteArray binary(QStringView id) const = 0;
    [[nodiscard]] virtual QStringList subItemNames() const = 0;
    [[nodiscard]] virtual QStringList valueNames() const = 0;

    // Non-virtual typed helpers over value(); apply the same Delphi conversion rules as
    // IniFile's own typed accessors (cg/core/ini_value_codec.h), so every provider gets
    // identical defaulting for free.
    [[nodiscard]] QString readString(QStringView id, const QString& def = QString()) const;
    [[nodiscard]] qint32 readInt(QStringView id, qint32 def) const;
    [[nodiscard]] bool readBool(QStringView id, bool def) const;
    [[nodiscard]] double readFloat(QStringView id, double def) const;
    [[nodiscard]] QDate readDate(QStringView id, QDate def) const;
    [[nodiscard]] QTime readTime(QStringView id, QTime def) const;
    [[nodiscard]] QDateTime readDateTime(QStringView id, const QDateTime& def) const;
    [[nodiscard]] QUuid readUuid(QStringView id, const QUuid& def) const; // ports ReadGUID
    [[nodiscard]] QStringList readStringList(QStringView id) const;       // ports ReadStringListStatic
};

} // namespace cg::core
