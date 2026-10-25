// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/vector_data_item_base.h"

#include <QtGlobal>

#include <utility>

namespace cg::vector {

// --- VectorDataItemMainInfo ----------------------------------------------------------------

VectorDataItemMainInfo::VectorDataItemMainInfo(core::HashValue hash, HtmlToHintTextConverterPtr hintConverter,
                                               QString name, QString desc)
    : hash_(hash), hintConverter_(std::move(hintConverter)), name_(std::move(name)), desc_(std::move(desc)) {}

core::HashValue VectorDataItemMainInfo::hash() const noexcept {
    return hash_;
}

QString VectorDataItemMainInfo::name() const {
    return name_;
}

QString VectorDataItemMainInfo::desc() const {
    return desc_;
}

QString VectorDataItemMainInfo::hintText() const {
    QString result = hintConverter_->convert(name_, QString());
    if (result.isEmpty()) {
        result = hintConverter_->convert(name_, desc_);
    }
    return result;
}

QString VectorDataItemMainInfo::infoCaption() const {
    return name_;
}

QString VectorDataItemMainInfo::infoHtml() const {
    if (desc_.isEmpty()) {
        return {};
    }
    return QStringLiteral("<HTML><BODY>") + desc_ + QStringLiteral("</BODY></HTML>");
}

QString VectorDataItemMainInfo::infoUrl() const {
    return {};
}

bool VectorDataItemMainInfo::isEqual(const IVectorDataItemMainInfo* item) const {
    if (item == nullptr) {
        return false;
    }
    if (item == this) {
        return true;
    }
    if (item->hash() != 0 && hash_ != 0 && item->hash() != hash_) {
        return false;
    }
    if (name_ != item->name()) {
        return false;
    }
    return desc_ == item->desc();
}

// --- VectorDataItem ------------------------------------------------------------------------

VectorDataItem::VectorDataItem(core::HashValue hash, AppearancePtr appearance,
                               VectorDataItemMainInfoPtr mainInfo, geo::GeometryLonLatPtr geometry)
    : hash_(hash), geometry_(std::move(geometry)), mainInfo_(std::move(mainInfo)),
      appearance_(std::move(appearance)) {
    Q_ASSERT(mainInfo_ != nullptr);
    Q_ASSERT(geometry_ != nullptr);
}

core::HashValue VectorDataItem::hash() const noexcept {
    return hash_;
}

VectorDataItemMainInfoPtr VectorDataItem::mainInfo() const {
    return mainInfo_;
}

QString VectorDataItem::name() const {
    return mainInfo_->name();
}

QString VectorDataItem::desc() const {
    return mainInfo_->desc();
}

geo::GeometryLonLatPtr VectorDataItem::geometry() const {
    return geometry_;
}

AppearancePtr VectorDataItem::appearance() const {
    return appearance_;
}

QString VectorDataItem::hintText() const {
    return mainInfo_->hintText();
}

QString VectorDataItem::infoUrl() const {
    return mainInfo_->infoUrl();
}

QString VectorDataItem::infoCaption() const {
    return mainInfo_->infoCaption();
}

QString VectorDataItem::infoHtml() const {
    return mainInfo_->infoHtml();
}

bool VectorDataItem::isEqual(const IVectorDataItem* item) const {
    if (item == nullptr) {
        return false;
    }
    if (item == this) {
        return true;
    }
    if (item->hash() != 0 && hash_ != 0 && item->hash() != hash_) {
        return false;
    }
    if (appearance_) {
        if (!appearance_->isEqual(item->appearance().get())) {
            return false;
        }
    } else if (item->appearance()) {
        return false;
    }
    if (!mainInfo_->isEqual(item->mainInfo().get())) {
        return false;
    }
    const auto otherGeometry = item->geometry();
    return otherGeometry && geometry_->isSameGeometry(*otherGeometry);
}

} // namespace cg::vector
