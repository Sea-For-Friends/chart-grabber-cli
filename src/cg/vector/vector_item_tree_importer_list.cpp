// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/vector_item_tree_importer_list.h"

#include "cg/core/delphi_text.h"

#include <cstddef>
#include <utility>

namespace cg::vector {

VectorItemTreeImporterListItem::VectorItemTreeImporterListItem(VectorItemTreeImporterPtr importer,
                                                               QStringList supportedExt, QString name)
    : importer_(std::move(importer)), supportedExt_(std::move(supportedExt)), name_(std::move(name)) {}

VectorItemTreeImporterPtr VectorItemTreeImporterListItem::importer() const {
    return importer_;
}

QStringList VectorItemTreeImporterListItem::supportedExt() const {
    return supportedExt_;
}

QString VectorItemTreeImporterListItem::name() const {
    return name_;
}

VectorItemTreeImporterListStatic::VectorItemTreeImporterListStatic(
    std::vector<VectorItemTreeImporterListItemPtr> items)
    : items_(std::move(items)) {}

int VectorItemTreeImporterListStatic::count() const {
    return static_cast<int>(items_.size());
}

VectorItemTreeImporterListItemPtr VectorItemTreeImporterListStatic::item(int index) const {
    // Legacy GetItem (:149-154) has no range check; out of range (EListError from the RTL list) and
    // the nil list (access violation) are answered with null here. D row of T-0141.
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    return items_[static_cast<std::size_t>(index)];
}

VectorItemTreeImporterPtr VectorItemTreeImporterListStatic::importerByExt(const QString& ext) const {
    // Ports GetImporterByExt (u_VectorItemTreeImporterList.pas:123-147). Only the argument is lower-cased
    // (LowerCase, ASCII letters only, :133); the stored extensions are compared as they are.
    VectorItemTreeImporterPtr result;
    QString lowered = core::delphiLowerCase(ext);
    if (lowered.startsWith(u'.')) { // :134-136: one leading dot is dropped
        lowered.remove(0, 1);
    }
    for (const auto& listItem : items_) {
        for (const QString& supported : listItem->supportedExt()) {
            if (lowered == supported) {
                result = listItem->importer();
                break; // legacy Break (:143) leaves the inner loop only: the last item listing ext wins
            }
        }
    }
    return result;
}

} // namespace cg::vector
