// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/config_provider_zip.h"

#include "cg/core/archive_reader.h"
#include "cg/core/archive_reader_zip.h"
#include "cg/core/config_member_ext.h"
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/core/log.h"
#include "cg/core/text_codec.h"

#include <QFile>
#include <QFileInfo>

#include <utility>

namespace cg::core {
namespace {

[[nodiscard]] QString withTrailingSlash(QString path) {
    if (!path.isEmpty() && !path.endsWith(QLatin1Char('/'))) {
        path += QLatin1Char('/');
    }
    return path;
}

[[nodiscard]] QString fileExt(const QString& name) {
    return QFileInfo(name).suffix();
}

// ExtractFileName: the part after the last '/' ("" for a directory item).
[[nodiscard]] QString extractFileName(const QString& fullName) {
    return fullName.mid(fullName.lastIndexOf(QLatin1Char('/')) + 1);
}

// A directory item has no name and no data from itemByIndex (ArchiveReaderZip); a file keeps its name.
[[nodiscard]] bool isDirectoryItem(const IArchiveReader& archive, int index) {
    QString name;
    const std::optional<QByteArray> data = archive.itemByIndex(index, name);
    return !data && name.isEmpty();
}

// ExtractFilePath(ExcludeTrailingPathDelimiter(name)) = subFolder, case-insensitively.
[[nodiscard]] bool isDirectChild(const QString& fullName, const QString& subFolder) {
    return fullName.left(fullName.lastIndexOf(QLatin1Char('/')) + 1)
               .compare(subFolder, Qt::CaseInsensitive) == 0;
}

} // namespace

ConfigProviderZip::ConfigProviderZip(QString sourceFileName, IArchiveReaderPtr archive, QString subFolder)
    : sourceFileName_(std::move(sourceFileName)), archive_(std::move(archive)),
      subFolder_(withTrailingSlash(std::move(subFolder))) {}

Result<std::shared_ptr<ConfigProviderZip>> ConfigProviderZip::open(const QString& zipFilePath) {
    QFile file(zipFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(ErrorCode::IoError, QStringLiteral("cannot read ") + zipFilePath);
    }
    Result<IArchiveReaderPtr> reader = openArchiveReaderZip(file.readAll(), zipFilePath);
    if (!reader.has_value()) {
        return std::unexpected(
            Error{reader.error().code(), zipFilePath + QStringLiteral(": ") + reader.error().what()});
    }
    return std::make_shared<ConfigProviderZip>(zipFilePath, std::move(reader.value()));
}

Result<void> ConfigProviderZip::status() const {
    return archive_->status();
}

IConfigProviderPtr ConfigProviderZip::subItem(QStringView id) const {
    const QString ident = id.toString();
    if (isIniOrTxtExt(fileExt(ident))) {
        const std::optional<QByteArray> data = archive_->itemByName(subFolder_ + ident);
        if (!data) {
            return nullptr;
        }
        Result<IniFile> loaded = IniFile::fromBytes(*data);
        if (!loaded.has_value()) {
            qCWarning(logConfig) << "ConfigProviderZip: cannot parse" << ident << ":"
                                 << loaded.error().what();
            return nullptr;
        }
        return ConfigProviderIni::wrap(std::make_shared<IniFile>(std::move(loaded.value())));
    }
    // DEVIATION (T-0273, L-155; register: the D row of this ticket): legacy compares the item name with
    // "<folder>\" (a trailing delimiter) only, and 7-Zip gives a directory item without it, so a sub-folder
    // of a zipped map was never found (u_ConfigDataProviderByZip.pas:158-165). The folder is found with its
    // directory entry (with or without the delimiter) or with any member below it; the members stay readable
    // by their full path.
    QString folder = ident;
    folder.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QString subFolder = withTrailingSlash(subFolder_ + folder);
    const QString directoryEntry = subFolder.chopped(1);
    for (int i = 0; i < archive_->itemsCount(); ++i) {
        const QString name = archive_->itemNameByIndex(i);
        if (name.startsWith(subFolder, Qt::CaseInsensitive) ||
            (name.compare(directoryEntry, Qt::CaseInsensitive) == 0 && isDirectoryItem(*archive_, i))) {
            return std::make_shared<ConfigProviderZip>(sourceFileName_ + QLatin1Char('/') + ident, archive_,
                                                       subFolder);
        }
    }
    return nullptr;
}

// ReadString. A missing text member is "" and not "absent" (LD-1).
QVariant ConfigProviderZip::value(QStringView id) const {
    if (id == u"::FileName") {
        return sourceFileName_;
    }
    if (!isIniTxtOrHtmlExt(fileExt(id.toString()))) {
        return {};
    }
    const std::optional<QByteArray> data = archive_->itemByName(subFolder_ + id.toString());
    return data ? decodeConfigText(*data).text : QString();
}

QByteArray ConfigProviderZip::binary(QStringView id) const {
    return archive_->itemByName(subFolder_ + id.toString()).value_or(QByteArray());
}

QStringList ConfigProviderZip::subItemNames() const {
    QStringList names;
    for (int i = 0; i < archive_->itemsCount(); ++i) {
        const QString full = archive_->itemNameByIndex(i);
        if (isDirectChild(full, subFolder_)) {
            const QString name = extractFileName(full);
            if (isIniOrTxtExt(fileExt(name))) {
                names.append(name);
            }
        }
    }
    return names;
}

QStringList ConfigProviderZip::valueNames() const {
    QStringList names;
    for (int i = 0; i < archive_->itemsCount(); ++i) {
        const QString full = archive_->itemNameByIndex(i);
        if (isDirectChild(full, subFolder_)) {
            names.append(extractFileName(full));
        }
    }
    return names;
}

} // namespace cg::core
