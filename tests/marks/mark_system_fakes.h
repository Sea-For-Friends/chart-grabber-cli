// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Test helper (marks module): in-memory fakes of the backend interfaces the category facade and the import
// helpers read. A fake that is told to fail throws std::runtime_error from every method it is asked, so a
// test can check that the facade catches it. The fakes of the import helpers record their calls in a CallLog,
// so a test can check the order of the database calls. Local to tests/marks (parallel development rule).

#include "cg/core/notifier.h"
#include "cg/core/read_write_state.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_db_impl.h"
#include "cg/marks/mark_category_factory.h"
#include "cg/marks/mark_category_list.h"
#include "cg/marks/mark_db_impl.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mark_system_impl.h"
#include "cg/marks/mark_system_impl_changeable.h"
#include "cg/vector/category.h"
#include "cg/vector/import_config.h"
#include "cg/vector/vector_data_item_simple.h"
#include "cg/vector/vector_item_subset.h"

#include <QString>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mark_test_util.h"

namespace cg::marks::testutil {

// One line per call of the fakes that were given the log, in call order (UTF-8).
using CallLog = std::vector<std::string>;

inline void noteCall(CallLog* log, const QString& entry) {
    if (log != nullptr) {
        log->push_back(entry.toStdString());
    }
}

// The name of a category for a log line; a nil category is "<nil>".
inline QString categoryLabel(const vector::CategoryPtr& category) {
    return category ? category->name() : QStringLiteral("<nil>");
}

// The category table of one backend, in list order. Methods answer from `categories`; updateCategory
// appends a non-null new category, so a later firstCategoryByName sees it. updateCategoryList appends its
// categories only when `storesLists` is set (the real backend stores them).
class FakeCategoryDbImpl final : public IMarkCategoryDBImpl {
public:
    bool fail = false;
    mutable int calls = 0;
    std::vector<MarkCategoryPtr> categories;
    std::vector<QString> inserted; // names given to updateCategory, in call order
    std::string last;              // set by a void method once it has returned normally
    core::Notifier changed;
    CallLog* log = nullptr;      // "cat.first", "cat.update", "cat.list" lines, one per name
    bool storesLists = false;    // updateCategoryList stores its categories
    bool returnsNull = false;    // updateCategory answers nil and keeps nothing
    int listCalls = 0;           // updateCategoryList calls
    std::vector<QString> listed; // names of the list of the last updateCategoryList call

    [[nodiscard]] MarkCategoryPtr firstCategoryByName(const QString& name) const override {
        enter();
        noteCall(log, QStringLiteral("cat.first ") + name);
        for (const auto& c : categories) {
            if (c && c->name() == name) {
                return c;
            }
        }
        return nullptr;
    }
    [[nodiscard]] int categoryByNameCount(const QString& name) const override {
        enter();
        int count = 0;
        for (const auto& c : categories) {
            count += (c && c->name() == name) ? 1 : 0;
        }
        return count;
    }
    MarkCategoryPtr updateCategory(const MarkCategoryPtr&, const MarkCategoryPtr& newCategory) override {
        enter();
        if (newCategory) {
            noteCall(log, QStringLiteral("cat.update ") + newCategory->name());
            if (!returnsNull) {
                categories.push_back(newCategory);
                inserted.push_back(newCategory->name());
            }
        }
        return returnsNull ? nullptr : newCategory; // the answer is not the argument when returnsNull
    }
    MarkCategoryListPtr updateCategoryList(const MarkCategoryListPtr&,
                                           const MarkCategoryListPtr& newList) override {
        enter();
        ++listCalls;
        listed.clear();
        if (newList) {
            for (int i = 0; i < newList->count(); ++i) {
                const MarkCategoryPtr c = newList->item(i);
                listed.push_back(c->name());
                noteCall(log, QStringLiteral("cat.list ") + c->name());
                if (storesLists) {
                    categories.push_back(c);
                }
            }
        }
        return newList;
    }
    [[nodiscard]] bool isCategoryFromThisDb(const vector::CategoryPtr&) const override {
        enter();
        return false;
    }
    [[nodiscard]] MarkCategoryListPtr categoriesList() const override {
        enter();
        return MarkCategoryList::build(categories);
    }
    void setAllCategoriesVisible(bool visible) override {
        enter();
        last = visible ? "setAllCategoriesVisible(true)" : "setAllCategoriesVisible(false)";
    }
    [[nodiscard]] core::Notifier& changeNotifier() override { return changed; }

private:
    void enter() const {
        ++calls;
        if (fail) {
            throw std::runtime_error("boom");
        }
    }
};

// Answers every new category with a visible one whose zoom range is 3..23.
class FakeCategoryFactory final : public IMarkCategoryFactory {
public:
    [[nodiscard]] MarkCategoryPtr createNew(const QString& name) const override {
        return std::make_shared<const MarkCategory>(name, true, 3, 23);
    }
    [[nodiscard]] MarkCategoryPtr modify(const MarkCategoryPtr& source, const QString&, bool, int,
                                         int) const override {
        return source;
    }
    [[nodiscard]] MarkCategoryPtr modifyVisible(const MarkCategoryPtr& source, bool) const override {
        return source;
    }
    [[nodiscard]] std::shared_ptr<IMarkCategoryFactoryConfig> config() const override { return nullptr; }
};

// A backend mark database for the import helpers and the facade. By default it answers fixed values and
// records the arguments of the last call (the facade tests); with `byName` set, firstMarkByName answers from
// `marks` by name, markSubsetByCategory from `subsets` by category name, and updateMarkList returns its
// argument. Every call is written to `log` when one is given; updateMarkList also keeps the names it is given
// (`written`). A fake that is told to fail throws "boom: <method>" from every method.
class FakeItem final : public vector::IVectorDataItem {
public:
    [[nodiscard]] core::HashValue hash() const noexcept override { return {}; }
    [[nodiscard]] vector::VectorDataItemMainInfoPtr mainInfo() const override { return nullptr; }
    [[nodiscard]] QString name() const override { return {}; }
    [[nodiscard]] QString desc() const override { return {}; }
    [[nodiscard]] geo::GeometryLonLatPtr geometry() const override { return nullptr; }
    [[nodiscard]] vector::AppearancePtr appearance() const override { return nullptr; }
    [[nodiscard]] bool isEqual(const IVectorDataItem* item) const override { return item == this; }
    [[nodiscard]] QString hintText() const override { return {}; }
    [[nodiscard]] QString infoUrl() const override { return {}; }
    [[nodiscard]] QString infoHtml() const override { return {}; }
    [[nodiscard]] QString infoCaption() const override { return {}; }
};

class FakeDb final : public IMarkDbImpl {
public:
    bool fail = false;
    mutable int calls = 0;
    mutable std::string last;
    CallLog* log = nullptr;
    bool byName = false;
    // answers
    vector::VectorDataItemPtr item = std::make_shared<FakeItem>();
    vector::VectorItemSubsetPtr subset = std::make_shared<vector::VectorItemSubset>(
        core::HashValue{}, std::vector<vector::VectorDataItemPtr>{});
    MarkList markList = std::make_shared<const std::vector<vector::VectorDataItemPtr>>();
    MarkIdList idList = std::make_shared<const std::vector<MarkIdPtr>>();
    bool visible = false;
    std::vector<vector::VectorDataItemPtr> marks;                         // byName: firstMarkByName
    std::vector<std::pair<QString, vector::VectorItemSubsetPtr>> subsets; // byName: by category name
    // recorded arguments
    mutable QString argName;
    mutable vector::CategoryPtr argCategory;
    mutable bool argBool = false;
    mutable bool argBool2 = false;
    mutable int argInt = 0;
    mutable MarkIdPtr argId;
    mutable vector::VectorDataItemPtr argOld, argNew;
    mutable MarkOrIdList argOrIdList;
    mutable MarkList argNewList;
    int writeCalls = 0;           // updateMarkList calls
    std::vector<QString> written; // names of the list of the last updateMarkList call
    core::Notifier changed;

    [[nodiscard]] vector::VectorDataItemPtr firstMarkByName(const QString& n,
                                                            const vector::CategoryPtr& c) const override {
        enter("firstMarkByName");
        argName = n, argCategory = c;
        noteCall(log, QStringLiteral("mark.first ") + n + QStringLiteral(" @") + categoryLabel(c));
        if (!byName) {
            return item;
        }
        for (const auto& m : marks) {
            if (m && m->name() == n) {
                return m;
            }
        }
        return nullptr;
    }
    [[nodiscard]] vector::VectorItemSubsetPtr markSubsetByCategoryList(const MarkCategoryListPtr&,
                                                                       bool h) const override {
        enter("markSubsetByCategoryList");
        argBool = h;
        return subset;
    }
    [[nodiscard]] vector::VectorItemSubsetPtr markSubsetByCategory(const vector::CategoryPtr& c,
                                                                   bool h) const override {
        enter("markSubsetByCategory");
        argCategory = c, argBool = h;
        const QString label = categoryLabel(c);
        noteCall(log, QStringLiteral("mark.subset ") + label +
                          (h ? QStringLiteral(" hidden=1") : QStringLiteral(" hidden=0")));
        if (!byName) {
            return subset;
        }
        for (const auto& [name, answer] : subsets) {
            if (name == label) {
                return answer;
            }
        }
        return nullptr;
    }
    [[nodiscard]] vector::VectorItemSubsetPtr
    markSubsetByCategoryListInRect(const geo::DoubleRect& r, const MarkCategoryListPtr&, bool h,
                                   const geo::DoublePoint& s) const override {
        enter("markSubsetByCategoryListInRect");
        argBool = h, argInt = static_cast<int>(r.right + s.x());
        return subset;
    }
    [[nodiscard]] vector::VectorItemSubsetPtr
    markSubsetByCategoryInRect(const geo::DoubleRect& r, const vector::CategoryPtr& c, bool h,
                               const geo::DoublePoint& s) const override {
        enter("markSubsetByCategoryInRect");
        argCategory = c, argBool = h, argInt = static_cast<int>(r.right + s.x());
        return subset;
    }
    [[nodiscard]] vector::VectorItemSubsetPtr findMarks(const QString& q, int max, bool h,
                                                        bool d) const override {
        enter("findMarks");
        argName = q, argInt = max, argBool = h, argBool2 = d;
        return subset;
    }
    vector::VectorDataItemPtr updateMark(const vector::VectorDataItemPtr& o,
                                         const vector::VectorDataItemPtr& n) override {
        enter("updateMark");
        argOld = o, argNew = n;
        return item;
    }
    MarkList updateMarkList(const MarkOrIdList& o, const MarkList& n) override {
        enter("updateMarkList");
        argOrIdList = o, argNewList = n;
        ++writeCalls;
        written.clear();
        if (n) {
            for (const auto& m : *n) {
                written.push_back(m->name());
                noteCall(log, QStringLiteral("mark.write ") + m->name());
            }
        }
        return byName ? n : markList;
    }
    [[nodiscard]] MarkIdList allMarkIdList() const override {
        enter("allMarkIdList");
        return idList;
    }
    [[nodiscard]] MarkIdList markIdListByCategory(const vector::CategoryPtr& c) const override {
        enter("markIdListByCategory");
        argCategory = c;
        return idList;
    }
    [[nodiscard]] vector::VectorDataItemPtr markById(const MarkIdPtr& id) const override {
        enter("markById");
        argId = id;
        return item;
    }
    void setMarkVisibleById(const MarkIdPtr& id, bool b) override {
        enter("setMarkVisibleById");
        argId = id, argBool = b;
    }
    void setMarkVisible(const vector::VectorDataItemPtr& mk, bool b) override {
        enter("setMarkVisible");
        argOld = mk, argBool = b;
    }
    void setMarkVisibleByIdList(const MarkOrIdList& l, bool b) override {
        enter("setMarkVisibleByIdList");
        argOrIdList = l, argBool = b;
    }
    void toggleMarkVisibleByIdList(const MarkOrIdList& l) override {
        enter("toggleMarkVisibleByIdList");
        argOrIdList = l;
    }
    [[nodiscard]] bool markVisibleById(const MarkIdPtr& id) const override {
        enter("markVisibleById");
        argId = id;
        return visible;
    }
    [[nodiscard]] bool markVisible(const vector::VectorDataItemPtr& mk) const override {
        enter("markVisible");
        argOld = mk;
        return visible;
    }
    void setAllMarksInCategoryVisible(const vector::CategoryPtr& c, bool b) override {
        enter("setAllMarksInCategoryVisible");
        argCategory = c, argBool = b;
    }
    [[nodiscard]] core::Notifier& changeNotifier() override { return changed; }

private:
    void enter(const char* name) const {
        ++calls;
        last = name;
        if (fail) {
            throw std::runtime_error(std::string("boom: ") + name);
        }
    }
};

// The mark factory of the import helpers. prepareMark records "factory.prepare <name> <kind> @<category>",
// where kind is the class of the params it was given (point, line, poly, or nil); it refuses a null item or
// null params (as MarkFactory::prepareMark does) and the names in `refused`; otherwise it answers a mark of
// the given name. The other methods are not used by the import helpers and throw std::logic_error.
class FakeMarkFactory final : public IMarkFactory {
public:
    CallLog* log = nullptr;
    std::vector<QString> refused;

    [[nodiscard]] vector::VectorDataItemPtr
    prepareMark(const vector::VectorDataItemPtr& item, const QString& name,
                const std::shared_ptr<const vector::IImportMarkParams>& params,
                const vector::CategoryPtr& category) const override {
        QString kind = QStringLiteral("nil");
        if (dynamic_cast<const vector::IImportPointParams*>(params.get()) != nullptr) {
            kind = QStringLiteral("point");
        } else if (dynamic_cast<const vector::IImportLineParams*>(params.get()) != nullptr) {
            kind = QStringLiteral("line");
        } else if (dynamic_cast<const vector::IImportPolyParams*>(params.get()) != nullptr) {
            kind = QStringLiteral("poly");
        }
        noteCall(log, QStringLiteral("factory.prepare ") + name + QStringLiteral(" ") + kind +
                          QStringLiteral(" @") + categoryLabel(category));
        if (item == nullptr || params == nullptr ||
            std::find(refused.begin(), refused.end(), name) != refused.end()) {
            return nullptr;
        }
        return dataItem(name,
                        item->geometry()); // a new mark of that name and the item's geometry (CreateMark)
    }
    [[nodiscard]] vector::VectorDataItemPtr createNewMark(const geo::GeometryLonLatPtr&, const QString&,
                                                          const QString&,
                                                          const MarkTemplatePtr&) const override {
        throw std::logic_error("FakeMarkFactory: not used by the import helpers");
    }
    [[nodiscard]] vector::VectorDataItemPtr replaceCategory(const vector::VectorDataItemPtr&,
                                                            const vector::CategoryPtr&) const override {
        throw std::logic_error("FakeMarkFactory: not used by the import helpers");
    }
    [[nodiscard]] vector::VectorDataItemPtr createMark(const geo::GeometryLonLatPtr&, const QString&,
                                                       const QString&, const vector::CategoryPtr&,
                                                       const vector::AppearancePtr&) const override {
        throw std::logic_error("FakeMarkFactory: not used by the import helpers");
    }
    [[nodiscard]] vector::VectorDataItemPtr modifyGeometry(const vector::VectorDataItemPtr&,
                                                           const geo::GeometryLonLatPtr&) const override {
        throw std::logic_error("FakeMarkFactory: not used by the import helpers");
    }
    [[nodiscard]] vector::VectorDataItemPtr modifyName(const vector::VectorDataItemPtr&, const QString&,
                                                       const QString&) const override {
        throw std::logic_error("FakeMarkFactory: not used by the import helpers");
    }
    [[nodiscard]] vector::MarkPictureListPtr markPictureList() const override {
        throw std::logic_error("FakeMarkFactory: not used by the import helpers");
    }
    [[nodiscard]] std::shared_ptr<IMarkFactoryConfig> config() const override {
        throw std::logic_error("FakeMarkFactory: not used by the import helpers");
    }
};

class FakeMarkSystemImpl final : public IMarkSystemImpl {
public:
    std::shared_ptr<IMarkDbImpl>
        markDatabase; // null answers markDb() with nullptr (a member cannot be named markDb)
    std::shared_ptr<IMarkCategoryDBImpl> db;       // null answers categoryDB() with nullptr
    core::ReadWriteStateChangeablePtr stateObject; // null answers state() with nullptr
    [[nodiscard]] core::ReadWriteStateChangeablePtr state() const override { return stateObject; }
    [[nodiscard]] std::shared_ptr<IMarkDbImpl> markDb() const override { return markDatabase; }
    [[nodiscard]] std::shared_ptr<IMarkCategoryDBImpl> categoryDB() const override { return db; }
    [[nodiscard]] QString stringIdByMark(const vector::VectorDataItemPtr&) const override { return {}; }
    [[nodiscard]] vector::VectorDataItemPtr markByStringId(const QString&) const override { return nullptr; }
    [[nodiscard]] MarkCategoryPtr markCategoryByStringId(const QString&) const override { return nullptr; }
};

// Opening (or closing, with null) a backend publishes `changed()` as the real object does.
class FakeMarkSystemImplChangeable final : public IMarkSystemImplChangeable {
public:
    std::shared_ptr<IMarkSystemImpl> impl;
    [[nodiscard]] core::ReadWriteStateChangeablePtr state() const override { return nullptr; }
    [[nodiscard]] std::shared_ptr<IMarkSystemImpl> getStatic() const override { return impl; }
    void open(std::shared_ptr<IMarkSystemImpl> next) {
        impl = std::move(next);
        fireChanged();
    }
};

} // namespace cg::marks::testutil
