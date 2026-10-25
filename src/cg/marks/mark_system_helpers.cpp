// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_system_helpers.h"

#include "cg/geo/geometry_lonlat.h"

#include <QChar>
#include <QCollator>
#include <QLocale>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cg::marks {

namespace {

// One line of the legacy TStringList: the (remaining) name and its Objects[] category.
struct Entry {
    QString name;
    MarkCategoryPtr category;
};

constexpr QChar cPathSeparator = QLatin1Char('\\'); // Pos('\', AName)
constexpr QChar cSortSeparator = QLatin1Char(' ');  // CSeparatorChar
constexpr QChar cReplacementChar = QChar(1);        // CReplacementChar = #1

// GetLevelName (:303-320)
bool getLevelName(const QString& name, QString& curLevelName, QString& trailName) {
    const qsizetype pos = name.indexOf(cPathSeparator);
    if (pos >= 0) {
        curLevelName = name.left(pos);
        trailName = name.mid(pos + 1);
        return true;
    }
    curLevelName = name;
    trailName.clear();
    return false;
}

// The fixed comparison of the sort. A Qt without ICU cannot honour it (QCollator then follows the C
// library or compares code points): detected by two probes that hold only for the ICU root order, and
// then the port compares code points - deterministic, but a name with a space next to its parent and
// child can split a folder into repeated nodes (L-161 / D-68).
class FixedCollation {
public:
    FixedCollation() : collator_(QLocale(QLocale::English, QLocale::UnitedStates)) {
        collator_.setCaseSensitivity(Qt::CaseSensitive);
        collator_.setIgnorePunctuation(false);
        collator_.setNumericMode(false);
        icu_ = collator_.compare(QStringLiteral("a"), QStringLiteral("B")) < 0 &&
               collator_.compare(QStringLiteral("A\x01"
                                                "B"),
                                 QStringLiteral("AB")) == 0;
    }
    [[nodiscard]] bool less(const QString& a, const QString& b) const {
        return icu_ ? collator_.compare(a, b) < 0 : a < b;
    }

private:
    QCollator collator_;
    bool icu_ = false;
};

MarkCategoryTreePtr makeLeafOrBranch(const QString& groupName, const MarkCategoryPtr& category,
                                     std::vector<Entry>& subList);

// TreeFromSortedStringList (:322-406)
MarkCategoryTreePtr treeFromSortedStringList(const QString& groupName, const MarkCategoryPtr& groupCategory,
                                             std::vector<Entry> strings) {
    std::vector<MarkCategoryTreePtr> subItems;
    QString prefix;
    QString trail;
    if (strings.size() == 1) {
        const QString name = strings[0].name;
        if (getLevelName(name, prefix, trail)) {
            strings[0].name = trail;
            subItems.push_back(treeFromSortedStringList(prefix, nullptr, std::move(strings)));
        } else {
            subItems.push_back(std::make_shared<const MarkCategoryTree>(strings[0].category, name,
                                                                        std::vector<MarkCategoryTreePtr>{}));
        }
    } else {
        std::vector<Entry> subList;
        QString name = strings[0].name;
        QString curGroupName;
        MarkCategoryPtr category;
        if (getLevelName(name, prefix, trail)) {
            curGroupName = prefix;
            subList.push_back({trail, strings[0].category});
        } else {
            curGroupName = name;
            category = strings[0].category;
        }
        for (std::size_t i = 1; i < strings.size(); ++i) {
            name = strings[i].name;
            if (getLevelName(name, prefix, trail)) {
                if (prefix == curGroupName) {
                    subList.push_back({trail, strings[i].category});
                } else {
                    subItems.push_back(makeLeafOrBranch(curGroupName, category, subList));
                    subList.clear();
                    category = nullptr;
                    curGroupName = prefix;
                    subList.push_back({trail, strings[i].category});
                }
            } else {
                subItems.push_back(makeLeafOrBranch(curGroupName, category, subList));
                subList.clear();
                curGroupName = name;
                category = strings[i].category;
            }
        }
        subItems.push_back(makeLeafOrBranch(curGroupName, category, subList));
    }
    return std::make_shared<const MarkCategoryTree>(groupCategory, groupName, std::move(subItems));
}

// The three identical "if VSubList.Count > 0 then recurse else leaf" blocks of the legacy loop.
MarkCategoryTreePtr makeLeafOrBranch(const QString& groupName, const MarkCategoryPtr& category,
                                     std::vector<Entry>& subList) {
    if (!subList.empty()) {
        return treeFromSortedStringList(groupName, category, subList);
    }
    return std::make_shared<const MarkCategoryTree>(category, groupName, std::vector<MarkCategoryTreePtr>{});
}

// --- ImportItemsTree and CategoryTreeToMarkTreeHelper (:72-301, T-0106) ---

using NewCategories = std::vector<MarkCategoryPtr>; // ACategoryList of PrepareCategoriesFromTreeForImport
using ImportedMarks = std::vector<vector::VectorDataItemPtr>; // AMarkList of PrepareFromTreeForImport

// AImportConfig.RootCategory, read where legacy reads its name (:86-90, :140-144). Nil is an access violation
// in legacy at that point, before any database call; refused (D-95).
vector::CategoryPtr rootCategoryOf(const vector::IImportConfig& config) {
    vector::CategoryPtr root = config.rootCategory();
    if (root == nullptr) {
        throw std::invalid_argument("import: nil root category");
    }
    return root;
}

// VCategoryName + '\' + node name, or the node name alone when the path is empty (:94-98, :148-152).
QString joinCategoryPath(const QString& parentPath, const QString& nodeName) {
    return parentPath.isEmpty() ? nodeName : parentPath + cPathSeparator + nodeName;
}

// PrepareCategoriesFromTreeForImport (:72-116). Reads only: a named node whose path the database does not
// hold is listed (the list is written once, by ImportItemsTree). Recursion in tree order.
void prepareCategoriesFromTreeForImport(NewCategories& categories,
                                        const vector::VectorItemTreePtr& dataItemTree,
                                        const vector::IImportConfig& config, IMarkCategoryDBImpl& categoryDb,
                                        const IMarkCategoryFactory& categoryFactory,
                                        const QString& parentCategoryName) {
    if (dataItemTree == nullptr) {
        throw std::invalid_argument("import: nil tree node"); // legacy: access violation at :106
    }
    QString categoryName = parentCategoryName.isEmpty() ? rootCategoryOf(config)->name() : parentCategoryName;
    if (!dataItemTree->name().isEmpty()) {
        categoryName = joinCategoryPath(categoryName, dataItemTree->name());
        if (categoryDb.firstCategoryByName(categoryName) == nullptr) {
            categories.push_back(categoryFactory.createNew(categoryName));
        }
    }
    const int subCount = dataItemTree->subTreeItemCount();
    for (int i = 0; i < subCount; ++i) {
        prepareCategoriesFromTreeForImport(categories, dataItemTree->subTreeItem(i), config, categoryDb,
                                           categoryFactory, categoryName);
    }
}

// PrepareFromTreeForImport (:118-211). A named node takes its category from the database, or creates and
// writes it at once (updateCategory), and the node's items take that category. An unnamed node's items take
// the ROOT category (the category is read from the root in each call, :140; L row). An item is looked up for
// the ignore check unless its name is generated (an unnamed item of a named node); the factory turns the rest
// into marks, and a refused item (null) is dropped.
void prepareFromTreeForImport(ImportedMarks& marks, const vector::VectorItemTreePtr& dataItemTree,
                              const vector::IImportConfig& config, IMarkDbImpl& markDb,
                              const IMarkFactory& markFactory, IMarkCategoryDBImpl& categoryDb,
                              const IMarkCategoryFactory& categoryFactory,
                              const QString& parentCategoryName) {
    if (dataItemTree == nullptr) {
        throw std::invalid_argument("import: nil tree node"); // legacy: access violation at :161
    }
    vector::CategoryPtr category = rootCategoryOf(config);
    QString categoryName = parentCategoryName.isEmpty() ? category->name() : parentCategoryName;
    if (!dataItemTree->name().isEmpty()) {
        categoryName = joinCategoryPath(categoryName, dataItemTree->name());
        MarkCategoryPtr markCategory = categoryDb.firstCategoryByName(categoryName);
        if (markCategory == nullptr) {
            markCategory = categoryDb.updateCategory(nullptr, categoryFactory.createNew(categoryName));
        }
        category = markCategory;
    }
    if (const vector::VectorItemSubsetPtr items = dataItemTree->items()) {
        const int count = items->count();
        for (int i = 0; i < count; ++i) {
            const vector::VectorDataItemPtr item = items->item(i);
            if (item == nullptr) {
                throw std::invalid_argument("import: nil item"); // legacy: access violation at :165
            }
            QString name = item->name();
            if (name.isEmpty() && !dataItemTree->name().isEmpty()) {
                // A generated name is not looked up (the ignore check is in the else branch, :167-176).
                name = count > 1 ? dataItemTree->name() + QStringLiteral("-") + QString::number(i + 1)
                                 : dataItemTree->name();
            } else {
                const vector::ImportCategoryParamsPtr categoryParams = config.categoryParams();
                if (categoryParams == nullptr) {
                    throw std::invalid_argument(
                        "import: nil category params"); // legacy: access violation at :173
                }
                if (categoryParams->isIgnoreMarkIfExistsWithSameNameInCategory() &&
                    markDb.firstMarkByName(name, category) != nullptr) {
                    continue;
                }
            }
            const geo::GeometryLonLatPtr geometry = item->geometry();
            std::shared_ptr<const vector::IImportMarkParams> params; // nil when no type matches (:179)
            if (std::dynamic_pointer_cast<const geo::IGeometryLonLatPoint>(geometry) != nullptr) {
                params = config.pointParams();
            } else if (std::dynamic_pointer_cast<const geo::IGeometryLonLatLine>(geometry) != nullptr) {
                params = config.lineParams();
            } else if (std::dynamic_pointer_cast<const geo::IGeometryLonLatPolygon>(geometry) != nullptr) {
                params = config.polyParams();
            }
            if (const vector::VectorDataItemPtr mark =
                    markFactory.prepareMark(item, name, params, category)) {
                marks.push_back(mark);
            }
        }
    }
    const int subCount = dataItemTree->subTreeItemCount();
    for (int i = 0; i < subCount; ++i) {
        prepareFromTreeForImport(marks, dataItemTree->subTreeItem(i), config, markDb, markFactory, categoryDb,
                                 categoryFactory, categoryName);
    }
}

} // namespace

// ImportItemsTree (u_MarkSystemHelpers.pas:213-255)
MarkList importItemsTree(const vector::VectorItemTreePtr& tree, const vector::ImportConfigPtr& config,
                         IMarkDbImpl& markDb, const IMarkFactory& markFactory,
                         IMarkCategoryDBImpl& categoryDb, const IMarkCategoryFactory& categoryFactory) {
    if (tree == nullptr || config == nullptr) {
        // legacy: access violation at :86 (config) or at :106 (tree), both before any database call
        throw std::invalid_argument("importItemsTree: nil tree or config");
    }
    NewCategories categories;
    prepareCategoriesFromTreeForImport(categories, tree, *config, categoryDb, categoryFactory, QString());
    if (!categories.empty()) {
        categoryDb.updateCategoryList(nullptr, MarkCategoryList::build(std::move(categories)));
    }
    ImportedMarks marks;
    prepareFromTreeForImport(marks, tree, *config, markDb, markFactory, categoryDb, categoryFactory,
                             QString());
    if (marks.empty()) {
        return nullptr;
    }
    return markDb.updateMarkList(nullptr, std::make_shared<const ImportedMarks>(std::move(marks)));
}

// CategoryTreeToMarkTreeHelper (u_MarkSystemHelpers.pas:257-301)
vector::VectorItemTreePtr categoryTreeToMarkTreeHelper(const IMarkDbImpl& markDb,
                                                       const MarkCategoryTreePtr& tree,
                                                       bool includeHiddenMarks) {
    if (tree == nullptr) {
        return nullptr;
    }
    vector::VectorItemSubsetPtr markSubset;
    if (tree->markCategory() != nullptr) {
        markSubset = markDb.markSubsetByCategory(tree->markCategory(), includeHiddenMarks);
    }
    const int count = tree->subItemCount();
    std::vector<vector::VectorItemTreePtr> subItems;
    subItems.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        subItems.push_back(categoryTreeToMarkTreeHelper(markDb, tree->subItem(i), includeHiddenMarks));
    }
    return std::make_shared<const vector::VectorItemTree>(tree->name(), std::move(markSubset),
                                                          std::move(subItems));
}

// CategoryListToCategoryTree (:408-441)
MarkCategoryTreePtr categoryListToCategoryTree(const MarkCategoryListPtr& list) {
    if (list == nullptr || list->count() == 0) {
        return nullptr; // legacy: Assert, then Strings[0] raises EStringListError (L-159)
    }
    std::vector<Entry> items;
    items.reserve(static_cast<std::size_t>(list->count()));
    for (int i = 0; i < list->count(); ++i) {
        const MarkCategoryPtr item = list->item(i);
        if (!item) {
            // legacy :422-423 reads IMarkCategory(AList.Items[i]).Name: an access violation
            throw std::runtime_error("Access violation: nil category in a category list");
        }
        QString name = item->name();
        name.replace(QLatin1Char(' '), cReplacementChar);
        name.replace(cPathSeparator, cSortSeparator);
        items.push_back({name, item});
    }
    // TStringList.Sort with CaseSensitive = True is AnsiCompareStr (Windows CompareString, user locale).
    // The port uses ONE fixed comparison, independent of the process locale (D-68): ICU root collation
    // through QCollator(en_US), case-sensitive, punctuation not ignored. Equal keys keep the input order
    // (Delphi's quick sort is unstable).
    const FixedCollation collation;
    std::stable_sort(items.begin(), items.end(),
                     [&collation](const Entry& a, const Entry& b) { return collation.less(a.name, b.name); });
    for (Entry& e : items) {
        e.name.replace(cSortSeparator, cPathSeparator);
        e.name.replace(cReplacementChar, QLatin1Char(' '));
    }
    return treeFromSortedStringList(QString(), nullptr, std::move(items));
}

} // namespace cg::marks
