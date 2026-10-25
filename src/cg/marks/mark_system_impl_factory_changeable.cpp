// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_system_impl_factory_changeable.h"

#include "cg/marks/c_mark_system.h"
#include "cg/marks/mormot/mark_system_impl_factory_orm.h"

#include <QString>

#include <utility>

namespace cg::marks {

namespace {

// TMarkSystemImplFactoryListElement (:70-115)
class MarkSystemImplFactoryListElement final : public IMarkSystemImplFactoryListElement {
public:
    MarkSystemImplFactoryListElement(const QUuid& guid, QString caption,
                                     std::shared_ptr<IMarkSystemImplFactory> factory)
        : guid_(guid), caption_(std::move(caption)), factory_(std::move(factory)) {}

    [[nodiscard]] QUuid guid() const override { return guid_; }         // :112-115
    [[nodiscard]] QString caption() const override { return caption_; } // :102-105
    [[nodiscard]] std::shared_ptr<IMarkSystemImplFactory> factory() const override {
        return factory_;
    } // :107-110

private:
    QUuid guid_;
    QString caption_;
    std::shared_ptr<IMarkSystemImplFactory> factory_;
};

} // namespace

// :119-248
MarkSystemImplFactoryListStatic::MarkSystemImplFactoryListStatic(
    vector::MarkPictureListPtr markPictureList, vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
    const geo::GeometryLonLatFactory& vectorGeometryLonLatFactory,
    vector::VectorItemSubsetBuilderFactoryPtr vectorItemSubsetBuilderFactory,
    std::shared_ptr<const IMarkFactory> markFactory, vector::HtmlToHintTextConverterPtr hintConverter)
    : list_(false) { // :136
    // SQLite3 (:159-179)
    auto factory = std::make_shared<MarkSystemImplFactoryORM>(
        std::move(markPictureList), std::move(appearanceOfMarkFactory), vectorGeometryLonLatFactory,
        std::move(vectorItemSubsetBuilderFactory), std::move(markFactory), std::move(hintConverter));
    auto item = std::make_shared<const MarkSystemImplFactoryListElement>(cORMSQLiteMarksDbGUID,
                                                                         rsORMSQLiteMarksDbName(), factory);
    list_.add(item->guid(), item);
}

// :250-253
MarkSystemImplFactoryListElementPtr MarkSystemImplFactoryListStatic::get(const QUuid& guid) const {
    return list_.getByGuid(guid);
}

// :255-258
std::vector<QUuid> MarkSystemImplFactoryListStatic::guidEnum() const {
    return list_.guids();
}

} // namespace cg::marks
