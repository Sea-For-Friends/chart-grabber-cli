// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkFactory.pas, u_MarkFactory.pas

#include "cg/geo/geometry_lonlat.h"
#include "cg/marks/mark_factory_config.h"
#include "cg/marks/mark_template.h"
#include "cg/vector/appearance.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/category.h"
#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/import_config.h"
#include "cg/vector/mark_picture.h"
#include "cg/vector/vector_data_item_simple.h"

#include <QString>

#include <memory>

namespace cg::marks {

class IMarkFactory {
public:
    virtual ~IMarkFactory() = default;
    [[nodiscard]] virtual vector::VectorDataItemPtr
    createNewMark(const geo::GeometryLonLatPtr& geometry, const QString& name, const QString& desc,
                  const MarkTemplatePtr& tmpl = nullptr) const = 0;
    [[nodiscard]] virtual vector::VectorDataItemPtr
    replaceCategory(const vector::VectorDataItemPtr& mark, const vector::CategoryPtr& category) const = 0;
    [[nodiscard]] virtual vector::VectorDataItemPtr
    createMark(const geo::GeometryLonLatPtr& geometry, const QString& name, const QString& desc,
               const vector::CategoryPtr& category, const vector::AppearancePtr& appearance) const = 0;
    [[nodiscard]] virtual vector::VectorDataItemPtr
    modifyGeometry(const vector::VectorDataItemPtr& source, const geo::GeometryLonLatPtr& geometry) const = 0;
    [[nodiscard]] virtual vector::VectorDataItemPtr modifyName(const vector::VectorDataItemPtr& source,
                                                               const QString& name,
                                                               const QString& desc = {}) const = 0;
    // vector/import_config.h has no alias for the params base: spelled out.
    [[nodiscard]] virtual vector::VectorDataItemPtr
    prepareMark(const vector::VectorDataItemPtr& item, const QString& name,
                const std::shared_ptr<const vector::IImportMarkParams>& params,
                const vector::CategoryPtr& category) const = 0;
    [[nodiscard]] virtual vector::MarkPictureListPtr markPictureList() const = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkFactoryConfig> config() const = 0;
};

// TMarkFactory. Hashes are the core free functions (legacy threaded an IHashFunction through).
class MarkFactory final : public IMarkFactory {
public:
    // Preconditions (legacy: none checked, a null collaborator crashes on first use): all non-null.
    MarkFactory(std::shared_ptr<IMarkFactoryConfig> config, vector::MarkPictureListPtr markPictureList,
                vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                vector::HtmlToHintTextConverterPtr hintConverter);

    // Null (deviation D-75, silent: the sibling vector factory throws): geometry has no template to take (not
    // a point / line / polygon) and no template was given.
    [[nodiscard]] vector::VectorDataItemPtr
    createNewMark(const geo::GeometryLonLatPtr& geometry, const QString& name, const QString& desc,
                  const MarkTemplatePtr& tmpl = nullptr) const override;
    [[nodiscard]] vector::VectorDataItemPtr
    replaceCategory(const vector::VectorDataItemPtr& mark,
                    const vector::CategoryPtr& category) const override;
    // Null when geometry or appearance is null (legacy: Assert, then an access violation).
    [[nodiscard]] vector::VectorDataItemPtr
    createMark(const geo::GeometryLonLatPtr& geometry, const QString& name, const QString& desc,
               const vector::CategoryPtr& category, const vector::AppearancePtr& appearance) const override;
    [[nodiscard]] vector::VectorDataItemPtr
    modifyGeometry(const vector::VectorDataItemPtr& source,
                   const geo::GeometryLonLatPtr& geometry) const override;
    [[nodiscard]] vector::VectorDataItemPtr modifyName(const vector::VectorDataItemPtr& source,
                                                       const QString& name,
                                                       const QString& desc = {}) const override;
    [[nodiscard]] vector::VectorDataItemPtr
    prepareMark(const vector::VectorDataItemPtr& item, const QString& name,
                const std::shared_ptr<const vector::IImportMarkParams>& params,
                const vector::CategoryPtr& category) const override;
    [[nodiscard]] vector::MarkPictureListPtr markPictureList() const override;
    [[nodiscard]] std::shared_ptr<IMarkFactoryConfig> config() const override;

private:
    [[nodiscard]] vector::AppearancePtr prepareAppearance(const vector::AppearancePtr& source,
                                                          const vector::IImportMarkParams& params) const;
    [[nodiscard]] vector::AppearancePtr
    preparePointAppearance(const vector::AppearancePtr& source,
                           const vector::IImportPointParams& params) const;
    [[nodiscard]] vector::AppearancePtr prepareLineAppearance(const vector::AppearancePtr& source,
                                                              const vector::IImportLineParams& params) const;
    [[nodiscard]] vector::AppearancePtr preparePolyAppearance(const vector::AppearancePtr& source,
                                                              const vector::IImportPolyParams& params) const;

    std::shared_ptr<IMarkFactoryConfig> config_;
    vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory_;
    vector::HtmlToHintTextConverterPtr hintConverter_;
    vector::MarkPictureListPtr markPictureList_;
};

} // namespace cg::marks
