// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MarkSystemHelpers.pas, fully (:1-301: ImportItemsTree, CategoryTreeToMarkTreeHelper; their private
// steps PrepareCategoriesFromTreeForImport and PrepareFromTreeForImport are in the .cpp; :303-441:
// GetLevelName, TreeFromSortedStringList, CategoryListToCategoryTree). TMarkSystem calls the first two.

#include "cg/marks/mark_category_db_impl.h"
#include "cg/marks/mark_category_factory.h"
#include "cg/marks/mark_category_list.h"
#include "cg/marks/mark_category_tree.h"
#include "cg/marks/mark_db_impl.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mark_id.h"
#include "cg/vector/import_config.h"
#include "cg/vector/vector_item_tree.h"

namespace cg::marks {

// ImportItemsTree (u_MarkSystemHelpers.pas:213-255). Lists the categories that the tree names and the
// database lacks (one updateCategoryList call, only when there are some), prepares every item with the import
// params of its geometry type (the factory drops the ones it refuses), and writes the marks with one
// updateMarkList call. Returns the written marks, or nullptr when no mark was prepared (legacy: nil, and no
// call of updateMarkList). A nil tree, config, root category, categoryParams (when read), node or item is
// refused with std::invalid_argument at the point legacy dereferences it (access violation; a deviation from
// legacy).
[[nodiscard]] MarkList importItemsTree(const vector::VectorItemTreePtr& tree,
                                       const vector::ImportConfigPtr& config, IMarkDbImpl& markDb,
                                       const IMarkFactory& markFactory, IMarkCategoryDBImpl& categoryDb,
                                       const IMarkCategoryFactory& categoryFactory);

// CategoryTreeToMarkTreeHelper (u_MarkSystemHelpers.pas:257-301). Pre-order: the node's subset of marks is
// asked first (only when the node has a category), then its children in order. A nil node gives nullptr
// (legacy Assigned guard); a nil child is kept as a nil subtree (legacy VTemp.Add of the nil result).
[[nodiscard]] vector::VectorItemTreePtr categoryTreeToMarkTreeHelper(const IMarkDbImpl& markDb,
                                                                     const MarkCategoryTreePtr& tree,
                                                                     bool includeHiddenMarks);

// CategoryListToCategoryTree (u_MarkSystemHelpers.pas:408-441). Builds the folder tree of the category names.
// The path separator is '\' (u_MarkSystemHelpers.pas:311); the names are sorted, after '\' is made to sort
// below every printable character, with ONE fixed comparison (ICU root order, en_US, case-sensitive) instead
// of AnsiCompareStr in the user locale; the root node has the name "" and no category. An empty or null list
// gives nullptr (unreachable in legacy).
[[nodiscard]] MarkCategoryTreePtr categoryListToCategoryTree(const MarkCategoryListPtr& list);

} // namespace cg::marks
