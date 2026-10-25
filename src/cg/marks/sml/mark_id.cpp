// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/MarksDB/SML/u_MarkId.pas (315). Line references in the comments are to that unit.
#include "cg/marks/sml/mark_id.h"

#include "cg/marks/sml/mark_category_factory_db_internal.h"

#include <utility>

namespace cg::marks {

namespace {

// :123-127: the category id is taken from an IMarkCategorySMLInternal, otherwise it is CNotExistCategoryID
// (:122).
int categoryIdOf(const vector::CategoryPtr& category) {
    const auto* internal = dynamic_cast<const IMarkCategorySMLInternal*>(category.get());
    return internal != nullptr ? internal->id() : kNotExistCategoryId;
}

} // namespace

MarkId::MarkId(vector::HtmlToHintTextConverterPtr hintConverter, core::HashValue hash, MarkIdType type,
               QString name, QString desc, int id, quintptr dbId, int multiGeometryCount,
               vector::CategoryPtr category, bool visible)
    : hintConverter_(std::move(hintConverter)), hash_(hash), type_(type), name_(std::move(name)),
      desc_(std::move(desc)), id_(id), dbId_(dbId), multiGeometryCount_(multiGeometryCount),
      category_(std::move(category)), categoryId_(categoryIdOf(category_)), visible_(visible) {}

// :131-134
vector::CategoryPtr MarkId::category() const {
    return category_;
}

// :199-202
QString MarkId::name() const {
    return name_;
}

// :194-197
int MarkId::multiGeometryCount() const {
    return multiGeometryCount_;
}

// :189-192
MarkIdType MarkId::markType() const {
    return type_;
}

// :239-249. The id and the database are both compared; the legacy Supports fails for a mark of another class.
bool MarkId::isSameId(const IMarkId* markId) const {
    const auto* internal = dynamic_cast<const IMarkSMLInternal*>(markId);
    return internal != nullptr && id_ == internal->id() && dbId_ == internal->dbId();
}

// :251-263. A nil mark is False (:255-258); a main info of another class is False (:260).
bool MarkId::isSameMark(const vector::IVectorDataItem* mark) const {
    if (mark == nullptr) {
        return false;
    }
    const auto mainInfo = mark->mainInfo();
    const auto* internal = dynamic_cast<const IMarkSMLInternal*>(mainInfo.get());
    return internal != nullptr && id_ == internal->id() && dbId_ == internal->dbId();
}

// :161-164
int MarkId::id() const {
    return id_;
}

// :141-144
quintptr MarkId::dbId() const {
    return dbId_;
}

// :136-139
int MarkId::categoryId() const {
    return categoryId_;
}

// :209-212
bool MarkId::visible() const {
    return visible_.load();
}

// :310-313
void MarkId::setVisible(bool value) const {
    visible_.store(value);
}

// :151-154
core::HashValue MarkId::hash() const noexcept {
    return hash_;
}

// :146-149
QString MarkId::desc() const {
    return desc_;
}

// :265-308. Asymmetric with TMarkMainInfo::IsEqual (u_MarkFullBase.pas:154-157): an item without
// IVectorDataItemWithCategory matches this mark only when this mark has no category (:301-306).
// The category test calls ICategory.IsSame of this mark's category (:291), which compares the id only
// (L-193).
bool MarkId::isEqual(const vector::IVectorDataItemMainInfo* item) const {
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

// :156-159. Callers pass a non-null converter (see mark_id.h).
QString MarkId::hintText() const {
    return hintConverter_->convert(name_, desc_);
}

// :181-187. GetStringID is never empty (IntToStr), so the legacy test at :184 is always true and not ported.
QString MarkId::infoUrl() const {
    return cMarksSystemInternalURL.toString() + stringId() + QLatin1Char('/');
}

// :171-179
QString MarkId::infoHtml() const {
    if (desc_.isEmpty()) {
        return {};
    }
    return QStringLiteral("<HTML><BODY>") + desc_ + QStringLiteral("</BODY></HTML>");
}

// :166-169
QString MarkId::infoCaption() const {
    return name_;
}

// :204-207
QString MarkId::stringId() const {
    return QString::number(id_);
}

// :214-237. No caller in legacy Src (grep of IsEqualInternal: declaration :50, definition :214).
bool MarkId::isEqualInternal(const IMarkSMLInternal& markInternal) const {
    return dbId_ == markInternal.dbId() && categoryId_ == markInternal.categoryId() &&
           id_ == markInternal.id() && visible() == markInternal.visible() && name_ == markInternal.name();
}

} // namespace cg::marks
