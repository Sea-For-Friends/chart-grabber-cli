// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/script/diagnostic.h"

#include <QStringList>

namespace cg::script {

QString Diagnostic::toString() const {
    return QStringLiteral("[Line %1, Column %2] %3").arg(line).arg(column).arg(message);
}

QString formatDiagnostics(const DiagnosticList& list) {
    QStringList lines;
    lines.reserve(static_cast<int>(list.size()));
    for (const Diagnostic& diagnostic : list) {
        lines.append(diagnostic.toString());
    }
    return lines.join(QChar(u'\n'));
}

} // namespace cg::script
