// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/PascalScript/u_PascalScriptLogger.pas (the whole unit; WriteFmt is the script
// binder's concern)
//
// The per-ZMP script log: `<logsPath>/zmp/<zmp base name>[.<appId>].log`, created lazily on the
// first non-empty write and truncated when created.

#include "cg/core/sync.h"
#include "cg/script/script_host.h"

#include <QByteArray>
#include <QFile>
#include <QString>

namespace cg::maptype {

class PascalScriptLogger final : public script::IScriptLogger {
public:
    PascalScriptLogger(int appId, const QString& logsPath, const QString& zmpFileName);

    // Throws std::runtime_error when the directory or the file cannot be created
    // or the record cannot be written (legacy raises an OS error / EWriteError).
    void write(const QByteArray& text) override;

    [[nodiscard]] const QString& fileName() const noexcept { return fileName_; }

private:
    void lazyInit();

    QString fileName_;
    core::Mutex mutex_;
    QFile file_;
};

} // namespace cg::maptype
