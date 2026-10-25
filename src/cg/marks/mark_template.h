// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkTemplate.pas (interfaces, shared), u_MarkTemplates.pas (the implementations, T-0091:
// MarkTemplateCommon = TMarkTemplateBase, MarkTemplatePoint, MarkTemplateLine, MarkTemplatePoly).

#include "cg/marks/mark_name_generator.h"
#include "cg/vector/appearance.h"
#include "cg/vector/appearance_of_vector_item.h"
#include "cg/vector/category.h"

#include <QString>

#include <memory>

namespace cg::marks {

class IMarkTemplate {
public:
    virtual ~IMarkTemplate() = default;
    [[nodiscard]] virtual QString newName() const = 0; // calls the name generator: has a side effect
    [[nodiscard]] virtual vector::AppearancePtr appearance() const = 0;
    [[nodiscard]] virtual vector::CategoryPtr category() const = 0;
};
using MarkTemplatePtr = std::shared_ptr<const IMarkTemplate>;

class IMarkTemplatePoint;
using MarkTemplatePointPtr = std::shared_ptr<const IMarkTemplatePoint>;
class IMarkTemplatePoint : public IMarkTemplate {
public:
    [[nodiscard]] virtual vector::AppearancePointCaptionPtr captionAppearance() const = 0;
    [[nodiscard]] virtual vector::AppearancePointIconPtr iconAppearance() const = 0;
    [[nodiscard]] virtual bool isSame(const IMarkTemplatePoint* value) const = 0;
};

class IMarkTemplateLine;
using MarkTemplateLinePtr = std::shared_ptr<const IMarkTemplateLine>;
class IMarkTemplateLine : public IMarkTemplate {
public:
    [[nodiscard]] virtual vector::AppearanceLinePtr lineAppearance() const = 0;
    [[nodiscard]] virtual bool isSame(const IMarkTemplateLine* value) const = 0;
};

class IMarkTemplatePoly;
using MarkTemplatePolyPtr = std::shared_ptr<const IMarkTemplatePoly>;
class IMarkTemplatePoly : public IMarkTemplate {
public:
    [[nodiscard]] virtual vector::AppearancePolygonBorderPtr borderAppearance() const = 0;
    [[nodiscard]] virtual vector::AppearancePolygonFillPtr fillAppearance() const = 0;
    [[nodiscard]] virtual bool isSame(const IMarkTemplatePoly* value) const = 0;
};

// Ports TMarkTemplateBase (u_MarkTemplates.pas:35-51, :108-154): the state the three kinds share. Not an
// interface: each concrete template holds one (C++ cannot inherit IMarkTemplate three times without a
// diamond). Constructor order follows the legacy base: name generator, appearance, category.
class MarkTemplateCommon {
public:
    // A nil name generator or appearance is refused (legacy asserts, disabled in the shipped build).
    MarkTemplateCommon(std::shared_ptr<IMarkNameGenerator> nameGenerator, vector::AppearancePtr app,
                       vector::CategoryPtr cat);

    [[nodiscard]] QString newName() const;
    [[nodiscard]] vector::AppearancePtr appearance() const { return appearance_; }
    [[nodiscard]] vector::CategoryPtr category() const { return category_; }
    // u_MarkTemplates.pas:138-154. A nil argument gives false (legacy: access violation, no handler).
    [[nodiscard]] bool isSameInternal(const IMarkTemplate* value) const;

private:
    std::shared_ptr<IMarkNameGenerator> nameGenerator_;
    vector::AppearancePtr appearance_;
    vector::CategoryPtr category_; // nil is legal: it is compared as "no category"
};

// Ports TMarkTemplatePoint (u_MarkTemplates.pas:53-68, :158-184). Facets that the appearance does not
// support are nil (legacy Supports with assertions off).
class MarkTemplatePoint final : public IMarkTemplatePoint {
public:
    MarkTemplatePoint(std::shared_ptr<IMarkNameGenerator> nameGenerator, vector::CategoryPtr cat,
                      vector::AppearancePtr app);

    [[nodiscard]] QString newName() const override;
    [[nodiscard]] vector::AppearancePtr appearance() const override;
    [[nodiscard]] vector::CategoryPtr category() const override;
    [[nodiscard]] vector::AppearancePointCaptionPtr captionAppearance() const override;
    [[nodiscard]] vector::AppearancePointIconPtr iconAppearance() const override;
    [[nodiscard]] bool isSame(const IMarkTemplatePoint* value) const override;

private:
    MarkTemplateCommon common_;
    vector::AppearancePointCaptionPtr caption_;
    vector::AppearancePointIconPtr icon_;
};

// Ports TMarkTemplateLine (u_MarkTemplates.pas:70-82, :186-207).
class MarkTemplateLine final : public IMarkTemplateLine {
public:
    MarkTemplateLine(std::shared_ptr<IMarkNameGenerator> nameGenerator, vector::CategoryPtr cat,
                     vector::AppearancePtr app);

    [[nodiscard]] QString newName() const override;
    [[nodiscard]] vector::AppearancePtr appearance() const override;
    [[nodiscard]] vector::CategoryPtr category() const override;
    [[nodiscard]] vector::AppearanceLinePtr lineAppearance() const override;
    [[nodiscard]] bool isSame(const IMarkTemplateLine* value) const override;

private:
    MarkTemplateCommon common_;
    vector::AppearanceLinePtr line_;
};

// Ports TMarkTemplatePoly (u_MarkTemplates.pas:84-99, :209-237).
class MarkTemplatePoly final : public IMarkTemplatePoly {
public:
    MarkTemplatePoly(std::shared_ptr<IMarkNameGenerator> nameGenerator, vector::CategoryPtr cat,
                     vector::AppearancePtr app);

    [[nodiscard]] QString newName() const override;
    [[nodiscard]] vector::AppearancePtr appearance() const override;
    [[nodiscard]] vector::CategoryPtr category() const override;
    [[nodiscard]] vector::AppearancePolygonBorderPtr borderAppearance() const override;
    [[nodiscard]] vector::AppearancePolygonFillPtr fillAppearance() const override;
    [[nodiscard]] bool isSame(const IMarkTemplatePoly* value) const override;

private:
    MarkTemplateCommon common_;
    vector::AppearancePolygonBorderPtr border_;
    vector::AppearancePolygonFillPtr fill_;
};

} // namespace cg::marks
