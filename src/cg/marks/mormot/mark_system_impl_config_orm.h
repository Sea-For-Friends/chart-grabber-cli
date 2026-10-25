// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: mORMot/i_MarkSystemImplConfigORM.pas, mORMot/u_MarkSystemImplConfigORM.pas

#include "cg/marks/mark_system_impl_config_base.h"

#include <QString>
#include <QUuid>
#include <QtGlobal>

namespace cg::marks {

// IMarkSystemImplConfigORM's interface GUID (i_MarkSystemImplConfigORM.pas:31).
inline constexpr QUuid iidMarkSystemImplConfigORM{0x4922B216, 0x1197, 0x4BB0, 0x93, 0xE1, 0xCC,
                                                  0x2C,       0xC1,   0xB6,   0x87, 0x87};

class IMarkSystemImplConfigORM : public virtual IMarkSystemImplConfigStatic {
public:
    [[nodiscard]] virtual QString userName() const = 0;
    [[nodiscard]] virtual QString password() const = 0;
    [[nodiscard]] virtual QString passwordPlain() const = 0;
    [[nodiscard]] virtual quint32 cacheSizeMb() const = 0;
    [[nodiscard]] virtual QString forcedSchemaName() const = 0;
};

class MarkSystemImplConfigORM final : public MarkSystemImplConfigBase, public IMarkSystemImplConfigORM {
public:
    // u_MarkSystemImplConfigORM.pas:80-84: a non-empty passwordPlain wins over password.
    MarkSystemImplConfigORM(QString fileName, bool isReadOnly, const QString& userName,
                            const QString& passwordPlain, const QString& password, quint32 cacheSizeMb = 100,
                            const QString& forcedSchemaName = {});

    [[nodiscard]] QString userName() const override { return userName_; }
    [[nodiscard]] QString password() const override { return password_; }
    [[nodiscard]] QString passwordPlain() const override { return password_; }
    [[nodiscard]] quint32 cacheSizeMb() const override { return cacheSizeMb_; }
    [[nodiscard]] QString forcedSchemaName() const override { return forcedSchemaName_; }

private:
    QString userName_;
    QString password_; // the INI value verbatim: legacy holds mORMot's obfuscated Base64
                       // (mormot.crypt.secure.pas:7072-7075, :7159-7175), not decoded here; a plain-text ctor
                       // argument is kept as-is (D-78)
    quint32 cacheSizeMb_;
    QString forcedSchemaName_;
};

} // namespace cg::marks
