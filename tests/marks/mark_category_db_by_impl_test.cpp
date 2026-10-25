// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: u_MarkCategoryDbByImpl.pas (TMarkCategoryDbByImpl) with the fake backends of
// mark_system_fakes.h.
#include "cg/core/notifier.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_db_by_impl.h"
#include "cg/marks/mark_category_list.h"
#include "cg/marks/mark_category_tree.h"

#include <QString>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "mark_system_fakes.h"

namespace m = cg::marks;
namespace t = cg::marks::testutil;

namespace {

m::MarkCategoryPtr named(const QString& name, bool visible, int afterScale, int beforeScale) {
    return std::make_shared<const m::MarkCategory>(name, visible, afterScale, beforeScale);
}

// A new category as a caller saves it: visible, zoom range 3..23.
m::MarkCategoryPtr newCat(const QString& name) {
    return named(name, true, 3, 23);
}

std::shared_ptr<m::IMarkSystemImpl> implWith(std::shared_ptr<m::IMarkCategoryDBImpl> db) {
    auto impl = std::make_shared<t::FakeMarkSystemImpl>();
    impl->db = std::move(db);
    return impl;
}

std::string nameOf(const m::MarkCategoryPtr& c) {
    return c ? c->name().toStdString() : "<null>";
}

std::string namesOf(const m::MarkCategoryListPtr& list) {
    if (!list) {
        return "<null>";
    }
    std::string text;
    for (int i = 0; i < list->count(); ++i) {
        if (i > 0) { // not (cond ? "|" : "") + std::string&&: GCC 12 Release reports a false -Werror=restrict
                     // (T-0316)
            text += '|';
        }
        text += nameOf(list->item(i));
    }
    return text;
}

// Tree shape: name, "!" when the node carries a category, children in parentheses.
std::string describe(const m::MarkCategoryTreePtr& tree) {
    if (!tree) {
        return "<null>";
    }
    std::string text = tree->name().toStdString();
    if (tree->markCategory()) {
        text += "!";
    }
    if (tree->subItemCount() > 0) {
        text += "(";
        for (int i = 0; i < tree->subItemCount(); ++i) {
            if (i > 0) {
                text += ',';
            }
            text += describe(tree->subItem(i));
        }
        text += ")";
    }
    return text;
}

std::string joined(const std::vector<QString>& names) {
    std::string text;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            text += '|';
        }
        text += names[i].toStdString();
    }
    return text;
}

// A backend seeded with A, A\B, A\BC (hidden), Z, and the facade over it, with the errors counted.
struct Fixture {
    std::shared_ptr<t::FakeMarkSystemImplChangeable> changeable =
        std::make_shared<t::FakeMarkSystemImplChangeable>();
    std::shared_ptr<t::FakeCategoryDbImpl> db = std::make_shared<t::FakeCategoryDbImpl>();
    std::shared_ptr<t::FakeCategoryFactory> factory = std::make_shared<t::FakeCategoryFactory>();
    m::MarkCategoryPtr a = named(QStringLiteral("A"), true, 5, 20);
    m::MarkCategoryPtr ab = named(QStringLiteral("A\\B"), true, 5, 20);
    m::MarkCategoryPtr abc = named(QStringLiteral("A\\BC"), false, 5, 20);
    m::MarkCategoryPtr z = named(QStringLiteral("Z"), true, 1, 2);
    cg::core::Notifier errors;
    int errorCount = 0;
    cg::core::Subscription errorSub = errors.subscribe([this] { ++errorCount; });
    std::unique_ptr<m::MarkCategoryDbByImpl> facade;

    explicit Fixture(bool open = true) {
        db->categories = {a, ab, abc, z};
        if (open) {
            changeable->impl = implWith(db);
        }
        facade = std::make_unique<m::MarkCategoryDbByImpl>(changeable, factory, errors);
    }
};

// One facade method: how to call it (the summary of its result), the answer of a healthy backend, and
// the legacy empty value. usesBackend false means the legacy method never asks the backend.
struct Row {
    const char* method;
    bool usesBackend;
    std::function<std::string(m::IMarkCategoryDB&, Fixture&)> call;
    std::string served;
    std::string empty; // not used when usesBackend is false
};

std::vector<Row> rows() {
    return {
        {"firstCategoryByName", true,
         [](m::IMarkCategoryDB& c, Fixture&) {
             return nameOf(c.firstCategoryByName(QStringLiteral("A\\B")));
         },
         "A\\B", "<null>"},
        {"categoryByNameCount", true,
         [](m::IMarkCategoryDB& c, Fixture&) {
             return std::to_string(c.categoryByNameCount(QStringLiteral("A")));
         },
         "1", "0"},
        {"categoryWithSubCategories", true,
         [](m::IMarkCategoryDB& c, Fixture& f) { return namesOf(c.categoryWithSubCategories(f.a)); },
         "A|A\\B|A\\BC", "<null>"},
        {"subCategoryListForCategory", true,
         [](m::IMarkCategoryDB& c, Fixture& f) { return namesOf(c.subCategoryListForCategory(f.a)); },
         "A\\B|A\\BC", "<null>"},
        {"categoriesList", true, [](m::IMarkCategoryDB& c, Fixture&) { return namesOf(c.categoriesList()); },
         "A|A\\B|A\\BC|Z", "<null>"},
        {"visibleCategories(zoom 4)", true,
         [](m::IMarkCategoryDB& c, Fixture&) { return namesOf(c.visibleCategories(4)); }, "A|A\\B", "<null>"},
        {"visibleCategoriesIgnoreZoom", true,
         [](m::IMarkCategoryDB& c, Fixture&) { return namesOf(c.visibleCategoriesIgnoreZoom()); }, "A|A\\B|Z",
         "<null>"},
        {"categoryListToStaticTree", false,
         [](m::IMarkCategoryDB& c, Fixture& f) {
             return describe(c.categoryListToStaticTree(m::MarkCategoryList::build({f.a, f.ab})));
         },
         "(A!(B!))", "<null>"},
        {"filterVisibleCategories", false,
         [](m::IMarkCategoryDB& c, Fixture& f) {
             return namesOf(c.filterVisibleCategories(m::MarkCategoryList::build({f.a, f.abc, f.z})));
         },
         "A|Z", "<null>"},
        {"updateCategory", true,
         [](m::IMarkCategoryDB& c, Fixture&) { return nameOf(c.updateCategory(nullptr, newCat("X\\Y"))); },
         "X\\Y", "<null>"},
        {"updateCategoryList", true,
         [](m::IMarkCategoryDB& c, Fixture&) {
             return namesOf(c.updateCategoryList(nullptr, m::MarkCategoryList::build({newCat("X\\Y")})));
         },
         "X\\Y", "<null>"},
        {"setAllCategoriesVisible", true,
         [](m::IMarkCategoryDB& c, Fixture& f) {
             c.setAllCategoriesVisible(true);
             return f.db->last;
         },
         "setAllCategoriesVisible(true)", ""},
        {"factory", false,
         [](m::IMarkCategoryDB& c, Fixture& f) {
             return std::string(c.factory().get() == f.factory.get() ? "same" : "other");
         },
         "same", "-"},
    };
}

enum class Mode { Served, Failing, NoDatabase };

const char* modeName(Mode mode) {
    switch (mode) {
    case Mode::Served:
        return "served";
    case Mode::Failing:
        return "failing backend";
    case Mode::NoDatabase:
        return "no database";
    }
    return "";
}

} // namespace

TEST(MarkCategoryDbByImpl, EveryMethodFollowsTheLegacyShapeInEveryMode) {
    for (const Row& row : rows()) {
        for (const Mode mode : {Mode::Served, Mode::Failing, Mode::NoDatabase}) {
            SCOPED_TRACE(std::string(row.method) + " / " + modeName(mode));
            Fixture f;
            if (mode == Mode::Failing) {
                f.db->fail = true;
            }
            if (mode == Mode::NoDatabase) {
                f.changeable->impl = nullptr;
            }
            const std::string got = row.call(*f.facade, f);
            if (!row.usesBackend) {
                EXPECT_EQ(got, row.served);
                EXPECT_EQ(f.errorCount, 0);
                EXPECT_EQ(f.db->calls, 0);
            } else if (mode == Mode::Served) {
                EXPECT_EQ(got, row.served);
                EXPECT_EQ(f.errorCount, 0);
                EXPECT_GT(f.db->calls, 0);
            } else if (mode == Mode::Failing) {
                EXPECT_EQ(got, row.empty);
                EXPECT_EQ(f.errorCount, 1);
                EXPECT_GT(f.db->calls, 0);
            } else {
                EXPECT_EQ(got, row.empty);
                EXPECT_EQ(f.errorCount, 0);
                EXPECT_EQ(f.db->calls, 0);
            }
        }
    }
}

TEST(MarkCategoryDbByImpl, ForceCreateSubCategoriesCreatesEveryMissingPrefixInOrder) {
    {
        Fixture f; // empty database: "A" and "A\B" are created before "A\B\C"
        f.db->categories.clear();
        f.facade->updateCategory(nullptr, newCat(QStringLiteral("A\\B\\C")));
        EXPECT_EQ(joined(f.db->inserted), "A|A\\B|A\\B\\C");
        EXPECT_EQ(f.errorCount, 0);
    }
    {
        Fixture f; // "A" exists, "A\B" does not: only "A\B" is created
        f.db->categories = {f.a};
        f.db->inserted.clear();
        f.facade->updateCategory(nullptr, newCat(QStringLiteral("A\\B\\C")));
        EXPECT_EQ(joined(f.db->inserted), "A\\B|A\\B\\C");
    }
    {
        Fixture f; // empty parts are kept (RTL SplitString): "A\\B" asks the factory for "A" and "A\"
        f.db->categories.clear();
        f.db->inserted.clear();
        f.facade->updateCategory(nullptr, newCat(QStringLiteral("A\\\\B")));
        EXPECT_EQ(joined(f.db->inserted), "A|A\\|A\\\\B");
    }
    {
        Fixture f; // a name without a backslash creates no parent
        f.db->inserted.clear();
        f.facade->updateCategory(nullptr, newCat(QStringLiteral("Q")));
        EXPECT_EQ(joined(f.db->inserted), "Q");
    }
}

// Pins legacy quirk (L-199 in legacy-defects.md): with no category at all in the database, the sub-category
// queries answer nil even for a category that was passed in (the Build branch is skipped, :264-275 and
// :303-312).
TEST(MarkCategoryDbByImpl, EmptyDatabaseGivesNilForASubCategoryQuery) {
    Fixture f;
    f.db->categories.clear();
    EXPECT_EQ(f.facade->categoryWithSubCategories(f.a), nullptr);
    EXPECT_EQ(f.facade->subCategoryListForCategory(f.a), nullptr);
    EXPECT_EQ(f.errorCount, 0);
}

TEST(MarkCategoryDbByImpl, BackendSwitchNotifiesOnceAndMovesTheSubscription) {
    Fixture f(false);
    int changes = 0;
    const auto sub = f.facade->changeNotifier().subscribe([&changes] { ++changes; });

    f.changeable->open(implWith(f.db)); // a switch is a change
    EXPECT_EQ(changes, 1);
    f.db->changed.notify(); // the new backend's category changes are forwarded
    EXPECT_EQ(changes, 2);
    f.changeable->open(nullptr); // closing is a switch too, notified without a database
    EXPECT_EQ(changes, 3);
    f.db->changed.notify(); // the old backend is no longer subscribed
    EXPECT_EQ(changes, 3);
    EXPECT_EQ(f.errorCount, 0);
}

// Pins legacy behaviour (L-194, both constructors): the constructor calls OnDBImplChange, not
// OnImplChange, so a backend that is already open is not subscribed until the next switch.
TEST(MarkCategoryDbByImpl, ConstructorDoesNotSubscribeTheBackendThatIsAlreadyOpen) {
    Fixture f; // open at construction
    int changes = 0;
    const auto sub = f.facade->changeNotifier().subscribe([&changes] { ++changes; });
    f.db->changed.notify();
    EXPECT_EQ(changes, 0);
}

// Legacy access violation on a nil CategoryDB, caught by the try: reproduced (not a deviation).
TEST(MarkCategoryDbByImpl, NullCategoryDatabaseIsCaughtAndSwitchDoesNotNotify) {
    Fixture f(false);
    int changes = 0;
    const auto sub = f.facade->changeNotifier().subscribe([&changes] { ++changes; });
    f.changeable->open(std::make_shared<t::FakeMarkSystemImpl>()); // db is null
    EXPECT_EQ(changes, 0);
    EXPECT_EQ(f.errorCount, 1);

    f.changeable->impl = std::make_shared<t::FakeMarkSystemImpl>();
    EXPECT_EQ(f.facade->categoriesList(), nullptr);
    EXPECT_EQ(f.errorCount, 2);
}

// Legacy access violation on a nil list element, caught by the try: reproduced (not a deviation).
TEST(MarkCategoryDbByImpl, NilElementInTheCategoryListIsCaught) {
    Fixture f;
    f.db->categories = {f.a, nullptr, f.z};
    EXPECT_EQ(f.facade->visibleCategoriesIgnoreZoom(), nullptr);
    EXPECT_EQ(f.errorCount, 1);
    EXPECT_EQ(f.facade->categoryWithSubCategories(f.a), nullptr);
    EXPECT_EQ(f.errorCount, 2);
    EXPECT_EQ(f.facade->categoryListToStaticTree(m::MarkCategoryList::build({f.a, nullptr})), nullptr);
    EXPECT_EQ(f.errorCount, 3);
    // Legacy Supports(nil, IMarkCategory) is false: the nil element is skipped without an error.
    EXPECT_EQ(namesOf(f.facade->filterVisibleCategories(m::MarkCategoryList::build({f.a, nullptr, f.z}))),
              "A|Z");
    EXPECT_EQ(f.errorCount, 3);
}

// Legacy access violation on a nil factory, caught by the try: reproduced (not a deviation); needed only
// when a missing parent has to be created.
TEST(MarkCategoryDbByImpl, NullFactoryIsCaughtOnlyWhenAParentIsNeeded) {
    Fixture f;
    f.db->categories.clear();
    m::MarkCategoryDbByImpl noFactory(f.changeable, nullptr, f.errors);
    EXPECT_EQ(noFactory.updateCategory(nullptr, newCat(QStringLiteral("X\\Y"))), nullptr);
    EXPECT_EQ(f.errorCount, 1);
    EXPECT_EQ(nameOf(noFactory.updateCategory(nullptr, newCat(QStringLiteral("X")))), "X");
    EXPECT_EQ(f.errorCount, 1);
}
