// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_VectorItemTreeImporterList.pas, Src/Vector/u_VectorItemTreeImporterList.pas
//
// The importer list: items (value holders) and the static list, looked up by file extension.
// A nil legacy list (FList = nil) is an empty vector here: count() is 0
// (u_VectorItemTreeImporterList.pas:116-120). item() outside 0..count()-1 and importerByExt() on an empty
// list answer null (D rows of T-0141).

#include "cg/core/changeable.h"
#include "cg/vector/vector_item_tree_importer.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace cg::vector {

class IVectorItemTreeImporterListItem {
public:
    virtual ~IVectorItemTreeImporterListItem() = default;
    [[nodiscard]] virtual VectorItemTreeImporterPtr importer() const = 0;
    [[nodiscard]] virtual QStringList
    supportedExt() const = 0; // without dot, as stored (not lower-cased here)
    [[nodiscard]] virtual QString name() const = 0;
};
using VectorItemTreeImporterListItemPtr = std::shared_ptr<const IVectorItemTreeImporterListItem>;

class VectorItemTreeImporterListItem final
    : public IVectorItemTreeImporterListItem { // TVectorItemTreeImporterListItem
public:
    VectorItemTreeImporterListItem(VectorItemTreeImporterPtr importer, QStringList supportedExt,
                                   QString name);

    [[nodiscard]] VectorItemTreeImporterPtr importer() const override;
    [[nodiscard]] QStringList supportedExt() const override; // a copy: the item is not changed by the caller
    [[nodiscard]] QString name() const override;

private:
    VectorItemTreeImporterPtr importer_;
    QStringList supportedExt_;
    QString name_;
};

class IVectorItemTreeImporterListStatic {
public:
    virtual ~IVectorItemTreeImporterListStatic() = default;
    [[nodiscard]] virtual int count() const = 0;
    [[nodiscard]] virtual VectorItemTreeImporterListItemPtr item(int index) const = 0;
    [[nodiscard]] virtual VectorItemTreeImporterPtr importerByExt(const QString& ext) const = 0;
};
using VectorItemTreeImporterListStaticPtr = std::shared_ptr<const IVectorItemTreeImporterListStatic>;

class VectorItemTreeImporterListStatic final
    : public IVectorItemTreeImporterListStatic { // TVectorItemTreeImporterListStatic
public:
    explicit VectorItemTreeImporterListStatic(std::vector<VectorItemTreeImporterListItemPtr> items);

    [[nodiscard]] int count() const override;
    [[nodiscard]] VectorItemTreeImporterListItemPtr item(int index) const override;
    [[nodiscard]] VectorItemTreeImporterPtr importerByExt(const QString& ext) const override;

private:
    std::vector<VectorItemTreeImporterListItemPtr> items_;
};

// Holder of the current static list (interface only; the implementations come with their owners).
class IVectorItemTreeImporterListChangeable : public core::Changeable {
public:
    [[nodiscard]] virtual VectorItemTreeImporterListStaticPtr getStatic() const = 0;
};

} // namespace cg::vector
