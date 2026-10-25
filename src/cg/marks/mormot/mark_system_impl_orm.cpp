// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/mark_system_impl_orm.h"

#include "cg/core/delphi_text.h"
#include "cg/marks/mormot/categories/mark_category_internal_orm.h"
#include "cg/marks/mormot/mark_system_impl_config_orm.h"
#include "cg/marks/mormot/mark_system_orm_types.h"
#include "cg/marks/mormot/marks/mark_db_internal_orm.h"

#include <utility>

namespace cg::marks {

// :110-198
MarkSystemImplORM::MarkSystemImplORM(const std::stop_token& /*stop*/, const QString& basePath,
                                     vector::MarkPictureListPtr markPictureList,
                                     vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                                     const geo::GeometryLonLatFactory& geometryFactory,
                                     vector::VectorItemSubsetBuilderFactoryPtr subsetBuilderFactory,
                                     std::shared_ptr<const IMarkFactory> markFactory,
                                     vector::HtmlToHintTextConverterPtr hintConverter,
                                     const MarkSystemImplConfigStaticPtr& implConfig)
    : dbId_(reinterpret_cast<quintptr>(this)), geometryFactory_(geometryFactory) { // :138
    // :140-142 (Supports: a null config is not an IMarkSystemImplConfigORM either)
    const auto* config = dynamic_cast<const IMarkSystemImplConfigORM*>(implConfig.get());
    if (config == nullptr) {
        throw MarkSystemORMError("MarkSystemORM: Unknown Impl config interface!");
    }

    state_ = std::make_shared<core::ReadWriteStateInternal>(true, !config->isReadOnly()); // :144

    clientProvider_ = std::make_unique<MarkSystemImplORMClientProvider>(basePath, *config); // :148-153

    categoryDb_ = std::make_shared<MarkCategoryDbImplORM>(dbId_, state_, *clientProvider_); // :155-163

    factoryDbInternal_ = std::make_shared<MarkFactoryDbInternalORM>( // :165-174
        dbId_, std::move(markPictureList), std::move(appearanceOfMarkFactory), std::move(markFactory),
        std::move(hintConverter), categoryDb_);

    // :176-197; the four geometry readers / writers are the one geometry factory (mark_db_impl_orm.h)
    markDb_ = std::make_shared<MarkDbImplORM>(dbId_, config->cacheSizeMb(), state_, *clientProvider_,
                                              factoryDbInternal_, geometryFactory_,
                                              std::move(subsetBuilderFactory));
}

// :200-209: the legacy order, the provider last
MarkSystemImplORM::~MarkSystemImplORM() {
    markDb_.reset();
    categoryDb_.reset();
    factoryDbInternal_.reset();
    clientProvider_.reset();
}

// :211-214
std::shared_ptr<IMarkCategoryDBImpl> MarkSystemImplORM::categoryDB() const {
    return categoryDb_;
}

// :246-249
std::shared_ptr<IMarkDbImpl> MarkSystemImplORM::markDb() const {
    return markDb_;
}

// :251-254
core::ReadWriteStateChangeablePtr MarkSystemImplORM::state() const {
    return state_;
}

// :216-229
vector::VectorDataItemPtr MarkSystemImplORM::markByStringId(const QString& id) const {
    qint64 markId = 0;
    if (!id.isEmpty() && markDb_ && core::delphiTryStrToInt64(id, markId)) {
        return markDb_->getById(markId);
    }
    return nullptr;
}

// :231-244
MarkCategoryPtr MarkSystemImplORM::markCategoryByStringId(const QString& id) const {
    qint64 categoryId = 0;
    if (!id.isEmpty() && categoryDb_ && core::delphiTryStrToInt64(id, categoryId)) {
        return categoryDb_->getCategoryById(categoryId);
    }
    return nullptr;
}

// :256-264
QString MarkSystemImplORM::stringIdByMark(const vector::VectorDataItemPtr& mark) const {
    if (mark) {
        const vector::VectorDataItemMainInfoPtr mainInfo = mark->mainInfo();
        if (const auto* internal = dynamic_cast<const IMarkInternalORM*>(mainInfo.get())) {
            return QString::number(internal->id());
        }
    }
    return {};
}

} // namespace cg::marks
