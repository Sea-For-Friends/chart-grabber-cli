// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Vector/u_VectorItemTreeMarksDb.pas
#include "cg/marks/vector_item_tree_marks_db.h"

#include "cg/marks/c_mark_system.h"
#include "cg/marks/mark_system_helpers.h"
#include "cg/marks/mormot/mark_system_impl_config_orm.h"
#include "cg/marks/sml/mark_system_impl_config_sml.h"
#include "cg/vector/category.h"

#include <QString>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

namespace cg::marks {

namespace {

// ExtractFilePath (:172): the text up to and including the last delimiter, empty when there is none
// (lastIndexOf -1 gives left(0)). Delphi's ':' delimiter is not treated as one (D-123).
QString extractFilePath(const QString& fileName) {
    const qsizetype sep = std::max(fileName.lastIndexOf(u'/'), fileName.lastIndexOf(u'\\'));
    return fileName.left(sep + 1);
}

// WriteAccess / ReadAccess of the impl state (:187, :211). unreachable: no landed impl returns null; no
// register row.
bool hasAccess(const IMarkSystemImpl& impl, bool write) {
    const auto state = impl.state();
    if (!state) {
        return false;
    }
    const auto snapshot = state->getStatic();
    if (!snapshot) {
        return false;
    }
    return write ? snapshot->writeAccess : snapshot->readAccess;
}

} // namespace

VectorItemTreeMarksDb::VectorItemTreeMarksDb(const QUuid& database, std::shared_ptr<IMarkFactory> markFactory,
                                             std::shared_ptr<IMarkCategoryFactory> categoryFactory,
                                             vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                                             MarkSystemImplFactoryListStaticPtr implFactoryList)
    : database_(database), markFactory_(std::move(markFactory)), categoryFactory_(std::move(categoryFactory)),
      appearanceOfMarkFactory_(std::move(appearanceOfMarkFactory)),
      implFactoryList_(std::move(implFactoryList)) {
    if (!markFactory_ || !categoryFactory_ || !appearanceOfMarkFactory_ || !implFactoryList_) {
        throw std::invalid_argument("VectorItemTreeMarksDb: null factory or factory list");
    }
}

// _GetConfigForExporter (:116-139). Built anew on every export.
vector::ImportConfigPtr VectorItemTreeMarksDb::getConfigForExporter() const {
    return std::make_shared<vector::ImportConfig>(
        std::make_shared<vector::Category>(QString()),
        std::make_shared<vector::ImportCategoryParams>(true, false, false, false), // ToDo (legacy)
        std::make_shared<vector::ImportPointParams>(
            appearanceOfMarkFactory_->createPointAppearance(0, 0, 0, QString(), nullptr, 0), false, false,
            false, false, false),
        std::make_shared<vector::ImportLineParams>(appearanceOfMarkFactory_->createLineAppearance(0, 0),
                                                   false, false),
        std::make_shared<vector::ImportPolyParams>(appearanceOfMarkFactory_->createPolygonAppearance(0, 0, 0),
                                                   false, false, false));
}

// _GetImpl (:141-175). The config is built by the GUID first, then the list is asked.
std::shared_ptr<IMarkSystemImpl> VectorItemTreeMarksDb::getImpl(std::stop_token stop, const QString& fileName,
                                                                bool isReadOnly) const {
    MarkSystemImplConfigStaticPtr config;
    if (database_ == cSMLMarksDbGUID) {
        config = std::make_shared<MarkSystemImplConfigSML>(fileName, isReadOnly);
    } else if (database_ == cORMSQLiteMarksDbGUID) {
        config =
            std::make_shared<MarkSystemImplConfigORM>(fileName, isReadOnly, QString(), QString(), QString());
    } else {
        return nullptr; // legacy Assert(False); Exit: no-op in the shipped build (SASPlanet.dproj:92)
    }

    const auto element = implFactoryList_->get(database_);
    if (!element) {
        return nullptr; // legacy Assert(False); Exit (:163-166)
    }
    const auto factory = element->factory();
    if (!factory) {
        return nullptr; // unreachable: no landed impl returns null; no register row
    }
    return factory->build(std::move(stop), extractFilePath(fileName), config);
}

// ProcessExport (:177-197)
core::Result<void> VectorItemTreeMarksDb::processExport(std::stop_token stop, const QString& fileName,
                                                        const vector::VectorItemTreePtr& tree) const {
    std::shared_ptr<IMarkSystemImpl> impl;
    try {
        impl = getImpl(stop, fileName, false);
    } catch (const std::exception& e) {
        // The factory's build throws when the database cannot be opened (i_MarkSystemImplFactory.h).
        return core::fail(core::ErrorCode::IoError, QString::fromUtf8(e.what()));
    }
    if (!impl || !hasAccess(*impl, true)) {
        return {}; // legacy: nothing happens and nothing is reported (:187)
    }
    const auto markDb = impl->markDb();
    const auto categoryDb = impl->categoryDB();
    if (!markDb || !categoryDb) {
        return {}; // unreachable: no landed impl returns null; no register row
    }
    static_cast<void>(importItemsTree(tree, getConfigForExporter(), *markDb, *markFactory_, *categoryDb,
                                      *categoryFactory_));
    return {};
}

// ProcessImport (:199-221). The config argument is never read in legacy.
core::Result<vector::VectorItemTreePtr>
VectorItemTreeMarksDb::processImport(std::stop_token stop, const QString& fileName,
                                     const vector::ImportConfigPtr& /*config*/) const {
    std::shared_ptr<IMarkSystemImpl> impl;
    try {
        impl = getImpl(stop, fileName, true);
    } catch (const std::exception& e) {
        return core::fail(core::ErrorCode::IoError, QString::fromUtf8(e.what()));
    }
    if (!impl || !hasAccess(*impl, false)) {
        return vector::VectorItemTreePtr{}; // legacy: null tree (:211)
    }
    const auto markDb = impl->markDb();
    const auto categoryDb = impl->categoryDB();
    if (!markDb || !categoryDb) {
        return vector::VectorItemTreePtr{}; // unreachable: no landed impl returns null; no register row
    }
    const MarkCategoryListPtr categories = categoryDb->categoriesList();
    if (!categories) {
        return vector::VectorItemTreePtr{}; // legacy: Assigned(VCategoiesList) (:213)
    }
    return categoryTreeToMarkTreeHelper(*markDb, categoryListToCategoryTree(categories),
                                        true /*IncludeHiddenMarks*/);
}

} // namespace cg::marks
