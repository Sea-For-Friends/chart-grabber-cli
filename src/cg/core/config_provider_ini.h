// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ConfigDataProviderByIniFile.pas, u_ConfigDataWriteProviderByIniFile.pas
//
// The whole-file node. The root has no values of its own: every Read* returns the default
// unconditionally and only subItem()/subItemNames() do anything (u_ConfigDataProviderByIniFile.pas
// 117-166). Value writes on the root are logged no-ops rather than the legacy
// `Exception.Create('Not expected')`: throwing during a settings save would lose the whole file.

#include "cg/core/config_provider_ini_section.h"
#include "cg/core/config_writer.h"
#include "cg/core/ini_file.h"
#include "cg/core/result.h"

#include <memory>

namespace cg::core {

class ConfigProviderIni final : public IConfigWriter, public std::enable_shared_from_this<ConfigProviderIni> {
public:
    explicit ConfigProviderIni(std::shared_ptr<IniFile> file);

    [[nodiscard]] static std::shared_ptr<ConfigProviderIni> wrap(std::shared_ptr<IniFile> file);
    [[nodiscard]] static Result<std::shared_ptr<ConfigProviderIni>> open(const QString& path);

    [[nodiscard]] IConfigProviderPtr subItem(QStringView id) const override;
    [[nodiscard]] IConfigWriterPtr orCreateSubItem(QStringView id) override;
    [[nodiscard]] QVariant value(QStringView id) const override;        // Always invalid
    [[nodiscard]] QByteArray binary(QStringView id) const override;     // Always empty
    [[nodiscard]] QStringList subItemNames() const override;            // = section names
    [[nodiscard]] QStringList valueNames() const override;              // Always empty
    void deleteSubItem(QStringView id) override;                        // eraseSection
    void deleteValue(QStringView id) override;                          // Logged no-op
    void deleteValues() override;                                       // Logged no-op
    void writeValue(QStringView id, const QVariant& value) override;    // Logged no-op
    void writeBinary(QStringView id, const QByteArray& value) override; // Logged no-op

    [[nodiscard]] Result<void> save(); // ports the destructor's IniFile.UpdateFile
    [[nodiscard]] IniFile& file() noexcept;
    [[nodiscard]] const IniFile& file() const noexcept;

private:
    std::shared_ptr<IniFile> file_;
};

} // namespace cg::core
