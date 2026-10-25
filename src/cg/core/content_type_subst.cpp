// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/content_type_subst.h"

#include "cg/core/delimited_text.h"

namespace cg::core {

ContentTypeSubst ContentTypeSubst::fromConfig(const IConfigProviderPtr& config) {
    if (!config) {
        return {};
    }
    return fromString(config->readString(QStringLiteral("MimeTypeSubst"), QString()));
}

ContentTypeSubst ContentTypeSubst::fromString(const QString& value) {
    ContentTypeSubst subst;
    for (const QString& entry : parseDelimitedText(value, u';')) {
        const QStringList parts = parseDelimitedText(entry, u'=');
        if (parts.size() != 2) {
            continue; // malformed entry, skipped (core.md 2.22)
        }
        const QString& source = parts.at(0);
        const QString& target = parts.at(1);
        if (source.isEmpty() || target.isEmpty()) {
            continue; // u_ContentTypeSubstByList.pas:125 (VSource <> '') and (VTarget <> '')
        }
        subst.entries_.insert(source.toLower().toUtf8(), target.toUtf8());
    }
    return subst;
}

QByteArray ContentTypeSubst::substitute(QByteArrayView source) const {
    const QByteArray key = source.toByteArray().toLower();
    const auto it = entries_.constFind(key);
    return it == entries_.constEnd() ? source.toByteArray() : it.value();
}

bool ContentTypeSubst::isEmpty() const noexcept {
    return entries_.isEmpty();
}

} // namespace cg::core
