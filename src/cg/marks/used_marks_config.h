// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_UsedMarksConfig.pas (IUsedMarksConfig), u_UsedMarksConfig.pas (TUsedMarksConfig)
//
// The three used-marks flags of a marks layer (IsUseMarks, IgnoreCategoriesVisible, IgnoreMarksVisible), with
// defaults true / false / false. Persisted in the parent's section (u_MarksLayerConfig.pas:62-63 adds this
// element with the basic use-provider strategy, so the keys sit directly in the parent's section). The static
// snapshot is rebuilt before every change notification (ConfigElement base) and published through
// core::Snapshot.

#include "cg/core/config_element.h"
#include "cg/core/snapshot.h"
#include "cg/marks/used_marks_config_static.h"

namespace cg::marks {

class IUsedMarksConfig : public core::ConfigElement {
public:
    [[nodiscard]] virtual bool isUseMarks() const = 0;
    virtual void setIsUseMarks(bool value) = 0;

    [[nodiscard]] virtual bool ignoreCategoriesVisible() const = 0;
    virtual void setIgnoreCategoriesVisible(bool value) = 0;

    [[nodiscard]] virtual bool ignoreMarksVisible() const = 0;
    virtual void setIgnoreMarksVisible(bool value) = 0;

    [[nodiscard]] virtual IUsedMarksConfigStaticPtr getStatic() const = 0;
};

// Ports TUsedMarksConfig (a TConfigDataElementWithStaticBase; the interface derives from
// core::ConfigElement).
class UsedMarksConfig final : public IUsedMarksConfig {
public:
    UsedMarksConfig();
    ~UsedMarksConfig() override;

    [[nodiscard]] bool isUseMarks() const override;
    void setIsUseMarks(bool value) override;

    [[nodiscard]] bool ignoreCategoriesVisible() const override;
    void setIgnoreCategoriesVisible(bool value) override;

    [[nodiscard]] bool ignoreMarksVisible() const override;
    void setIgnoreMarksVisible(bool value) override;

    [[nodiscard]] IUsedMarksConfigStaticPtr getStatic() const override;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;
    void doBeforeChangeNotify() override; // rebuilds the snapshot

private:
    bool isUseMarks_ = true;
    bool ignoreMarksVisible_ = false;
    bool ignoreCategoriesVisible_ = false;
    core::Snapshot<IUsedMarksConfigStatic> snapshot_;
};

} // namespace cg::marks
