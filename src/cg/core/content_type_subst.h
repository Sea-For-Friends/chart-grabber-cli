// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ContentTypeSubst.pas, u_ContentTypeSubstByList.pas
//
// Per-map MIME rewriting: the `MimeTypeSubst` value of a .zmp params.txt [PARAMS] section maps a
// server's (mis)reported content type onto the one the map author actually meant. Consumed by
// net/maptype; core only owns the parsing and lookup.

#include "cg/core/config_provider.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QString>

namespace cg::core {

class ContentTypeSubst {
public:
    ContentTypeSubst() = default; // empty table: substitute() is the identity

    // Parses the `MimeTypeSubst` value of a .zmp params.txt [PARAMS] section. Format: ';'-separated
    // entries, each `source=target`, Delphi DelimitedText quoting (u_ContentTypeSubstByList.pas
    // 87-133).
    [[nodiscard]] static ContentTypeSubst fromConfig(const IConfigProviderPtr& config);
    [[nodiscard]] static ContentTypeSubst fromString(const QString& value);

    // Returns the mapped type, or `source` unchanged when there is no entry
    // (u_ContentTypeSubstByList.pas:74-85 -- pass-through, NOT empty). Source keys are matched
    // case-insensitively.
    [[nodiscard]] QByteArray substitute(QByteArrayView source) const;

    [[nodiscard]] bool isEmpty() const noexcept;

private:
    QHash<QByteArray, QByteArray> entries_; // key: lower-cased source
};

} // namespace cg::core
