// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/MarksDB/mORMot/Marks/u_MarkIdORM.pas (322). Line references in the comments are to that unit.
#include "cg/marks/mormot/marks/mark_id_orm.h"

#include <utility>

namespace cg::marks {

namespace {

// :130-134: the category id is taken from an IMarkCategoryInternalORM, otherwise it stays 0.
qint64 categoryIdOf(const vector::CategoryPtr& category) {
    const auto* internal = dynamic_cast<const IMarkCategoryInternalORM*>(category.get());
    return internal != nullptr ? internal->id() : 0;
}

} // namespace

MarkIdORM::MarkIdORM(vector::HtmlToHintTextConverterPtr hintConverter, core::HashValue hash, MarkIdType type,
                     QString name, QString desc, qint64 id, quintptr dbId, int multiGeometryCount,
                     vector::CategoryPtr category, bool visible)
    : hintConverter_(std::move(hintConverter)), hash_(hash), type_(type), name_(std::move(name)),
      desc_(std::move(desc)), id_(id), dbId_(dbId), multiGeometryCount_(multiGeometryCount),
      category_(std::move(category)), categoryId_(categoryIdOf(category_)), visible_(visible) {}

vector::CategoryPtr MarkIdORM::category() const {
    return category_;
}

QString MarkIdORM::name() const {
    return name_;
}

int MarkIdORM::multiGeometryCount() const {
    return multiGeometryCount_;
}

MarkIdType MarkIdORM::markType() const {
    return type_;
}

// :246-256
bool MarkIdORM::isSameId(const IMarkId* markId) const {
    const auto* internal = dynamic_cast<const IMarkInternalORM*>(markId);
    return internal != nullptr && id_ == internal->id() && dbId_ == internal->dbId();
}

// :258-270
bool MarkIdORM::isSameMark(const vector::IVectorDataItem* mark) const {
    if (mark == nullptr) {
        return false;
    }
    const auto mainInfo = mark->mainInfo();
    const auto* internal = dynamic_cast<const IMarkInternalORM*>(mainInfo.get());
    return internal != nullptr && id_ == internal->id() && dbId_ == internal->dbId();
}

qint64 MarkIdORM::id() const {
    return id_;
}

quintptr MarkIdORM::dbId() const {
    return dbId_;
}

qint64 MarkIdORM::categoryId() const {
    return categoryId_;
}

bool MarkIdORM::visible() const {
    return visible_.load();
}

void MarkIdORM::setVisible(bool value) const {
    visible_.store(value);
}

core::HashValue MarkIdORM::hash() const noexcept {
    return hash_;
}

QString MarkIdORM::desc() const {
    return desc_;
}

// :272-315. Asymmetric with TMarkMainInfo::IsEqual (u_MarkFullBase.pas:154-157): an item without
// IVectorDataItemWithCategory matches this mark only when this mark has no category (:308-313).
bool MarkIdORM::isEqual(const vector::IVectorDataItemMainInfo* item) const {
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
        return category_ == nullptr;
    }
    const auto otherCategory = withCategory->category();
    if (category_ != nullptr) {
        return category_->isSame(otherCategory.get());
    }
    return otherCategory == nullptr;
}

QString MarkIdORM::hintText() const {
    return hintConverter_->convert(name_, desc_);
}

// :188-194; the id is never empty, so the result is always the prefixed URL.
QString MarkIdORM::infoUrl() const {
    return cMarksSystemInternalURL.toString() + QString::number(id_) + QLatin1Char('/');
}

// :178-186
QString MarkIdORM::infoHtml() const {
    if (desc_.isEmpty()) {
        return {};
    }
    return QStringLiteral("<HTML><BODY>") + desc_ + QStringLiteral("</BODY></HTML>");
}

QString MarkIdORM::infoCaption() const {
    return name_;
}

// :221-244. No caller in legacy Src (grep of IsEqualInternal: declaration :57, definition :221).
bool MarkIdORM::isEqualInternal(const IMarkInternalORM& markInternal) const {
    return dbId_ == markInternal.dbId() && categoryId_ == markInternal.categoryId() &&
           id_ == markInternal.id() && visible() == markInternal.visible() && name_ == markInternal.name();
}

} // namespace cg::marks
