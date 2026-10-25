// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkNameGenerator.pas (interface, shared), u_MarkNameGenerator.pas (MarkNameGenerator, T-0091)

#include "cg/core/config_element_complex.h"
#include "cg/core/string_config_data_element.h"

#include <QString>

#include <memory>

namespace cg::marks {

class IMarkNameGenerator : public core::ConfigElementComplex {
public:
    [[nodiscard]] virtual std::shared_ptr<core::IStringConfigDataElement> formatString() const = 0;
    [[nodiscard]] virtual int counter() const = 0;
    virtual void setCounter(int value) = 0;
    // Formats with the counter, then increments it: has a side effect (legacy GetNewName).
    [[nodiscard]] virtual QString newName() = 0;
};

// Ports TMarkNameGenerator. The format element is supplied by the owner (the template config).
class MarkNameGenerator final : public IMarkNameGenerator {
public:
    // A null format element is refused (legacy: passed to Add and read on first use).
    explicit MarkNameGenerator(std::shared_ptr<core::IStringConfigDataElement> formatString);
    ~MarkNameGenerator() override;

    [[nodiscard]] std::shared_ptr<core::IStringConfigDataElement> formatString() const override;
    [[nodiscard]] int counter() const override;
    void setCounter(int value) override;
    // Ports GetNewName (u_MarkNameGenerator.pas:101-111). Throws std::runtime_error with the Delphi Format
    // error text on a malformed format; the counter is then unchanged and nothing is notified.
    [[nodiscard]] QString newName() override;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;

private:
    std::shared_ptr<core::IStringConfigDataElement> formatString_;
    int counter_ = 0;
};

} // namespace cg::marks
