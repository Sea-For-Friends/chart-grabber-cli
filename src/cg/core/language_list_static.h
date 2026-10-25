// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_LanguageListStatic.pas, u_LanguageListStatic.pas
//
// Immutable ordered list of language codes; index 0 is the default language. FindCode compares
// case-insensitively (a sorted, case-insensitive TStringList in legacy).

#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace cg::core {

// FEAT-NET-002
class ILanguageListStatic {
public:
    virtual ~ILanguageListStatic() = default;

    [[nodiscard]] virtual int count() const = 0;
    // Out-of-range index throws std::out_of_range (legacy: EStringListError).
    [[nodiscard]] virtual QString code(int index) const = 0;
    [[nodiscard]] virtual std::optional<int> findCode(const QString& code) const = 0;
};

using ILanguageListStaticPtr = std::shared_ptr<const ILanguageListStatic>;

class LanguageListStatic final : public ILanguageListStatic {
public:
    // Throws std::invalid_argument when two codes are equal ignoring case (legacy: dupError, L-128).
    explicit LanguageListStatic(const QStringList& codes);

    [[nodiscard]] int count() const override;
    [[nodiscard]] QString code(int index) const override;
    [[nodiscard]] std::optional<int> findCode(const QString& code) const override;

private:
    QStringList list_;
};

} // namespace cg::core
