// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ConfigDataProviderByZip.pas (TConfigDataProviderByArchive)
//
// A zip archive (a ".zmp" file) or one folder inside it, exposed as a read-only config node.
// Differences from ConfigProviderFolder that are legacy's own: a text member that is missing reads
// as an empty string, not as "absent" (LD-1, L-154); valueNames() lists every direct member
// including .ini files and lists a directory entry by its name (L-155); subItem() finds a folder with or
// without its directory entry (legacy never did, L-155; the D row of T-0273) -- the members inside
// are reachable by their full path ("EmptyTiles/NA.png") through binary()/value(). Legacy's typed
// ReadInteger/ReadBool/... always answered the default; here the shared helpers parse a text member, but only
// for an id ending in .ini/.html/.txt, which no caller passes to a typed reader (typed params.txt values come
// from the INI sub-provider; the folder provider has the same legacy shape). Member names are looked up
// exactly first, then case-insensitively (D-48); '\' and '/' are the same separator.

#include "cg/core/archive_reader.h"
#include "cg/core/config_provider.h"
#include "cg/core/result.h"

namespace cg::core {

// prerequisite of FEAT-NET-001: the zip form of a ZMP container
class ConfigProviderZip final : public IConfigProvider {
public:
    // sourceFileName is what "::FileName" answers; subFolder is "" for the archive root.
    ConfigProviderZip(QString sourceFileName, IArchiveReaderPtr archive, QString subFolder = {});

    // Reads the file into memory and opens it as a zip (u_ZmpInfoSet.pas:141-155). Fails when the
    // file cannot be read or is not a readable zip.
    [[nodiscard]] static Result<std::shared_ptr<ConfigProviderZip>> open(const QString& zipFilePath);

    [[nodiscard]] IConfigProviderPtr subItem(QStringView id) const override;
    [[nodiscard]] QVariant value(QStringView id) const override;
    [[nodiscard]] QByteArray binary(QStringView id) const override;
    [[nodiscard]] QStringList subItemNames() const override; // *.ini / *.txt members of this folder
    [[nodiscard]] QStringList valueNames() const override;   // every member of this folder

    // The first member that could not be extracted (damaged, encrypted, over the size cap), sticky
    // and shared with every sub-provider of the same archive. Such a member reads as absent
    // (empty); legacy raised and the ZMP set loader dropped the ZMP -- the loader (T-E) must check
    // this (D-63).
    [[nodiscard]] Result<void> status() const;

private:
    QString sourceFileName_;
    IArchiveReaderPtr archive_;
    QString subFolder_; // "" or ends in '/'
};

} // namespace cg::core
