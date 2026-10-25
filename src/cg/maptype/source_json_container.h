// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing. NEW code, no legacy unit: the `*.source.json` source definition
// (docs/specs/map-source-json.md sections 1-4) presented to ZmpInfo as the container of a ZMP. Read-only.
//
// Children: "params.txt" (an ini with a [PARAMS] section carrying the section 3 key mapping) and
// "EmptyTiles" (the decoded `emptyTiles` samples); binary("GetUrlScript.js") is the script text, which
// TileDownloadSubsystem hands to the JavaScript factory (T-0225).

#include "cg/core/config_provider.h"
#include "cg/core/result.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace cg::maptype {

class SourceJsonContainer final : public core::IConfigProvider {
public:
    // True for a file name ending `.source.json` (ASCII case-insensitive), spec section 2.
    [[nodiscard]] static bool isSourceJsonName(const QString& name);

    // Reads and parses the file; the error text names the key (spec section 2).
    [[nodiscard]] static core::Result<std::shared_ptr<const SourceJsonContainer>>
    load(const QString& filePath);

    // `bytes` is the file content; `folder` is where `script.file` is resolved.
    [[nodiscard]] static core::Result<std::shared_ptr<const SourceJsonContainer>>
    parse(const QByteArray& bytes, const QString& folder);

    [[nodiscard]] core::IConfigProviderPtr subItem(QStringView id) const override;
    [[nodiscard]] QVariant value(QStringView id) const override;
    [[nodiscard]] QByteArray binary(QStringView id) const override;
    [[nodiscard]] QStringList subItemNames() const override;
    [[nodiscard]] QStringList valueNames() const override;

    SourceJsonContainer(core::IConfigProviderPtr params, core::IConfigProviderPtr emptyTiles,
                        QByteArray script, bool hasScript);

private:
    core::IConfigProviderPtr params_;
    core::IConfigProviderPtr emptyTiles_; // null when no samples
    QByteArray script_;
    bool hasScript_;
};

} // namespace cg::maptype
