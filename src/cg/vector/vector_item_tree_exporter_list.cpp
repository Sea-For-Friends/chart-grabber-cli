// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/vector_item_tree_exporter_list.h"

#include <cstddef>
#include <utility>

namespace cg::vector {

VectorItemTreeExporterListItem::VectorItemTreeExporterListItem(VectorItemTreeExporterPtr exporter,
                                                               std::shared_ptr<IExportConfig> config,
                                                               ExporterListItemOptions options,
                                                               QString defaultExt, QString name)
    : exporter_(std::move(exporter)), config_(std::move(config)), options_(options),
      defaultExt_(std::move(defaultExt)), name_(std::move(name)) {}

VectorItemTreeExporterPtr VectorItemTreeExporterListItem::exporter() const {
    return exporter_;
}

std::shared_ptr<IExportConfig> VectorItemTreeExporterListItem::config() const {
    return config_;
}

ExporterListItemOptions VectorItemTreeExporterListItem::options() const {
    return options_;
}

QString VectorItemTreeExporterListItem::defaultExt() const {
    return defaultExt_;
}

QString VectorItemTreeExporterListItem::name() const {
    return name_;
}

VectorItemTreeExporterListStatic::VectorItemTreeExporterListStatic(
    std::vector<VectorItemTreeExporterListItemPtr> items)
    : items_(std::move(items)) {}

int VectorItemTreeExporterListStatic::count() const {
    return static_cast<int>(items_.size());
}

VectorItemTreeExporterListItemPtr VectorItemTreeExporterListStatic::item(int index) const {
    // Legacy GetItem (:134-139) has no range check and no nil guard; both are answered with null here.
    // D row of T-0141.
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    return items_[static_cast<std::size_t>(index)];
}

} // namespace cg::vector
