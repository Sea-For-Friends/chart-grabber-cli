// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/zmp_file_names_iterator_factory.h"

#include "cg/maptype/source_json_container.h"

#include <QDir>
#include <QFileInfo>

#include <deque>

namespace cg::maptype {

namespace {

// u_ZmpFileNamesIteratorFactory.pas:70 --
// TFoldersIteratorRecursiveByLevelsWithIgnoredFoldersFactory.Create(6, ...)
constexpr int kMaxFolderDepth = 6;

// PathMatchSpec(name, '*.zmp') (Win32 masks are case-insensitive), u_ZmpFileNamesIteratorFactory.pas:65-66
bool isZmpName(const QString& name) {
    return name.endsWith(QLatin1String(".zmp"), Qt::CaseInsensitive);
}

// PathMatchSpec(name, '.*'), :67
bool isDotName(const QString& name) {
    return name.startsWith(QLatin1Char('.'));
}

struct PendingFolder {
    QString fromRoot; // '' for the root
    int depth;
};

} // namespace

QStringList zmpFileNames(const QString& rootFolder) {
    QStringList result;
    // TFoldersIteratorRecursiveByLevels.Next (u_FoldersIteratorRecursiveByLevels.pas:109-120): the front of
    // the list is returned and its sub-folders are appended -- a breadth-first walk.
    std::deque<PendingFolder> folders{{QString(), 0}};
    while (!folders.empty()) {
        const PendingFolder folder = folders.front();
        folders.pop_front();
        const QString prefix = folder.fromRoot.isEmpty() ? QString() : folder.fromRoot + QLatin1Char('/');
        const QDir dir(folder.fromRoot.isEmpty() ? rootFolder : QDir(rootFolder).filePath(folder.fromRoot));
        // FindFirstFile lists hidden and system entries too; '.' and '..' are skipped by
        // TFileNameIteratorInFolderByMask.IsNeedProcess (u_FileNameIteratorInFolderByMask.pas:101-102).
        const QFileInfoList entries = dir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
        for (const QFileInfo& entry : entries) {
            const QString name = entry.fileName();
            // ProcessAddSubFolders (:133-153): only while depth < max; IsNeedFolderProcess of the
            // ignoring subclass (WithIgnoredFolders.pas:100-119) drops '*.zmp' and '.*'.
            if (folder.depth < kMaxFolderDepth && entry.isDir() && !isZmpName(name) && !isDotName(name)) {
                folders.push_back({prefix + name, folder.depth + 1});
            }
            // AFilesOnly = False (:76): files and directories alike; mask '*.zmp' (:65). NEW: also
            // the files of a `*.source.json` source (map-source-json.md section 2).
            if (isZmpName(name) || (entry.isFile() && SourceJsonContainer::isSourceJsonName(name))) {
                result.append(prefix + name);
            }
        }
    }
    return result;
}

} // namespace cg::maptype
