// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_VectorItemTreeExporterList.pas, Src/Vector/u_VectorItemTreeExporterList.pas
//
// The exporter list: items (value holders) and the static list. Legacy TExporterListItemOptions is a
// set of three ordinals; here it is QFlags with the port's own bits (1, 2, 4). Not traced: whether the
// ordinals are persisted (the names are used in frm_MarksExport.pas:207-436 and the Simple list only).
// A nil legacy list is an empty vector: count() is 0 (u_VectorItemTreeExporterList.pas:127-131).
// item() outside 0..count()-1 answers null (D row of T-0141).

#include "cg/core/changeable.h"
#include "cg/vector/export_config.h"
#include "cg/vector/vector_item_tree_exporter.h"

#include <QFlags>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <vector>

namespace cg::vector {

// TExporterListItemOption (elioSaveToFile, elioSaveToDir, elioAllowSeparateFiles)
enum class ExporterListItemOption : quint8 { SaveToFile = 1, SaveToDir = 2, AllowSeparateFiles = 4 };
Q_DECLARE_FLAGS(ExporterListItemOptions, ExporterListItemOption)
Q_DECLARE_OPERATORS_FOR_FLAGS(ExporterListItemOptions)

class IVectorItemTreeExporterListItem {
public:
    virtual ~IVectorItemTreeExporterListItem() = default;
    [[nodiscard]] virtual VectorItemTreeExporterPtr exporter() const = 0;
    [[nodiscard]] virtual std::shared_ptr<IExportConfig> config() const = 0; // may be null
    [[nodiscard]] virtual ExporterListItemOptions options() const = 0;
    [[nodiscard]] virtual QString defaultExt() const = 0;
    [[nodiscard]] virtual QString name() const = 0;
};
using VectorItemTreeExporterListItemPtr = std::shared_ptr<const IVectorItemTreeExporterListItem>;

class VectorItemTreeExporterListItem final
    : public IVectorItemTreeExporterListItem { // TVectorItemTreeExporterListItem
public:
    VectorItemTreeExporterListItem(VectorItemTreeExporterPtr exporter, std::shared_ptr<IExportConfig> config,
                                   ExporterListItemOptions options, QString defaultExt, QString name);

    [[nodiscard]] VectorItemTreeExporterPtr exporter() const override;
    [[nodiscard]] std::shared_ptr<IExportConfig> config() const override;
    [[nodiscard]] ExporterListItemOptions options() const override;
    [[nodiscard]] QString defaultExt() const override;
    [[nodiscard]] QString name() const override;

private:
    VectorItemTreeExporterPtr exporter_;
    std::shared_ptr<IExportConfig> config_;
    ExporterListItemOptions options_;
    QString defaultExt_;
    QString name_;
};

class IVectorItemTreeExporterListStatic {
public:
    virtual ~IVectorItemTreeExporterListStatic() = default;
    [[nodiscard]] virtual int count() const = 0;
    [[nodiscard]] virtual VectorItemTreeExporterListItemPtr item(int index) const = 0;
};
using VectorItemTreeExporterListStaticPtr = std::shared_ptr<const IVectorItemTreeExporterListStatic>;

class VectorItemTreeExporterListStatic final
    : public IVectorItemTreeExporterListStatic { // TVectorItemTreeExporterListStatic
public:
    explicit VectorItemTreeExporterListStatic(std::vector<VectorItemTreeExporterListItemPtr> items);

    [[nodiscard]] int count() const override;
    [[nodiscard]] VectorItemTreeExporterListItemPtr item(int index) const override;

private:
    std::vector<VectorItemTreeExporterListItemPtr> items_;
};

// Holder of the current static list (interface only; the implementations come with their owners).
class IVectorItemTreeExporterListChangeable : public core::Changeable {
public:
    [[nodiscard]] virtual VectorItemTreeExporterListStaticPtr getStatic() const = 0;
};

} // namespace cg::vector
