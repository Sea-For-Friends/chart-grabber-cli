// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkSystemConfig.pas, u_MarkSystemConfig.pas

#include "cg/core/config_element.h"
#include "cg/core/id_list.h"
#include "cg/marks/mark_system_impl_config.h"

#include <QString>
#include <QStringList>
#include <QUuid>

#include <memory>
#include <vector>

namespace cg::marks {

class IMarkSystemConfigStatic {
public:
    virtual ~IMarkSystemConfigStatic() = default;
    [[nodiscard]] virtual int id() const = 0;
    [[nodiscard]] virtual QUuid databaseGUID() const = 0;
    [[nodiscard]] virtual QString displayName() const = 0;
    [[nodiscard]] virtual MarkSystemImplConfigStaticPtr implConfig() const = 0;
};
using MarkSystemConfigStaticPtr = std::shared_ptr<const IMarkSystemConfigStatic>;

class IMarkSystemConfigListChangeable : public core::ConfigElement {
public:
    [[nodiscard]] virtual int count() const = 0;
    [[nodiscard]] virtual MarkSystemConfigStaticPtr byId(int id) const = 0;
    [[nodiscard]] virtual int activeConfigId() const = 0;
    virtual void setActiveConfigId(int id) = 0;
    [[nodiscard]] virtual MarkSystemConfigStaticPtr activeConfig() const = 0;
    virtual void deleteById(int id) = 0;
    virtual int add(const QUuid& databaseGUID, const QString& displayName,
                    const MarkSystemImplConfigStaticPtr& implConfig, bool setAsActive) = 0;
    virtual void update(int id, const QUuid& databaseGUID, const QString& displayName,
                        const MarkSystemImplConfigStaticPtr& implConfig) = 0;
    // Despite its name the legacy function returns the config OBJECTS (u_MarkSystemConfig.pas:464-484),
    // in ascending id order; the spec's std::vector<int> is wrong.
    [[nodiscard]] virtual std::vector<MarkSystemConfigStaticPtr> idListStatic() const = 0;
};

// u_MarkSystemConfig.pas:38-71. Reads/writes the INI section [MarkSystemConfig].
class MarkSystemConfig final : public IMarkSystemConfigListChangeable {
public:
    MarkSystemConfig() = default;

    [[nodiscard]] int count() const override;
    [[nodiscard]] MarkSystemConfigStaticPtr byId(int id) const override;
    [[nodiscard]] int activeConfigId() const override;
    void setActiveConfigId(int id) override;
    [[nodiscard]] MarkSystemConfigStaticPtr activeConfig() const override;
    void deleteById(int id) override;
    int add(const QUuid& databaseGUID, const QString& displayName,
            const MarkSystemImplConfigStaticPtr& implConfig, bool setAsActive) override;
    void update(int id, const QUuid& databaseGUID, const QString& displayName,
                const MarkSystemImplConfigStaticPtr& implConfig) override;
    [[nodiscard]] std::vector<MarkSystemConfigStaticPtr> idListStatic() const override;

    // DV-2: legacy shows a modal ShowErrorMessage from inside DoReadConfig (:216, :224, :251);
    // the messages of the last readConfig() are collected here for the UI instead.
    [[nodiscard]] QStringList readErrors() const;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;

private:
    int newId() { return ++id_; }

    int id_ = 0;
    core::IdInterfaceList<const IMarkSystemConfigStatic> list_;
    int activeConfigId_ = 0;
    QStringList readErrors_;
};

} // namespace cg::marks
