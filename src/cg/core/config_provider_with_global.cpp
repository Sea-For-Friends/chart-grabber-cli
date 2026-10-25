// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/config_provider_with_global.h"

#include <memory>
#include <utility>

namespace cg::core {
namespace {

// Wraps a read-only IConfigProviderPtr as an IConfigWriter whose writes are silent no-ops: the
// `global` branch of ConfigProviderWithGlobal (a Qt resource today) is never writable, but
// IConfigWriter::orCreateSubItem() must still return something usable.
class NullConfigWriter final : public IConfigWriter {
public:
    explicit NullConfigWriter(IConfigProviderPtr source) : source_(std::move(source)) {}

    [[nodiscard]] IConfigProviderPtr subItem(QStringView id) const override {
        return source_ ? source_->subItem(id) : nullptr;
    }
    [[nodiscard]] QVariant value(QStringView id) const override {
        return source_ ? source_->value(id) : QVariant();
    }
    [[nodiscard]] QByteArray binary(QStringView id) const override {
        return source_ ? source_->binary(id) : QByteArray();
    }
    [[nodiscard]] QStringList subItemNames() const override {
        return source_ ? source_->subItemNames() : QStringList();
    }
    [[nodiscard]] QStringList valueNames() const override {
        return source_ ? source_->valueNames() : QStringList();
    }
    [[nodiscard]] IConfigWriterPtr orCreateSubItem(QStringView id) override {
        return std::make_shared<NullConfigWriter>(source_ ? source_->subItem(id) : nullptr);
    }
    void deleteSubItem(QStringView) override {}
    void deleteValue(QStringView) override {}
    void deleteValues() override {}
    void writeValue(QStringView, const QVariant&) override {}
    void writeBinary(QStringView, const QByteArray&) override {}

private:
    IConfigProviderPtr source_;
};

} // namespace

ConfigProviderWithGlobal::ConfigProviderWithGlobal(IConfigWriterPtr main, QString prefix,
                                                   IConfigProviderPtr global)
    : mainWrite_(main), mainRead_(std::move(main)), prefix_(std::move(prefix)), global_(std::move(global)) {}

ConfigProviderWithGlobal::ConfigProviderWithGlobal(IConfigProviderPtr mainRead, QString prefix,
                                                   IConfigProviderPtr global)
    : mainWrite_(nullptr), mainRead_(std::move(mainRead)), prefix_(std::move(prefix)),
      global_(std::move(global)) {}

IConfigWriterPtr ConfigProviderWithGlobal::create(IConfigWriterPtr main, QString prefix,
                                                  IConfigProviderPtr global) {
    return std::make_shared<ConfigProviderWithGlobal>(std::move(main), std::move(prefix), std::move(global));
}

bool ConfigProviderWithGlobal::isGlobalIdent(QStringView id, QString& stripped) const {
    if (!id.startsWith(prefix_)) {
        return false;
    }
    stripped = id.mid(prefix_.size()).toString();
    return true;
}

IConfigProviderPtr ConfigProviderWithGlobal::subItem(QStringView id) const {
    QString stripped;
    if (isGlobalIdent(id, stripped)) {
        return global_ ? global_->subItem(stripped) : nullptr;
    }
    if (!mainRead_) {
        return shared_from_this();
    }
    // Re-wrap the child (even a nullptr one, for a missing ini section) so the routing and the
    // "missing branch answers defaults" sentinel both hold at every depth, not just the root
    // (u_ConfigDataProviderWithGlobal.pas:126-130).
    return std::make_shared<ConfigProviderWithGlobal>(mainRead_->subItem(id), prefix_, global_);
}

QVariant ConfigProviderWithGlobal::value(QStringView id) const {
    QString stripped;
    if (isGlobalIdent(id, stripped)) {
        return global_ ? global_->value(stripped) : QVariant();
    }
    return mainRead_ ? mainRead_->value(id) : QVariant();
}

QByteArray ConfigProviderWithGlobal::binary(QStringView id) const {
    QString stripped;
    if (isGlobalIdent(id, stripped)) {
        return global_ ? global_->binary(stripped) : QByteArray();
    }
    return mainRead_ ? mainRead_->binary(id) : QByteArray();
}

QStringList ConfigProviderWithGlobal::subItemNames() const {
    return mainRead_ ? mainRead_->subItemNames() : QStringList();
}

QStringList ConfigProviderWithGlobal::valueNames() const {
    return mainRead_ ? mainRead_->valueNames() : QStringList();
}

IConfigWriterPtr ConfigProviderWithGlobal::orCreateSubItem(QStringView id) {
    QString stripped;
    if (isGlobalIdent(id, stripped)) {
        return std::make_shared<NullConfigWriter>(global_ ? global_->subItem(stripped) : nullptr);
    }
    if (!mainWrite_) {
        return shared_from_this();
    }
    // orCreateSubItem() always creates, so the child is never null; still re-wrap it so the
    // "sas:\" routing and further orCreateSubItem() descents keep working below the root.
    return std::make_shared<ConfigProviderWithGlobal>(mainWrite_->orCreateSubItem(id), prefix_, global_);
}

void ConfigProviderWithGlobal::deleteSubItem(QStringView id) {
    QString stripped;
    if (isGlobalIdent(id, stripped)) {
        return; // writes through the global branch are no-ops
    }
    if (mainWrite_) {
        mainWrite_->deleteSubItem(id);
    }
}

void ConfigProviderWithGlobal::deleteValue(QStringView id) {
    QString stripped;
    if (isGlobalIdent(id, stripped)) {
        return;
    }
    if (mainWrite_) {
        mainWrite_->deleteValue(id);
    }
}

void ConfigProviderWithGlobal::deleteValues() {
    if (mainWrite_) {
        mainWrite_->deleteValues();
    }
}

void ConfigProviderWithGlobal::writeValue(QStringView id, const QVariant& value) {
    QString stripped;
    if (isGlobalIdent(id, stripped)) {
        return;
    }
    if (mainWrite_) {
        mainWrite_->writeValue(id, value);
    }
}

void ConfigProviderWithGlobal::writeBinary(QStringView id, const QByteArray& value) {
    QString stripped;
    if (isGlobalIdent(id, stripped)) {
        return;
    }
    if (mainWrite_) {
        mainWrite_->writeBinary(id, value);
    }
}

} // namespace cg::core
