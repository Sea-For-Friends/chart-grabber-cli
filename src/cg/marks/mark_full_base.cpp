// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_full_base.h"

#include <utility>

namespace cg::marks {

MarkMainInfo::MarkMainInfo(core::HashValue hash, vector::HtmlToHintTextConverterPtr hintConverter,
                           QString name, vector::CategoryPtr category, QString desc)
    : hash_(hash), hintConverter_(std::move(hintConverter)), name_(std::move(name)), desc_(std::move(desc)),
      category_(std::move(category)) {}

core::HashValue MarkMainInfo::hash() const noexcept {
    return hash_;
}

QString MarkMainInfo::name() const {
    return name_;
}

QString MarkMainInfo::desc() const {
    return desc_;
}

vector::CategoryPtr MarkMainInfo::category() const {
    return category_;
}

QString MarkMainInfo::hintText() const {
    return hintConverter_->convert(name_, desc_); // :100-103
}

QString MarkMainInfo::infoCaption() const {
    return name_;
}

QString MarkMainInfo::infoHtml() const {
    if (desc_.isEmpty()) {
        return {};
    }
    return QStringLiteral("<HTML><BODY>") + desc_ + QStringLiteral("</BODY></HTML>");
}

QString MarkMainInfo::infoUrl() const {
    return {};
}

bool MarkMainInfo::isEqual(const vector::IVectorDataItemMainInfo* item) const {
    if (item == nullptr) {
        return false;
    }
    if (item == this) {
        return true;
    }
    if (item->hash() != 0 && hash_ != 0 && item->hash() != hash_) {
        return false;
    }
    if (name_ != item->name() || desc_ != item->desc()) {
        return false;
    }
    const auto* withCategory = dynamic_cast<const vector::IVectorDataItemWithCategory*>(item);
    if (withCategory == nullptr) {
        return false;
    }
    const auto otherCategory = withCategory->category();
    if (category_ != nullptr) {
        return category_->isSame(otherCategory.get());
    }
    return otherCategory == nullptr;
}

} // namespace cg::marks
