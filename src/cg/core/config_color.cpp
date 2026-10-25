// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/config_color.h"

#include "cg/core/delphi_text.h"

namespace cg::core {

Color32 readColor32(const IConfigProviderPtr& provider, QStringView id, Color32 def) {
    if (!provider) {
        return def;
    }
    const QString hexId = id.toString() + QStringLiteral("Hex");
    const QString hex = provider->readString(hexId, QString());
    if (hex.isEmpty()) {
        const qint32 alpha = provider->readInt(id.toString() + QStringLiteral("Alfa"), alphaComponent(def));
        const qint32 color = provider->readInt(id, winColor(def));
        return setAlpha(color32FromWinColor(color), alpha);
    }
    int parsed = 0;
    if (delphiTryStrToInt(hex, parsed)) {
        return static_cast<Color32>(parsed);
    }
    return def;
}

void writeColor32(IConfigWriter& provider, QStringView id, Color32 value) {
    const QString digits = QStringLiteral("%1").arg(value, 8, 16, QLatin1Char('0')).toUpper();
    provider.writeString(id.toString() + QStringLiteral("Hex"), QLatin1Char('$') + digits);
}

} // namespace cg::core
