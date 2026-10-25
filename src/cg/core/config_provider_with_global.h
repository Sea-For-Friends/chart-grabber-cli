// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ConfigDataProviderWithGlobal.pas, u_ConfigDataWriteProviderWithGlobal.pas
//
// An identifier that starts with `prefix` (legacy: "sas:\") is routed to `global` with the prefix
// stripped; everything else goes to `main`. Descending through subItem()/orCreateSubItem()
// re-wraps the child in a new ConfigProviderWithGlobal carrying the same prefix and global, so the
// routing holds for the *whole* sub-tree, not just the root (u_ConfigDataProviderWithGlobal.pas
// :126-130 re-wraps on every GetSubItem). A nullptr `main` -- whether at the root or produced by a
// descent into a missing ini section -- makes subItem() (and orCreateSubItem()) return `this`
// instead of nullptr, so a missing branch of the ini still answers reads with defaults and further
// chained descents keep working; writes through a global-prefixed identifier are no-ops (global is
// read-only).

#include "cg/core/config_writer.h"

#include <memory>

namespace cg::core {

// FEAT-UI-204
class ConfigProviderWithGlobal final : public IConfigWriter,
                                       public std::enable_shared_from_this<ConfigProviderWithGlobal> {
public:
    ConfigProviderWithGlobal(IConfigWriterPtr main, QString prefix, IConfigProviderPtr global);

    // Used when descending through the read-only subItem() path: the child has no write
    // capability of its own (matches legacy's GetSubItem always re-wrapping into the read-only
    // provider class, even when called on a write provider).
    ConfigProviderWithGlobal(IConfigProviderPtr mainRead, QString prefix, IConfigProviderPtr global);

    [[nodiscard]] static IConfigWriterPtr create(IConfigWriterPtr main, QString prefix,
                                                 IConfigProviderPtr global);

    [[nodiscard]] IConfigProviderPtr subItem(QStringView id) const override;
    [[nodiscard]] QVariant value(QStringView id) const override;
    [[nodiscard]] QByteArray binary(QStringView id) const override;
    [[nodiscard]] QStringList subItemNames() const override;
    [[nodiscard]] QStringList valueNames() const override;
    [[nodiscard]] IConfigWriterPtr orCreateSubItem(QStringView id) override;
    void deleteSubItem(QStringView id) override;
    void deleteValue(QStringView id) override;
    void deleteValues() override;
    void writeValue(QStringView id, const QVariant& value) override;
    void writeBinary(QStringView id, const QByteArray& value) override;

    // Lets a caller that built this composition (main_config.cpp) reach back into the "main"
    // branch, e.g. to save the IniFile behind it. Not part of the IConfigProvider contract.
    [[nodiscard]] const IConfigWriterPtr& mainProvider() const noexcept { return mainWrite_; }

private:
    [[nodiscard]] bool isGlobalIdent(QStringView id, QString& stripped) const;

    IConfigWriterPtr mainWrite_;  // non-null only where writes are possible (root, or a child
                                  // reached via orCreateSubItem()); used by the write-only methods.
    IConfigProviderPtr mainRead_; // read view of mainWrite_, or a read-only child reached via
                                  // subItem(); used by every read method and by the "missing
                                  // branch" nullptr check.
    QString prefix_;
    IConfigProviderPtr global_;
};

} // namespace cg::core
