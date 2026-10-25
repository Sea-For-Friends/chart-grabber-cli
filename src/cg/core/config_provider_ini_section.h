// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ConfigDataProviderByIniFileSection.pas, u_ConfigDataWriteProviderByIniFileSection.pas
//
// One [Section] of an IniFile, and the root of a sub-tree: `subItem(id)` of section `S` is the
// section named `S + "_" + id`. A child holds a shared_ptr to its parent so the whole chain (and
// the IniFile behind it) stays alive as long as any node in the tree is reachable.

#include "cg/core/config_writer.h"
#include "cg/core/ini_file.h"

#include <memory>

namespace cg::core {

// FEAT-UI-200
class ConfigProviderIniSection final : public IConfigWriter,
                                       public std::enable_shared_from_this<ConfigProviderIniSection> {
public:
    ConfigProviderIniSection(std::shared_ptr<IniFile> file, QString section, IConfigProviderPtr parent);

    [[nodiscard]] static std::shared_ptr<ConfigProviderIniSection>
    create(std::shared_ptr<IniFile> file, QString section, IConfigProviderPtr parent);

    [[nodiscard]] IConfigProviderPtr subItem(QStringView id) const override;
    [[nodiscard]] IConfigWriterPtr orCreateSubItem(QStringView id) override;
    [[nodiscard]] QVariant value(QStringView id) const override;
    [[nodiscard]] QByteArray binary(QStringView id) const override;
    [[nodiscard]] QStringList subItemNames() const override; // strips the "S_" prefix (D-1 fix)
    [[nodiscard]] QStringList valueNames() const override;   // keys in file order, first duplicate only
    void deleteSubItem(QStringView id) override;
    void deleteValue(QStringView id) override;
    void deleteValues() override;
    void writeValue(QStringView id, const QVariant& value) override;
    void writeBinary(QStringView id, const QByteArray& value) override;

    [[nodiscard]] const QString& sectionName() const noexcept;

private:
    std::shared_ptr<IniFile> file_;
    QString section_;
    IConfigProviderPtr parent_; // keeps the parent chain (and the IniFile) alive
};

} // namespace cg::core
