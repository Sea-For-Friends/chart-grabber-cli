// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/config_provider_ini.h"

#include "cg/core/log.h"

#include <utility>

namespace cg::core {

ConfigProviderIni::ConfigProviderIni(std::shared_ptr<IniFile> file) : file_(std::move(file)) {}

std::shared_ptr<ConfigProviderIni> ConfigProviderIni::wrap(std::shared_ptr<IniFile> file) {
    return std::make_shared<ConfigProviderIni>(std::move(file));
}

Result<std::shared_ptr<ConfigProviderIni>> ConfigProviderIni::open(const QString& path) {
    Result<IniFile> loaded = IniFile::fromFile(path);
    if (!loaded.has_value()) {
        return std::unexpected(loaded.error());
    }
    return wrap(std::make_shared<IniFile>(std::move(loaded.value())));
}

IConfigProviderPtr ConfigProviderIni::subItem(QStringView id) const {
    const QString section = id.toString();
    if (!file_->hasSection(section)) {
        return nullptr;
    }
    return ConfigProviderIniSection::create(file_, section, shared_from_this());
}

IConfigWriterPtr ConfigProviderIni::orCreateSubItem(QStringView id) {
    return ConfigProviderIniSection::create(file_, id.toString(), shared_from_this());
}

QVariant ConfigProviderIni::value(QStringView) const {
    return {}; // Q25
}

QByteArray ConfigProviderIni::binary(QStringView) const {
    return {}; // Q25
}

QStringList ConfigProviderIni::subItemNames() const {
    return file_->sectionNames();
}

QStringList ConfigProviderIni::valueNames() const {
    return {}; // Q25
}

void ConfigProviderIni::deleteSubItem(QStringView id) {
    file_->eraseSection(id);
}

void ConfigProviderIni::deleteValue(QStringView id) {
    qCWarning(logConfig) << "ConfigProviderIni: deleteValue on the whole-file root is a no-op (Q26):" << id;
}

void ConfigProviderIni::deleteValues() {
    qCWarning(logConfig) << "ConfigProviderIni: deleteValues on the whole-file root is a no-op (Q26)";
}

void ConfigProviderIni::writeValue(QStringView id, const QVariant&) {
    qCWarning(logConfig) << "ConfigProviderIni: writeValue on the whole-file root is a no-op (Q26):" << id;
}

void ConfigProviderIni::writeBinary(QStringView id, const QByteArray&) {
    qCWarning(logConfig) << "ConfigProviderIni: writeBinary on the whole-file root is a no-op (Q26):" << id;
}

Result<void> ConfigProviderIni::save() {
    return file_->save();
}

IniFile& ConfigProviderIni::file() noexcept {
    return *file_;
}

const IniFile& ConfigProviderIni::file() const noexcept {
    return *file_;
}

} // namespace cg::core
