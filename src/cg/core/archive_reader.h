// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ArchiveReadWrite.pas (IArchiveReader, the random-access reader half only)
//
// An opened archive as a flat list of items. Names are the archive's own paths with '\' turned
// into '/'; no name ends in '/' (7-Zip's kpidPath removes it, so a directory "X/" is the item "X"). A data
// result of std::nullopt is legacy's nil IBinaryData: the item is a directory, the name is unknown or the
// item could not be extracted.

#include "cg/core/result.h"

#include <QByteArray>
#include <QString>

#include <memory>
#include <optional>

namespace cg::core {

class IArchiveReader {
public:
    virtual ~IArchiveReader() = default;

    [[nodiscard]] virtual int itemsCount() const = 0;
    // The first item whose name equals itemName: exactly, else case-insensitively (D-48).
    [[nodiscard]] virtual std::optional<QByteArray> itemByName(const QString& itemName) const = 0;
    [[nodiscard]] virtual QString itemNameByIndex(int itemIndex) const = 0;
    [[nodiscard]] virtual std::optional<QByteArray> itemByIndex(int itemIndex, QString& itemName) const = 0;
    // The first extraction failure, sticky (legacy raised from ExtractItem). A value = none so far.
    [[nodiscard]] virtual Result<void> status() const { return {}; }
};

using IArchiveReaderPtr = std::shared_ptr<const IArchiveReader>;

} // namespace cg::core
