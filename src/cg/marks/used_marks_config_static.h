// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_UsedMarksConfig.pas (IUsedMarksConfigStatic), u_UsedMarksConfigStatic.pas (TUsedMarksConfigStatic)
//
// The immutable snapshot of the used-marks settings: the three flags that decide which marks a marks layer
// shows (spec marks.md section 2.11.5). UsedMarksConfig rebuilds it before every change notification and
// publishes it through core::Snapshot, so readers take it without the element's lock.

#include <memory>

namespace cg::marks {

class IUsedMarksConfigStatic {
public:
    virtual ~IUsedMarksConfigStatic() = default;

    [[nodiscard]] virtual bool isUseMarks() const = 0;
    [[nodiscard]] virtual bool ignoreCategoriesVisible() const = 0;
    [[nodiscard]] virtual bool ignoreMarksVisible() const = 0;
};

using IUsedMarksConfigStaticPtr = std::shared_ptr<const IUsedMarksConfigStatic>;

// Ports TUsedMarksConfigStatic. The constructor takes the flags in legacy constructor order (isUseMarks,
// ignoreMarksVisible, ignoreCategoriesVisible), which is not the order of the getters or of the INI keys.
class UsedMarksConfigStatic final : public IUsedMarksConfigStatic {
public:
    UsedMarksConfigStatic(bool isUseMarks, bool ignoreMarksVisible, bool ignoreCategoriesVisible);
    ~UsedMarksConfigStatic() override;

    [[nodiscard]] bool isUseMarks() const override;
    [[nodiscard]] bool ignoreCategoriesVisible() const override;
    [[nodiscard]] bool ignoreMarksVisible() const override;

private:
    bool isUseMarks_;
    bool ignoreMarksVisible_;
    bool ignoreCategoriesVisible_;
};

} // namespace cg::marks
