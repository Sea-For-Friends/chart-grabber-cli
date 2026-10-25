// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ZmpFileNamesIteratorFactory.pas (the composition), and the three Src/Basic iterators it
//        composes: u_FileNameIteratorFolderWithSubfolders.pas,
//        u_FoldersIteratorRecursiveByLevels(WithIgnoredFolders).pas,
//        u_FileNameIteratorInFolderByMask(List).pas.
//
// Legacy builds a lazy iterator out of the three; the only user (TZmpInfoSet) drains it into a list,
// so the port is one function that returns that list (spec maptype.md 2.3).

#include <QString>
#include <QStringList>

namespace cg::maptype {

// Relative names (from `rootFolder`, '/'-separated, no leading separator) of every ZMP entry,
// in directory-walk order. The caller sorts (zmp_info_set.cpp).
[[nodiscard]] QStringList zmpFileNames(const QString& rootFolder);

} // namespace cg::maptype
