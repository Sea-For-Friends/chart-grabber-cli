// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/config_writer.h"

#include "cg/core/ini_value_codec.h"

namespace cg::core {

void IConfigWriter::writeString(QStringView id, const QString& value) {
    writeValue(id, QVariant(value));
}

void IConfigWriter::writeInt(QStringView id, qint32 value) {
    writeValue(id, QVariant(QString::number(value)));
}

void IConfigWriter::writeBool(QStringView id, bool value) {
    writeValue(id, QVariant(value ? QStringLiteral("1") : QStringLiteral("0"))); // Q15
}

void IConfigWriter::writeFloat(QStringView id, double value) {
    writeValue(id, QVariant(valuecodec::formatFloat(value))); // Q17
}

void IConfigWriter::writeDate(QStringView id, QDate value) {
    writeValue(id, QVariant(valuecodec::formatDate(value))); // Q18
}

void IConfigWriter::writeTime(QStringView id, QTime value) {
    writeValue(id, QVariant(valuecodec::formatTime(value))); // Q18
}

void IConfigWriter::writeDateTime(QStringView id, const QDateTime& value) {
    writeValue(id, QVariant(valuecodec::formatDateTime(value))); // Q18
}

void IConfigWriter::writeUuid(QStringView id, const QUuid& value) {
    // Ports GUIDToString -> upper-case, braced.
    writeValue(id, QVariant(value.toString(QUuid::WithBraces).toUpper()));
}

void IConfigWriter::writeStringList(QStringView id, const QStringList& value) {
    // Ports u_ConfigProviderHelpers.pas:685-697: <id>_Count then <id>_0... A null/empty list
    // writes only <id>_Count = 0.
    const QString base = id.toString();
    writeInt(base + QStringLiteral("_Count"), static_cast<qint32>(value.size()));
    for (qsizetype i = 0; i < value.size(); ++i) {
        writeString(base + QLatin1Char('_') + QString::number(i), value.at(i));
    }
}

} // namespace cg::core
