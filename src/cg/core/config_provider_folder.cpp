// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/config_provider_folder.h"

#include "cg/core/config_member_ext.h"
#include "cg/core/config_provider_ini.h"
#include "cg/core/log.h"
#include "cg/core/text_codec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <utility>

namespace cg::core {
namespace {

constexpr QDir::Filters kAllEntries = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;

} // namespace

ConfigProviderFolder::ConfigProviderFolder(QString folderPath) : folderPath_(std::move(folderPath)) {}

IConfigProviderPtr ConfigProviderFolder::create(QString folderPath) {
    return std::make_shared<ConfigProviderFolder>(std::move(folderPath));
}

// Member lookup: the exact name, else the first entry equal under Qt::CaseInsensitive.
QString ConfigProviderFolder::memberPath(QStringView id) const {
    const QDir dir(folderPath_);
    QString exact = dir.filePath(id.toString());
    if (QFileInfo::exists(exact)) {
        return exact;
    }
    const QFileInfoList entries = dir.entryInfoList(kAllEntries, QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (entry.fileName().compare(id, Qt::CaseInsensitive) == 0) {
            return entry.filePath();
        }
    }
    return exact;
}

IConfigProviderPtr ConfigProviderFolder::subItem(QStringView id) const {
    const QFileInfo info(memberPath(id));
    if (info.isDir()) {
        return create(info.filePath());
    }
    if (info.isFile() && isIniOrTxtExt(info.suffix())) {
        Result<std::shared_ptr<ConfigProviderIni>> opened = ConfigProviderIni::open(info.filePath());
        if (!opened.has_value()) {
            qCWarning(logConfig) << "ConfigProviderFolder: cannot open" << info.filePath() << ":"
                                 << opened.error().what();
            return nullptr;
        }
        return opened.value();
    }
    return nullptr;
}

QVariant ConfigProviderFolder::value(QStringView id) const {
    if (id == u"::FileName") {
        return folderPath_;
    }
    const QString ext = QFileInfo(id.toString()).suffix();
    if (!isIniTxtOrHtmlExt(ext)) {
        return {};
    }
    QFile file(memberPath(id));
    if (!QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return decodeConfigText(file.readAll()).text;
}

QByteArray ConfigProviderFolder::binary(QStringView id) const {
    QFile file(memberPath(id));
    if (!QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QStringList ConfigProviderFolder::subItemNames() const {
    QStringList names;
    const QFileInfoList entries = QDir(folderPath_).entryInfoList(kAllEntries, QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (!entry.isDir() && isIniOrTxtExt(entry.suffix())) {
            names.append(entry.fileName());
        }
    }
    return names;
}

// Legacy tests (Ext <> '.INI') or (Ext = '.HTML') or (Ext = '.TXT'), which is true for every
// extension but .INI (L-117): any stray file counts as a value.
QStringList ConfigProviderFolder::valueNames() const {
    QStringList names;
    const QFileInfoList entries = QDir(folderPath_).entryInfoList(kAllEntries, QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (!entry.isDir() && entry.suffix().compare(QStringLiteral("ini"), Qt::CaseInsensitive) != 0) {
            names.append(entry.fileName());
        }
    }
    return names;
}

} // namespace cg::core
