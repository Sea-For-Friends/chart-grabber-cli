// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_template.h"

#include <stdexcept>
#include <utility>

// Ports u_MarkTemplates.pas (T-0091). The legacy assertions (Assert(Supports(...)), Assert(Assigned(...)))
// are compiled out in the shipped build (SASPlanet.dproj:92), so a wrong appearance kind is accepted and the
// facet stays nil: the same here. Nil name generator / appearance are refused, see the header.

namespace cg::marks {

MarkTemplateCommon::MarkTemplateCommon(std::shared_ptr<IMarkNameGenerator> nameGenerator,
                                       vector::AppearancePtr app, vector::CategoryPtr cat)
    : nameGenerator_(std::move(nameGenerator)), appearance_(std::move(app)), category_(std::move(cat)) {
    if (nameGenerator_ == nullptr) {
        throw std::invalid_argument("MarkTemplate: nil name generator");
    }
    if (appearance_ == nullptr) {
        throw std::invalid_argument("MarkTemplate: nil appearance");
    }
}

// u_MarkTemplates.pas:133-136.
QString MarkTemplateCommon::newName() const {
    return nameGenerator_->newName();
}

// u_MarkTemplates.pas:138-154. The legacy reads ATemplate.Appearance before its own nil check (:147), so a
// nil argument is an access violation there; here it is "not the same".
bool MarkTemplateCommon::isSameInternal(const IMarkTemplate* value) const {
    if (value == nullptr) {
        return false;
    }
    if (!appearance_->isEqual(value->appearance().get())) {
        return false;
    }
    const vector::CategoryPtr other = value->category();
    if (category_ == nullptr) {
        return other == nullptr;
    }
    return category_->isEqual(other.get());
}

// ---- point ----------------------------------------------------------------------------------------------

MarkTemplatePoint::MarkTemplatePoint(std::shared_ptr<IMarkNameGenerator> nameGenerator,
                                     vector::CategoryPtr cat, vector::AppearancePtr app)
    : common_(std::move(nameGenerator), std::move(app), std::move(cat)),
      caption_(std::dynamic_pointer_cast<const vector::IAppearancePointCaption>(common_.appearance())),
      icon_(std::dynamic_pointer_cast<const vector::IAppearancePointIcon>(common_.appearance())) {}

QString MarkTemplatePoint::newName() const {
    return common_.newName();
}

vector::AppearancePtr MarkTemplatePoint::appearance() const {
    return common_.appearance();
}

vector::CategoryPtr MarkTemplatePoint::category() const {
    return common_.category();
}

vector::AppearancePointCaptionPtr MarkTemplatePoint::captionAppearance() const {
    return caption_;
}

vector::AppearancePointIconPtr MarkTemplatePoint::iconAppearance() const {
    return icon_;
}

bool MarkTemplatePoint::isSame(const IMarkTemplatePoint* value) const {
    return common_.isSameInternal(value);
}

// ---- line -----------------------------------------------------------------------------------------------

MarkTemplateLine::MarkTemplateLine(std::shared_ptr<IMarkNameGenerator> nameGenerator, vector::CategoryPtr cat,
                                   vector::AppearancePtr app)
    : common_(std::move(nameGenerator), std::move(app), std::move(cat)),
      line_(std::dynamic_pointer_cast<const vector::IAppearanceLine>(common_.appearance())) {}

QString MarkTemplateLine::newName() const {
    return common_.newName();
}

vector::AppearancePtr MarkTemplateLine::appearance() const {
    return common_.appearance();
}

vector::CategoryPtr MarkTemplateLine::category() const {
    return common_.category();
}

vector::AppearanceLinePtr MarkTemplateLine::lineAppearance() const {
    return line_;
}

bool MarkTemplateLine::isSame(const IMarkTemplateLine* value) const {
    return common_.isSameInternal(value);
}

// ---- polygon --------------------------------------------------------------------------------------------

MarkTemplatePoly::MarkTemplatePoly(std::shared_ptr<IMarkNameGenerator> nameGenerator, vector::CategoryPtr cat,
                                   vector::AppearancePtr app)
    : common_(std::move(nameGenerator), std::move(app), std::move(cat)),
      border_(std::dynamic_pointer_cast<const vector::IAppearancePolygonBorder>(common_.appearance())),
      fill_(std::dynamic_pointer_cast<const vector::IAppearancePolygonFill>(common_.appearance())) {}

QString MarkTemplatePoly::newName() const {
    return common_.newName();
}

vector::AppearancePtr MarkTemplatePoly::appearance() const {
    return common_.appearance();
}

vector::CategoryPtr MarkTemplatePoly::category() const {
    return common_.category();
}

vector::AppearancePolygonBorderPtr MarkTemplatePoly::borderAppearance() const {
    return border_;
}

vector::AppearancePolygonFillPtr MarkTemplatePoly::fillAppearance() const {
    return fill_;
}

bool MarkTemplatePoly::isSame(const IMarkTemplatePoly* value) const {
    return common_.isSameInternal(value);
}

} // namespace cg::marks
