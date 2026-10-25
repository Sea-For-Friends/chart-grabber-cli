// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_StringByLanguage.pas, u_StringByLanguageWithStaticList.pas
//
// One string per language index; index 0 is the default (English) text.

#include <QString>
#include <QStringList>

#include <memory>

namespace cg::core {

// FEAT-NET-002
class IStringByLanguage {
public:
    virtual ~IStringByLanguage() = default;

    [[nodiscard]] virtual QString getString(int langIndex) const = 0;
    [[nodiscard]] virtual QString getDefault() const = 0;
};

using IStringByLanguagePtr = std::shared_ptr<const IStringByLanguage>;

class StringByLanguageWithStaticList final : public IStringByLanguage {
public:
    // The list must not be empty (legacy: Assert, off in release, L-30): with an empty list
    // getDefault() throws std::out_of_range, as legacy's TStringList raises EStringListError.
    explicit StringByLanguageWithStaticList(const QStringList& values);

    // index <= 0 or >= count falls back to getDefault().
    [[nodiscard]] QString getString(int langIndex) const override;
    [[nodiscard]] QString getDefault() const override;

private:
    QStringList values_;
};

} // namespace cg::core
