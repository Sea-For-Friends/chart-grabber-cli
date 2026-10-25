// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ConfigDataWriteProvider.pas
//
// Adds mutation to the settings tree: create-on-write children, deletion and typed writers that
// mirror the reader's exact Delphi-format conversions (cg/core/config_provider.h).

#include "cg/core/config_provider.h"

#include <memory>

namespace cg::core {

class IConfigWriter;
using IConfigWriterPtr = std::shared_ptr<IConfigWriter>;

// FEAT-UI-199
class IConfigWriter : public IConfigProvider {
public:
    // Always returns a writable child, creating it lazily on the first write
    // (ports GetOrCreateSubItem).
    [[nodiscard]] virtual IConfigWriterPtr orCreateSubItem(QStringView id) = 0;

    virtual void deleteSubItem(QStringView id) = 0;
    virtual void deleteValue(QStringView id) = 0;
    virtual void deleteValues() = 0;

    virtual void writeValue(QStringView id, const QVariant& value) = 0;
    virtual void writeBinary(QStringView id, const QByteArray& value) = 0;

    // Non-virtual typed helpers, mirroring the reader's rules.
    void writeString(QStringView id, const QString& value);
    void writeInt(QStringView id, qint32 value);
    void writeBool(QStringView id, bool value);
    void writeFloat(QStringView id, double value);
    void writeDate(QStringView id, QDate value);
    void writeTime(QStringView id, QTime value);
    void writeDateTime(QStringView id, const QDateTime& value);
    void writeUuid(QStringView id, const QUuid& value); // "{XXXXXXXX-....}" upper-case, braced
    void writeStringList(QStringView id, const QStringList& value);
};

} // namespace cg::core
