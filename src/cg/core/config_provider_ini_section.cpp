// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/config_provider_ini_section.h"

#include <algorithm>
#include <utility>

namespace cg::core {

ConfigProviderIniSection::ConfigProviderIniSection(std::shared_ptr<IniFile> file, QString section,
                                                   IConfigProviderPtr parent)
    : file_(std::move(file)), section_(std::move(section)), parent_(std::move(parent)) {}

std::shared_ptr<ConfigProviderIniSection>
ConfigProviderIniSection::create(std::shared_ptr<IniFile> file, QString section, IConfigProviderPtr parent) {
    return std::make_shared<ConfigProviderIniSection>(std::move(file), std::move(section), std::move(parent));
}

IConfigProviderPtr ConfigProviderIniSection::subItem(QStringView id) const {
    const QString childName = section_ + QLatin1Char('_') + id.toString();
    if (!file_->hasSection(childName)) {
        return nullptr;
    }
    return create(file_, childName, shared_from_this());
}

IConfigWriterPtr ConfigProviderIniSection::orCreateSubItem(QStringView id) {
    const QString childName = section_ + QLatin1Char('_') + id.toString();
    return create(file_, childName, shared_from_this());
}

QVariant ConfigProviderIniSection::value(QStringView id) const {
    if (!file_->hasKey(section_, id)) {
        return {};
    }
    return file_->readString(section_, id, QString());
}

QByteArray ConfigProviderIniSection::binary(QStringView id) const {
    return file_->readBinary(section_, id);
}

QStringList ConfigProviderIniSection::subItemNames() const {
    const QString prefix = section_ + QLatin1Char('_');
    QStringList names;
    for (const QString& name : file_->sectionNames()) {
        if (name.size() > prefix.size() && name.startsWith(prefix, Qt::CaseInsensitive)) {
            names.append(name.sliced(prefix.size())); // D-1: correct prefix strip
        }
    }
    return names;
}

QStringList ConfigProviderIniSection::valueNames() const {
    QStringList result;
    for (const QString& key : file_->keyNames(section_)) {
        const bool seen = std::any_of(result.cbegin(), result.cend(), [&key](const QString& other) {
            return other.compare(key, Qt::CaseInsensitive) == 0;
        });
        if (!seen) {
            result.append(key); // first duplicate only
        }
    }
    return result;
}

void ConfigProviderIniSection::deleteSubItem(QStringView id) {
    file_->eraseSection(section_ + QLatin1Char('_') + id.toString());
}

void ConfigProviderIniSection::deleteValue(QStringView id) {
    file_->deleteKey(section_, id);
}

void ConfigProviderIniSection::deleteValues() {
    for (const QString& key : valueNames()) {
        file_->deleteKey(section_, key);
    }
}

void ConfigProviderIniSection::writeValue(QStringView id, const QVariant& value) {
    file_->writeString(section_, id, value.toString());
}

void ConfigProviderIniSection::writeBinary(QStringView id, const QByteArray& value) {
    file_->writeBinary(section_, id, value);
}

const QString& ConfigProviderIniSection::sectionName() const noexcept {
    return section_;
}

} // namespace cg::core
