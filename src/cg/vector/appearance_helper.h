// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_AppearanceHelper.pas (IColorSetHelper, IIntegerSetHelper, IIconSetHelper,
// IAppearanceHelper) and Src/Vector/u_AppearanceHelper.pas (TAppearanceHelper).
//
// T-0142 (spec vector.md §2.14). Used only by the XML import path (i_XmlVectorObjects.pas). Mutable,
// single-threaded, one instance per parse. The three concrete set helpers (TColorSetHelper,
// TIntegerSetHelper, TIconSetHelper) stay private to appearance_helper.cpp. The Redefine functions
// reproduce the legacy force-flag pairing, including LV-1 (line and polygon force flags).

#include "cg/core/color32.h"
#include "cg/vector/appearance.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/import_config.h"
#include "cg/vector/mark_picture.h"

#include <QString>
#include <QtGlobal>

#include <memory>

namespace cg::vector {

class IColorSetHelper {
public:
    virtual ~IColorSetHelper() = default;
    virtual void reset() = 0;
    [[nodiscard]] virtual core::Color32 color() const = 0;
    virtual void setColor(core::Color32 value) = 0;
    [[nodiscard]] virtual bool found() const = 0;
    virtual bool setGpxColorName(const QString& name) = 0;
    virtual void setKmlColorValue(quint32 value) = 0;
};

class IIntegerSetHelper {
public:
    virtual ~IIntegerSetHelper() = default;
    virtual void reset() = 0;
    [[nodiscard]] virtual int value() const = 0;
    virtual void setValue(int value) = 0;
    [[nodiscard]] virtual bool found() const = 0;
};

class IIconSetHelper {
public:
    virtual ~IIconSetHelper() = default;
    virtual void reset() = 0;
    [[nodiscard]] virtual MarkPicturePtr icon() const = 0;
    virtual void setIcon(const MarkPicturePtr& icon) = 0;
    [[nodiscard]] virtual bool found() const = 0;
    virtual bool setByName(const QString& name) = 0;
};

class IAppearanceHelper {
public:
    virtual ~IAppearanceHelper() = default;
    virtual void reset() = 0;

    [[nodiscard]] virtual IColorSetHelper& textColor() = 0;
    [[nodiscard]] virtual IColorSetHelper& lineColor() = 0;
    [[nodiscard]] virtual IColorSetHelper& fillColor() = 0;
    [[nodiscard]] virtual IIntegerSetHelper& lineWidth() = 0;
    [[nodiscard]] virtual IIntegerSetHelper& textSize() = 0;
    [[nodiscard]] virtual IIntegerSetHelper& iconSize() = 0;
    [[nodiscard]] virtual IIconSetHelper& icon() = 0;

    [[nodiscard]] virtual bool hasPointAppearance() const = 0;
    [[nodiscard]] virtual bool hasLineAppearance() const = 0;
    [[nodiscard]] virtual bool hasPolygonAppearance() const = 0;

    // Null when the corresponding params object is null.
    [[nodiscard]] virtual AppearancePtr redefinePointAppearance() const = 0;
    [[nodiscard]] virtual AppearancePtr redefineLineAppearance() const = 0;
    [[nodiscard]] virtual AppearancePtr redefinePolygonAppearance() const = 0;
};

class AppearanceHelper final : public IAppearanceHelper { // TAppearanceHelper
public:
    // Legacy Assert(Assigned(factory)) is off in shipped builds: a precondition, not a guarded case.
    AppearanceHelper(ImportPointParamsPtr pointParams, ImportLineParamsPtr lineParams,
                     ImportPolyParamsPtr polyParams, MarkPictureListPtr markPictureList,
                     AppearanceOfMarkFactoryPtr appearanceOfMarkFactory);

    void reset() override;

    [[nodiscard]] IColorSetHelper& textColor() override;
    [[nodiscard]] IColorSetHelper& lineColor() override;
    [[nodiscard]] IColorSetHelper& fillColor() override;
    [[nodiscard]] IIntegerSetHelper& lineWidth() override;
    [[nodiscard]] IIntegerSetHelper& textSize() override;
    [[nodiscard]] IIntegerSetHelper& iconSize() override;
    [[nodiscard]] IIconSetHelper& icon() override;

    [[nodiscard]] bool hasPointAppearance() const override;
    [[nodiscard]] bool hasLineAppearance() const override;
    [[nodiscard]] bool hasPolygonAppearance() const override;

    [[nodiscard]] AppearancePtr redefinePointAppearance() const override;
    [[nodiscard]] AppearancePtr redefineLineAppearance() const override;
    [[nodiscard]] AppearancePtr redefinePolygonAppearance() const override;

private:
    ImportPointParamsPtr pointParams_;
    ImportLineParamsPtr lineParams_;
    ImportPolyParamsPtr polyParams_;
    AppearanceOfMarkFactoryPtr appearanceOfMarkFactory_;

    std::unique_ptr<IColorSetHelper> textColor_;
    std::unique_ptr<IColorSetHelper> lineColor_;
    std::unique_ptr<IColorSetHelper> fillColor_;
    std::unique_ptr<IIntegerSetHelper> lineWidth_;
    std::unique_ptr<IIntegerSetHelper> textSize_;
    std::unique_ptr<IIntegerSetHelper> iconSize_;
    std::unique_ptr<IIconSetHelper> icon_;
};

} // namespace cg::vector
