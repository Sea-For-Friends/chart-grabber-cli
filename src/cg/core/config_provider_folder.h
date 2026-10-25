// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ConfigDataProviderByFolder.pas
//
// A directory of the filesystem exposed as a read-only config node: a subdirectory becomes
// another ConfigProviderFolder, an ".ini"/".txt" file becomes a ConfigProviderIni over its parsed
// contents, and anything else is unreachable through subItem(). value() reads ".ini"/".html"/".txt"
// files as decoded text and "::FileName" as the folder path. Extensions are matched
// case-insensitively. A member name is looked up exactly first, then case-insensitively (the first
// match in QDir::Name order): legacy ran on a case-insensitive filesystem (deviation, maptype 2.2).

#include "cg/core/config_provider.h"

#include <memory>

namespace cg::core {

// FEAT-UI-201
class ConfigProviderFolder final : public IConfigProvider {
public:
    explicit ConfigProviderFolder(QString folderPath);

    [[nodiscard]] static IConfigProviderPtr create(QString folderPath);

    [[nodiscard]] IConfigProviderPtr subItem(QStringView id) const override;
    [[nodiscard]] QVariant value(QStringView id) const override;    // "::FileName", or a text file's contents
    [[nodiscard]] QByteArray binary(QStringView id) const override; // the file's raw bytes, or empty
    [[nodiscard]] QStringList subItemNames() const override; // *.ini/*.txt files (no dirs), QDir::Name order
    [[nodiscard]] QStringList valueNames() const override;   // every file except *.ini

private:
    [[nodiscard]] QString memberPath(QStringView id) const;

    QString folderPath_;
};

} // namespace cg::core
